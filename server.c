#include <stdbool.h>
#include <stddef.h>
#include <arpa/inet.h>

#include "server.h"
#include "solutions_db.h"

const uint8 welcome_pkt[] = {0x00, 0x01, 0xFF};
const uint8 welcome_pkt_len = 3;

const struct timeval recv_timeout = {5, 0};

bool server_running = true;
compiler_t* compilers_map = NULL;
client_t* clients_map = NULL;
uint32 last_client_id = 1;
logger_t* logger;

pthread_mutex_t* sln_queue_mutex;
pthread_mutex_t* compilers_map_mutex;
pthread_mutex_t* clients_map_mutex;
pthread_mutex_t* db_mutex;

void* client_loop(void* args);
void process_solutions(client_t* client);
void post_result(uint32 solution_id, uint8 result_code, uint8 test_number);
void remove_client(uint32 client_id);
void* poll_database(void* args);
int push_solution(solution_t* sln);
client_t* client_create(endpoint_t* ep, uint32 id);
compiler_t* compiler_create(uint16 id);
void client_free(client_t* client);
void compiler_free(compiler_t* compiler);

void run_server(logger_t* in_logger) {
    int client_socket, listener;
    socklen_t client_addrlen;
    struct sockaddr_in addr, client_addr;

    logger = in_logger;
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if(listener < 0) {
        logger_printf(logger, "Socket creation error");
        exit(1);
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logger_printf(logger, "Socket binding error");
        exit(2);
    }

    listen(listener, 1);
    client_addrlen = sizeof(struct sockaddr_in);
    logger_printf(logger, "Server is running...");
    printf("Judge Server v 0.1\nWaiting for incoming connections\n");

    thread_pool* pool = init_thread_pool(THREAD_POOL_CAPACITY);
    sln_queue_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    compilers_map_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    clients_map_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    db_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(sln_queue_mutex, NULL);
    pthread_mutex_init(compilers_map_mutex, NULL);
    pthread_mutex_init(clients_map_mutex, NULL);
    pthread_mutex_init(db_mutex, NULL);
    thread_pool_execute(pool, poll_database, NULL);
    while(server_running && (client_socket = accept(listener, (struct sockaddr*)&client_addr, &client_addrlen))) {
        printf("New client\n");
        logger_printf(logger, "New client connected");
        endpoint_t* new_ep = (endpoint_t*) malloc(sizeof(endpoint_t));
        new_ep->socket = client_socket;
        new_ep->addr = client_addr;
        new_ep->addrlen = client_addrlen;
        thread_pool_execute(pool, client_loop, new_ep);
    }
    //TODO implement cleanup
    pthread_mutex_destroy(db_mutex);
    destroy_thread_pool(pool);
}

ssize_t transfer_all(int socket, bool do_send, char* data, socklen_t data_len) {
    ssize_t transferred = 0;
    char* cur_data_ptr = data;
    socklen_t remaining_len = data_len;
    while(remaining_len > 0) {
        transferred = do_send
                ? send(socket, cur_data_ptr, remaining_len, 0)
                : recv(socket, cur_data_ptr, remaining_len, 0);
        if(transferred <= 0) {
            return transferred;
        }
        cur_data_ptr += transferred;
        remaining_len -= transferred;
    }
    return cur_data_ptr - data;
}

ssize_t send_all(int socket, char* data, socklen_t data_len) {
   return transfer_all(socket, true, data, data_len);
}

ssize_t recv_all(int socket, char* data, socklen_t data_len) {
   return transfer_all(socket, false, data, data_len);
}

void* client_loop(void* args) {
    endpoint_t* ep = (endpoint_t*) args;
    int client_sock = ep->socket;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (char*) &recv_timeout, sizeof(recv_timeout));

    //send handshake packet
    send_all(client_sock, (char*) &welcome_pkt[0], welcome_pkt_len);

    uint16 res_len;
    ssize_t recvd = recv_all(client_sock, (char*) &res_len, sizeof(res_len));
    if(recvd != sizeof(res_len)) {
		logger_printf(logger, "Error while receiving handshake packet (length part)");
		return NULL;
	}
    res_len = ntohs(res_len);

    //receive and parse client's compilers list
    char* res_pkt = (char*) malloc(res_len * sizeof(char));
    char* res_pkt_ptr = res_pkt;
    recvd = recv_all(client_sock, res_pkt, res_len);
	if(recvd != res_len) {
        logger_printf(logger, "Error while receiving handshake packet (data part)");
        free(res_pkt_ptr);
		return NULL;
	}

    uint8 op_code = (uint8) *res_pkt++;
    logger_printf(logger, "Client sent me opcode %d", op_code);
    if(op_code != OP_COMPILERS_LIST) {
        logger_printf(logger, "Client is insane (doesn't know anything about compilers)");
        free(res_pkt_ptr);
        free(args);
        return NULL;
    }

    uint8 compilers_count = (uint8) *res_pkt++;
    logger_printf(logger, "Available compilers count: %d", compilers_count);
    uint16* compilers_ids = (uint16*) malloc(compilers_count * sizeof(uint16));
    for(int i = 0; i < compilers_count; i++) {
        compilers_ids[i] = ntohs(*((uint16*)res_pkt));
        logger_printf(logger, "\tcompiler %d", compilers_ids[i]);
        res_pkt += sizeof(uint16);
    }

    //filling data structures
    pthread_mutex_lock(clients_map_mutex);
    uint32 new_client_id = last_client_id++;
    logger_printf(logger, "New client id: %d; Endpoint %s:%d", new_client_id, inet_ntoa(ep->addr.sin_addr), ntohs(ep->addr.sin_port));
    logger_printf(logger, "Filling data structures for new client...");
    client_t* client = client_create(ep, new_client_id);
    HASH_ADD_INT(clients_map, id, client);
    pthread_mutex_unlock(clients_map_mutex);

    pthread_mutex_lock(compilers_map_mutex);
    for(int i = 0; i < compilers_count; i++) {
        uint16 compiler_id = compilers_ids[i];
        compiler_t* compiler;
        HASH_FIND_INT(compilers_map, &compiler_id, compiler);
        if(compiler == NULL) {
            compiler = compiler_create(compiler_id);
            HASH_ADD_INT(compilers_map, id, compiler);
        }
        client_id_t* client_id = (client_id_t*) malloc(sizeof(client_id_t));
        client_id->id = new_client_id;
        HASH_ADD_INT(compiler->clients_list, id, client_id);
    }
    pthread_mutex_unlock(compilers_map_mutex);

    logger_printf(logger, "Client loop started");
    int err;
    while(server_running) {
        process_solutions(client);

        logger_printf(logger, "Waiting some data from client....");
        recvd = recv_all(client_sock, (char*) &res_len, sizeof(res_len));
        if(recvd <= 0) {
            err = errno;
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                //timeout
                logger_printf(logger, "Timeout, loop again");
                continue;
            } else if(errno == ECONNRESET) {
                //client disconnected unexpectedly
                logger_printf(logger, "Client disconnected unexpectedly");
                remove_client(new_client_id);
                break;
            } else {
                logger_printf(logger, "Error while waiting data from client (%d)", err);
                continue;
            }
        }
        logger_printf(logger, "Received something");
        res_len = ntohs(res_len);
        char* pkt = malloc(res_len * sizeof(char));
        char* pkt_ptr = pkt;
        recvd = recv_all(client_sock, pkt, res_len);
        if(recvd != res_len) {
            logger_printf(logger, "Broken packet");
            continue;
        }
        op_code = (uint8)*pkt++;
        uint32 solution_id;
        uint8 result_code;
        uint8 test_number;
        solution_t* checked_sln;
        switch(op_code) {
            case OP_RESULT:
                solution_id = ntohl(*((uint32*)pkt));
                pkt += sizeof(uint32);
                result_code = (uint8)*pkt++;
                test_number = (uint8)*pkt;

                post_result(solution_id, result_code, test_number);

                pthread_mutex_lock(client->mutex);
                checked_sln = client->checking_solution;
                client->checking_solution = NULL;
                pthread_mutex_unlock(client->mutex);
                solution_free(checked_sln);
                break;
        }

        free(pkt_ptr);
    }

    close(client_sock);
    free(compilers_ids);
    free(res_pkt_ptr);
    free(args);
    return NULL;
}

void process_solutions(client_t* client) {
    logger_printf(logger, "Lets look at client's solutions queue");
    pthread_mutex_lock(client->mutex);
    if(client->solutions_queue->elems_count == 0) {
        logger_printf(logger, "Here is nothing");
        pthread_mutex_unlock(client->mutex);
        return;
    }
    logger_printf(logger, "There is some solutions in a queue. Checking it..");
    solution_t* sln = queue_pop(client->solutions_queue);
    client->checking_solution = sln;
    pthread_mutex_unlock(client->mutex);

    size_t pkt_length = sizeof(char) + sizeof(sln->id) + sizeof(sln->compiler_id) + sizeof(sln->source_len) + sln->source_len;
    char* pkt = (char*) malloc(pkt_length* sizeof(char) + sizeof(uint16));
    char* pkt_ptr = pkt;
    *((uint16*)pkt) = htons(pkt_length);
    pkt += sizeof(uint16);
    *pkt++ = OP_CHECK_SLN;
    *((uint32*)pkt) = htonl(sln->id);
    pkt += sizeof(sln->id);
    *((uint32*)pkt) = htonl(sln->compiler_id);
    pkt += sizeof(sln->compiler_id);
    *((uint32*)pkt) = htonl(sln->source_len);
    pkt += sizeof(sln->source_len);
    memcpy(pkt, sln->source, sln->source_len);
    send_all(client->endpoint->socket, pkt_ptr, (socklen_t) pkt_length + sizeof(uint16));

}

void post_result(uint32 solution_id, uint8 result_code, uint8 test_number) {
    char str[10];
    pthread_mutex_lock(db_mutex);
    switch(result_code) {
        case 0:
            solution_post_result(solution_id, 1, "AC");
            break;
        case 1:
            snprintf(str, 10, "WA %u", test_number);
            solution_post_result(solution_id, 0, str);
            break;
        case 2:
            snprintf(str, 10, "MLE %u", test_number);
            solution_post_result(solution_id, 0, str);
            break;
        case 3:
            snprintf(str, 10, "TLE %u", test_number);
            solution_post_result(solution_id, 0, str);
            break;
        case 4:
            snprintf(str, 10, "PE %u", test_number);
            solution_post_result(solution_id, 0, str);
            break;
        case 5:
            snprintf(str, 10, "RE %u", test_number);
            solution_post_result(solution_id, 0, str);
            break;
        case 6:
            solution_post_result(solution_id, 0, "FL");
            break;
        case 7:
            solution_post_result(solution_id, 0, "CE");
            break;
        default:
            break;
    }
    pthread_mutex_unlock(db_mutex);
}

void remove_client(uint32 id) {
    logger_printf(logger, "Removing client %d from internal DB...", id);
    client_t* client = NULL;
    HASH_FIND_INT(clients_map, &id, client);
    if(client == NULL) {
        logger_printf(logger, "Client is already removed");
        return;
    }

    logger_printf(logger, "Deleting client ids...");
    //delete all records about this client from compilers map
    pthread_mutex_lock(compilers_map_mutex);
    compiler_t* compiler;
    client_id_t* clid;
    client_id_t* tmp_clid;
    for(compiler = compilers_map; compiler != NULL; compiler = compiler->hh.next) {
        pthread_mutex_lock(compiler->list_mutex);
        HASH_ITER(hh, compiler->clients_list, clid, tmp_clid) {
            if(clid->id == id) {
                HASH_DEL(compiler->clients_list, clid);
                free(clid);
            }
        }
        pthread_mutex_unlock(compiler->list_mutex);
    }
    pthread_mutex_unlock(compilers_map_mutex);

    //delete client record from clients map
    pthread_mutex_lock(clients_map_mutex);
    HASH_DEL(clients_map, client);
    last_client_id--;
    pthread_mutex_unlock(clients_map_mutex);

    //delegate solutions to other clients
    pthread_mutex_lock(client->mutex);
    if(client->checking_solution != NULL) {
        push_solution(client->checking_solution);
        client->checking_solution = NULL;
    }

    if(client->solutions_queue->elems_count > 0) {
        solution_t* pending_sln = NULL;
        while((pending_sln = (solution_t*) queue_pop(client->solutions_queue)) != NULL) {
            push_solution(pending_sln);
        }
    }
    pthread_mutex_unlock(client->mutex);
    client_free(client);
}

client_t* client_create(endpoint_t* ep, uint32 id) {
    client_t* c = (client_t*) malloc(sizeof(client_t));
    c->id = id;
    c->endpoint = ep;
    c->solutions_queue = queue_init();
    c->mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(c->mutex, NULL);
    return c;
}

compiler_t* compiler_create(uint16 id) {
    compiler_t* c = (compiler_t*) malloc(sizeof(compiler_t));
    c->id = id;
    c->clients_list = NULL;
    c->list_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(c->list_mutex, NULL);
    return c;
}

void client_free(client_t* client) {
    if(client == NULL) {
        return;
    }
    queue_struct_free(client->solutions_queue);
    client->endpoint = NULL;
    pthread_mutex_destroy(client->mutex);
    free(client);
}

void* poll_database(void* args) {
    if(solutions_init_db(logger) == ERR_DB) {
        return NULL;
    }
	solution_t* new_solutions = NULL;
    solution_t* cur_solution = NULL;
	uint64 count;

    logger_printf(logger, "Database polling loop started");
    while(server_running) {
        sleep(5);
        pthread_mutex_lock(db_mutex);
        logger_printf(logger, "Let's look into MySQL DB");
        int result = solutions_extract_new(&new_solutions, &count);
        pthread_mutex_unlock(db_mutex);

        if(result != 0) {
			logger_printf(logger, "Error while extracting solutions from DB");
			continue;
		}
        if(count > 0) {
            logger_printf(logger, "%d new solution_t(s) was submitted", count);
        }
		for(uint64 i = 0; i < count; i++) {
            cur_solution = &new_solutions[i];
            push_solution(cur_solution);
        }
    }
    solutions_close_db();
    return NULL;
}

int push_solution(solution_t* sln) {
    uint32 compiler_id = sln->compiler_id;
    logger_printf(logger, "SLN %d: Choosing client for checking  solution_t on compiler %d", sln->id, compiler_id);
    compiler_t* compiler = NULL;
    HASH_FIND_INT(compilers_map, &compiler_id, compiler);
    if(compiler == NULL) {
        logger_printf(logger, "SLN %d: Solution can't be checked, no compiler", sln->id);
        solution_post_result(sln->id, 0, "FAIL no compiler");
        return ERR_NOCOMPILER;
    }

    pthread_mutex_lock(clients_map_mutex);
    pthread_mutex_lock(compiler->list_mutex);
    uint32 min_loading = INT32_MAX;
    uint32 cur_loading;
    client_id_t* c;
    client_t* cur_client;
    client_t* most_free_client = NULL;

    for(c = compiler->clients_list; c != NULL; c = c->hh.next) {
        uint32 id = c->id;
        HASH_FIND_INT(clients_map, &id, cur_client);
        if(cur_client == NULL) {
            logger_printf(logger, "SLN %d: INCONSISTENCY: no client in the hash map (but it's still in compiler's list of clients)", sln->id);
            continue;
        }
        pthread_mutex_lock(cur_client->mutex);
        cur_loading = cur_client->solutions_queue->elems_count + (cur_client->checking_solution != NULL) ? 1 : 0;
        pthread_mutex_unlock(cur_client->mutex);
        if(cur_loading == 0) {
            most_free_client = cur_client;
            break;
        }
        if(cur_loading <= min_loading) {
            min_loading = cur_loading;
            most_free_client = cur_client;
        }
    }
    if(most_free_client == NULL) {
        logger_printf(logger, "SLN %d: Most free client-checker wasn't found", sln->id);
        //TODO find another client
        solution_post_result(sln->id, 0, "FAIL syserr");
        pthread_mutex_unlock(compiler->list_mutex);
        pthread_mutex_unlock(clients_map_mutex);
        return ERR_NOCLIENTS;
    }
    pthread_mutex_unlock(compiler->list_mutex);
    pthread_mutex_unlock(clients_map_mutex);

    logger_printf(logger, "SLN %d: Client found (%d), pushing solution to it's queue", sln->id, most_free_client->id);
    pthread_mutex_lock(most_free_client->mutex);
    queue_push(most_free_client->solutions_queue, sln);
    pthread_mutex_unlock(most_free_client->mutex);
    return 0;
}
