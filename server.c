#include <stddef.h>
#include <arpa/inet.h>

#include "server.h"

const uint8 welcome_pkt[] = {0x00, 0x01, 0xFF};
const uint8 welcome_pkt_len = 3;

const struct timeval recv_timeout = {RECV_TIMEOUT_SEC, 0};
const struct timeval send_timeout = {SEND_TIMEOUT_SEC, 0};

volatile char server_running = 1;
static compiler_t* compilers_map = NULL;
static client_t* clients_map = NULL;
static uint32 last_client_id = 1;
static logger_t* logger;

static pthread_mutex_t* compilers_map_mutex;
static pthread_mutex_t* clients_map_mutex;
static pthread_mutex_t* db_mutex;

void* client_loop(void* args);
int process_solutions(client_t* client);
void post_result(uint32 solution_id, uint8 result_code, uint8 test_number);
void* poll_database(void* args);
int push_solution(solution_t* sln);
void remove_client(uint32 client_id);
client_t* client_create(endpoint_t* ep, uint32 id);
compiler_t* compiler_create(uint16 id);
void client_free(client_t* client);
void compiler_free(compiler_t* compiler);

void* local_control_loop(void*);
void cleanup();

void run_server(logger_t* in_logger) {
    int client_socket, listener;
    socklen_t client_addrlen;
    struct sockaddr_in addr, client_addr;

    logger = in_logger;
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if(listener < 0) {
        logger_printf(logger, "Socket creation error: %s", strerror(errno));
        exit(1);
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logger_printf(logger, "Socket binding error: %s", strerror(errno));
        exit(2);
    }

    listen(listener, 1);
    client_addrlen = sizeof(struct sockaddr_in);
    logger_printf(logger, "Judge Server v%s is running. Waiting for incoming connections", VERSION);

    thread_pool* pool = init_thread_pool(THREAD_POOL_CAPACITY);
    compilers_map_mutex = malloc(sizeof(pthread_mutex_t));
    clients_map_mutex = malloc(sizeof(pthread_mutex_t));
    db_mutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(compilers_map_mutex, NULL);
    pthread_mutex_init(clients_map_mutex, NULL);
    pthread_mutex_init(db_mutex, NULL);
    thread_pool_execute(pool, poll_database, NULL);
    thread_pool_execute(pool, local_control_loop, NULL);
    while(server_running && (client_socket = accept(listener, (struct sockaddr*)&client_addr, &client_addrlen))) {
        if(!server_running) {
            logger_printf(logger, "Server has been stopped");
            close(client_socket);
            break;
        }
        logger_printf(logger, "New client connected. More info below");
        endpoint_t* new_ep = malloc(sizeof(endpoint_t));
        new_ep->socket = client_socket;
        new_ep->addr = client_addr;
        new_ep->addrlen = client_addrlen;
        thread_pool_execute(pool, client_loop, new_ep);
    }
    logger_printf(logger, "Stopping all threads...");
    destroy_thread_pool(pool);
    logger_printf(logger, "All worker threads have been destroyed");
    cleanup();
}

void* client_loop(void* args) {
    endpoint_t* ep = malloc(sizeof(endpoint_t));
    memcpy(ep, args, sizeof(endpoint_t));
    free(args);

    int client_sock = ep->socket;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (char*) &recv_timeout, sizeof(recv_timeout));
    setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, (char*) &send_timeout, sizeof(send_timeout));

    //send handshake packet
    send_all(client_sock, (char*) &welcome_pkt[0], welcome_pkt_len);

    uint16 res_len;
    ssize_t recvd = recv_all(client_sock, (char*) &res_len, sizeof(res_len));
    if(recvd != sizeof(res_len)) {
		logger_printf(logger, "Error while receiving handshake packet (length part)");
        close(client_sock);
        free(ep);
		return NULL;
	}
    res_len = ntohs(res_len);

    //receive and parse client's compilers list
    char* res_pkt = malloc(res_len * sizeof(char));
    char* res_pkt_ptr = res_pkt;
    recvd = recv_all(client_sock, res_pkt, res_len);
	if(recvd != res_len) {
        logger_printf(logger, "Error while receiving handshake packet (data part)");
        close(client_sock);
        free(res_pkt_ptr);
        free(ep);
        return NULL;
    }

    uint8 op_code = (uint8) *res_pkt++;
    logger_printf(logger, "Client sent me opcode %d", op_code);
    if(op_code != OP_COMPILERS_LIST) {
        logger_printf(logger, "Client is insane (doesn't know anything about compilers)");
        close(client_sock);
        free(res_pkt_ptr);
        free(ep);
        return NULL;
    }

    uint8 compilers_count = (uint8) *res_pkt++;
    logger_printf(logger, "Available compilers count: %d", compilers_count);
    uint16* compilers_ids = malloc(compilers_count * sizeof(uint16));
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
    client->recv_pkt = res_pkt_ptr;
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
        client_id_t* client_id = malloc(sizeof(client_id_t));
        client_id->id = new_client_id;
        HASH_ADD_INT(compiler->clients_list, id, client_id);
    }
    pthread_mutex_unlock(compilers_map_mutex);
    free(compilers_ids);

    logger_printf(logger, "Client loop started");
    while(server_running) {
        int status = process_solutions(client);
        if(status == CLI_SENDTIMEOUT || status == CLI_CONNERROR) {
            logger_printf(logger, "Network problem while sending solution to client. Disconnecting...");
            remove_client(new_client_id);
            break;
        }

        recvd = recv_all(client_sock, (char*) &res_len, sizeof(res_len));
        if(recvd <= 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                //timeout
                if(client->checking_solution != NULL) {
                    client->last_recv_delay += recv_timeout.tv_sec;
                    if(client->last_recv_delay >= RECV_MAX_TIMEOUT) {
                        //client died while checking solution
                        logger_printf(logger, "Client hang while checking soluton (%d sec. passed)", RECV_MAX_TIMEOUT);
                        remove_client(new_client_id);
                        break;
                    }
                } else if(client->last_recv_delay > 0) {
                    client->last_recv_delay = 0;
                }
                continue;
            } else if(errno == ECONNRESET) {
                //client disconnected unexpectedly
                logger_printf(logger, "Client disconnected unexpectedly");
                remove_client(new_client_id);
                break;
            } else {
                logger_printf(logger, "Error while waiting data from client (%s)", strerror(errno));
                continue;
            }
        }
        logger_printf(logger, "Received something");
        res_len = ntohs(res_len);
        client->recv_pkt = realloc(client->recv_pkt, res_len * sizeof(char));
        res_pkt = client->recv_pkt;
        recvd = recv_all(client_sock, client->recv_pkt, res_len);
        if(recvd != res_len) {
            logger_printf(logger, "Broken packet");
            continue;
        }
        op_code = (uint8)*res_pkt++;
        uint32 solution_id;
        uint8 result_code;
        uint8 test_number;
        solution_t* checked_sln;
        switch(op_code) {
            case OP_RESULT:
                solution_id = ntohl(*((uint32*)res_pkt));
                res_pkt += sizeof(uint32);
                result_code = (uint8)*res_pkt++;
                test_number = (uint8)*res_pkt;

                post_result(solution_id, result_code, test_number);

                pthread_mutex_lock(client->mutex);
                checked_sln = client->checking_solution;
                client->checking_solution = NULL;
                pthread_mutex_unlock(client->mutex);
                solution_free(checked_sln);
                break;
            default:
                break;
        }
    }

    logger_printf(logger, "Client loop ended");
    return NULL;
}

int process_solutions(client_t* client) {
    pthread_mutex_lock(client->mutex);
    if(client->solutions_queue->elems_count == 0) {
        pthread_mutex_unlock(client->mutex);
        return 0;
    }
    logger_printf(logger, "There is some solutions in a queue. Checking it..");
    solution_t* sln = queue_pop(client->solutions_queue);
    client->checking_solution = sln;
    pthread_mutex_unlock(client->mutex);

    size_t pkt_length = sizeof(char) + sizeof(sln->id) + sizeof(sln->compiler_id) + sizeof(sln->source_len) + sln->source_len;
    size_t total_length = sizeof(uint16) + pkt_length;
    client->send_pkt = realloc(client->send_pkt, total_length);
    char* pkt = client->send_pkt;
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
    ssize_t sent;
    do {
        sent = send_all(client->endpoint->socket, client->send_pkt, (socklen_t) total_length);
        if(sent <= 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                client->last_send_delay += send_timeout.tv_sec;
                if(client->last_send_delay >= SEND_MAX_TIMEOUT) {
                    return CLI_SENDTIMEOUT;
                }
            } else if(errno == ECONNRESET) {
                return CLI_CONNERROR;
            }
        }
    } while(sent <= 0);
    if(sent < total_length) {
        return CLI_CONNERROR;
    }
    return 0;
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
    logger_printf(logger, "Database polling loop ended");
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
        solution_post_result(sln->id, 0, "FL");
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
        solution_post_result(sln->id, 0, "FL");
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

void remove_client(uint32 id) {
    logger_printf(logger, "Removing client %d from internal DB...", id);
    client_t* client = NULL;
    HASH_FIND_INT(clients_map, &id, client);
    if(client == NULL) {
        logger_printf(logger, "Client is already removed");
        return;
    }

    //delete all records about this client from compilers map
    logger_printf(logger, "Deleting client ids...");
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
    logger_printf(logger, "Pushing remaining solutions to other clients...");
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
    client_t* c = malloc(sizeof(client_t));
    c->id = id;
    c->endpoint = ep;
    c->solutions_queue = queue_init();
    c->checking_solution = NULL;
    c->last_recv_delay = 0;
    c->last_send_delay = 0;
    c->recv_pkt = NULL;
    c->send_pkt = NULL;
    c->mutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(c->mutex, NULL);
    return c;
}

compiler_t* compiler_create(uint16 id) {
    compiler_t* c = malloc(sizeof(compiler_t));
    c->id = id;
    c->clients_list = NULL;
    c->list_mutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(c->list_mutex, NULL);
    return c;
}

void client_free(client_t* client) {
    if(client == NULL) {
        return;
    }
    close(client->endpoint->socket);
    queue_iterate(client->solutions_queue, solution_free);
    queue_struct_free(client->solutions_queue);
    if(client->checking_solution != NULL) {
        solution_free(client->checking_solution);
    }
    if(client->recv_pkt != NULL) {
        free(client->recv_pkt);
    }
    if(client->send_pkt != NULL) {
        free(client->send_pkt);
    }
    free(client->endpoint);
    pthread_mutex_destroy(client->mutex);
    free(client);
}

void compiler_free(compiler_t* compiler) {
    if(compiler == NULL) {
        return;
    }
    client_id_t *cur, *tmp;
    HASH_ITER(hh, compiler->clients_list, cur, tmp) {
        HASH_DEL(compiler->clients_list, cur);
        free(cur);
    }
    free(compiler);
}

void* local_control_loop(void* args) {
    int local_socket = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LOCAL_CONTROL_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(local_socket, (struct sockaddr*) &addr, sizeof(addr));

    struct sockaddr controller_addr;
    socklen_t controller_addrlen;
    char recv_buf[100];

    char srv_running_msg[] = "Server is running";
    char srv_stopped_msg[] = "Server is stopped";
    do {
        recvfrom(local_socket, recv_buf, 100, 0, &controller_addr, &controller_addrlen);
        if(strcmp(recv_buf, "status") == 0) {
            char* msg = server_running ? srv_running_msg : srv_stopped_msg;
            sendto(local_socket, msg, strlen(msg), 0, &controller_addr, controller_addrlen);
        } else if(strcmp(recv_buf, "stop") == 0) {
            logger_printf(logger, "Received stop request from controller");
            server_running = 0;
            //Mock client connects to server to wake it up on accept call
            int mock_sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in mock_addr;
            mock_addr.sin_family = AF_INET;
            mock_addr.sin_port = htons(PORT);
            mock_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            connect(mock_sock, (struct sockaddr*) &mock_addr, sizeof(mock_addr));
            close(mock_sock);
            break;
        }
    } while(1);
    close(local_socket);
    return NULL;
}


void cleanup() {
    //CALL ONLY AFTER DESTROYING THREAD POOL!!
    //it's guaranteed that all worker threads are stopped (1 db-polling and n clients threads)

    logger_printf(logger, "Cleaning up resources..");
    client_t *cur_cli, *tmp_cli;
    HASH_ITER(hh, clients_map, cur_cli, tmp_cli) {
        HASH_DEL(clients_map, cur_cli);
        client_free(cur_cli);
    }

    compiler_t *cur_comp, *tmp_comp;
    HASH_ITER(hh, compilers_map, cur_comp, tmp_comp) {
        HASH_DEL(compilers_map, cur_comp);
        compiler_free(cur_comp);
    }

    pthread_mutex_destroy(clients_map_mutex);
    pthread_mutex_destroy(compilers_map_mutex);
    pthread_mutex_destroy(db_mutex);
}