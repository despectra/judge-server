#include "server.h"

const uint8 welcome_pkt[] = {0x00, 0x01, 0xFF};
const uint8 welcome_pkt_len = 3;

const struct timeval recv_timeout = {5, 0};

bool server_running = true;
compiler_t* compilers_map = NULL;
client_t* clients_map = NULL;
uint32 last_client_id = 1;

pthread_mutex_t* compilers_map_mutex;
pthread_mutex_t* clients_map_mutex;
pthread_mutex_t* db_mutex;

void* client_loop(void* args);
void* poll_database(void* args);
client_t* client_create(endpoint_t* ep, uint32 id);
compiler_t* compiler_create(uint16 id);
void client_free(client_t* client);
void compiler_free(compiler_t* compiler);
void process_solutions(client_t* client);

void start_server() {
    int client_socket, listener;
    socklen_t client_addrlen;
    struct sockaddr_in addr, client_addr;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if(listener < 0) {
        perror("Socket");
        exit(1);
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind");
        exit(2);
    }

    listen(listener, 1);
    client_addrlen = sizeof(struct sockaddr_in);
    printf("Judge Server v 0.1\nWaiting for incoming connections\n");

    thread_pool* pool = init_thread_pool(THREAD_POOL_CAPACITY);
    compilers_map_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    clients_map_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    db_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(compilers_map_mutex, NULL);
    pthread_mutex_init(clients_map_mutex, NULL);
    pthread_mutex_init(db_mutex, NULL);
    //thread_pool_execute(pool, poll_database, NULL);
    while(server_running && (client_socket = accept(listener, (struct sockaddr*)&client_addr, &client_addrlen))) {
        printf("New client\n");
        endpoint_t*new_ep = (endpoint_t*) malloc(sizeof(endpoint_t));
        new_ep->socket = client_socket;
        new_ep->addr = *((struct sockaddr*)&client_addr);
        new_ep->addrlen = client_addrlen;
        thread_pool_execute(pool, client_loop, new_ep);
    }
    pthread_mutex_destroy(db_mutex);
    destroy_thread_pool(pool);
}

uint64 transfer_all(int socket, bool do_send, char* data, socklen_t data_len) {
    uint32 transferred;
    char* cur_data_ptr = data;
    socklen_t remaining_len = data_len;
    while(remaining_len > 0) {
        transferred = (uint32) (do_send
                ? send(socket, cur_data_ptr, remaining_len, 0)
                : recv(socket, cur_data_ptr, remaining_len, 0));
        if(transferred <= 0) {
            if(transferred < 0) {
                //TODO notify error
            }
            break;
        }
        cur_data_ptr += transferred;
        remaining_len -= transferred;
    }
    return cur_data_ptr - data;
}

uint64 send_all(int socket, char* data, socklen_t data_len) {
   return transfer_all(socket, true, data, data_len);
}

uint64 recv_all(int socket, char* data, socklen_t data_len) {
   return transfer_all(socket, false, data, data_len);
}

void* client_loop(void* args) {
    endpoint_t* ep = (endpoint_t*) args;
    int client_sock = ep->socket;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (char*) &recv_timeout, sizeof(recv_timeout));

    send_all(client_sock, (char*) &welcome_pkt[0], welcome_pkt_len);

    uint16 res_len;
    uint64 recvd = recv_all(client_sock, (char*) &res_len, sizeof(res_len));
    if(recvd != sizeof(res_len)) {
		perror("Error while receiving handshake packet\n");
		return NULL;
	}
    res_len = ntohs(res_len);

    char* res_pkt = (char*) malloc(res_len * sizeof(char));
    char* res_pkt_ptr = res_pkt;
    recvd = recv_all(client_sock, res_pkt, res_len);
	if(recvd != res_len) {
		perror("Error while receiving handshake packet\n");
        free(res_pkt_ptr);
		return NULL;
	}

    uint8 op_code = (uint8) *res_pkt++;
    printf("Opcode %d\n", op_code);
    if(op_code != OP_COMPILERS_LIST) {
        perror("Client is inadequate\n");
        free(res_pkt_ptr);
        free(args);
        return NULL;
    }

    uint8 compilers_count = (uint8) *res_pkt++;
    printf("Compilers count: %d\n", compilers_count);
    uint16* compilers_ids = (uint16*) malloc(compilers_count * sizeof(uint16));
    for(int i = 0; i < compilers_count; i++) {
        compilers_ids[i] = ntohs(*((uint16*)res_pkt));
        printf("\tcompiler %d\n", compilers_ids[i]);
        res_pkt += sizeof(uint16);
    }

    // TODO test all this stuff
    pthread_mutex_lock(clients_map_mutex);
    uint32 new_client_id = last_client_id++;
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

    while(server_running) {
        // TODO ...test this too
        process_solutions(client);

        recv_all(client_sock, (char*) &res_len, sizeof(res_len));
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            continue; //timeout
        }

        //TODO implement response processing
    }

    close(client_sock);
    free(compilers_ids);
    free(res_pkt_ptr);
    free(args);
}

client_t* client_create(endpoint_t* ep, uint32 id) {
    client_t* c = (client_t*) malloc(sizeof(client_t));
    c->id = id;
    c->endpoint = ep;
    c->loading = 0;
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

void process_solutions(client_t* client) {
    pthread_mutex_lock(client->mutex);
    if(client->solutions_queue->elems_count == 0) {
        pthread_mutex_unlock(client->mutex);
        return;
    }
    data_block* sln_block = queue_pop(client->solutions_queue);
    client->loading++;
    pthread_mutex_unlock(client->mutex);

    solution* sln = (solution*) sln_block->data;
    uint32 pkt_length = sizeof(char) + sizeof(sln->id) + sizeof(uint32) + sln->source_len;
    char* pkt = (char*) malloc(pkt_length* sizeof(char));
    char* pkt_ptr = pkt;
    *pkt++ = OP_CHECK_SLN;
    uint32 sln_id_n = htonl(sln->id);
    uint32 src_len_n = htonl(sln->source_len);
    pkt = (char*) &sln_id_n;
    pkt += sizeof(uint32);
    pkt = (char*) &src_len_n;
    pkt += sizeof(uint32);
    memcpy(pkt, sln->source, (size_t) sln->source_len);
    send_all(client->endpoint->socket, pkt_ptr, (socklen_t) pkt_length);

    block_free(sln_block);
}

void* poll_database(void* args) {
    solutions_init_db();
	solution* new_solutions = NULL;
    solution* cur_solution = NULL;
	uint64* count = NULL;
	
    while(server_running) {
        sleep(5);
        pthread_mutex_lock(db_mutex);
        int result = solutions_extract_new(new_solutions, count);
        pthread_mutex_unlock(db_mutex);

        if(result != 0) {
			perror("Error while extracting solutions from DB\n");
			continue;
		}
		for(uint64 i = 0; i < *count; i++) {
			cur_solution = new_solutions + i * sizeof(solution);
			uint32 compiler_id = cur_solution->compiler_id;
			compiler_t* compiler = NULL;
			HASH_FIND_INT(compilers_map, &compiler_id, compiler);
			if(compiler == NULL) {
				//no available compiler
				//TODO report error via DB row
				continue;
			}

			pthread_mutex_lock(clients_map_mutex);
			pthread_mutex_lock(compiler->list_mutex);
			uint32 min_loading = 99999;
			client_id_t* c;
            client_t* cur_client;
			client_t* most_free_client = NULL;
			for(c = compiler->clients_list; c != NULL; c = c->hh.next) {
				uint32 id = c->id;
				HASH_FIND_INT(clients_map, &id, cur_client);
				if(cur_client == NULL) {
					perror("There isn't client in the hash map (but it's still in compiler's list of clients)");
					continue;
				}
                pthread_mutex_lock(cur_client->mutex);
				if(cur_client->loading == 0) {
					most_free_client = cur_client;
					break;
				}
				if(cur_client->loading < min_loading) {
					min_loading = cur_client->loading;
					most_free_client = cur_client;
				}
                pthread_mutex_unlock(cur_client->mutex);
			}
			if(most_free_client == NULL) {
				perror("Most free client-checker wasn't found");
                //almost impossible situation BUT ANYWAY TODO find another client
			}
			pthread_mutex_unlock(compiler->list_mutex);
			pthread_mutex_unlock(clients_map_mutex);
			
			data_block* solution_block = block_init(cur_solution, sizeof(cur_solution));
			pthread_mutex_lock(most_free_client->mutex);
			queue_push(most_free_client->solutions_queue, solution_block);
			pthread_mutex_unlock(most_free_client->mutex);
			
			solution_free(cur_solution);
		}
    }
    solutions_close_db();
}
