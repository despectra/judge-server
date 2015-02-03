#include "server.h"

const uint8 welcome_pkt[] = {0x00, 0x01, 0xFF};
const uint8 welcome_pkt_len = 3;

const struct timeval recv_timeout = {5, 0};

compiler_def* compilers_map = NULL;
client_def* clients_map = NULL;
uint32 last_client_id = 1;

pthread_mutex_t* compilers_map_lock;
pthread_mutex_t* clients_map_lock;
pthread_mutex_t* db_lock;

void* client_loop(void *args);
void* poll_database(void* args);

client_def* client_create(int sock, uint32 id);
compiler_def* compiler_create(uint16 id);
void client_free(client_def* client);
void compiler_free(compiler_def* compiler);

void process_solutions(client_def* client);

void start_server() {
    int client_socket, listener, client_addrlen;
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
    compilers_map_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    clients_map_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    db_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(compilers_map_lock, NULL);
    pthread_mutex_init(clients_map_lock, NULL);
    pthread_mutex_init(db_lock, NULL);
    //thread_pool_execute(pool, poll_database, NULL);
    while((client_socket = accept(listener, (struct sockaddr*)&client_addr, (socklen_t*)&client_addrlen))) {
        printf("New client\n");
        endpoint* args = (endpoint*) malloc(sizeof(endpoint));
        args->socket = client_socket;
        args->addr = *((struct sockaddr*)&client_addr);
        args->addrlen = client_addrlen;
        thread_pool_execute(pool, client_loop, args);
    }
    pthread_mutex_destroy(db_lock);
    destroy_thread_pool(pool);
}

int transfer_all(int socket, bool do_send, const char* data, socklen_t data_len) {
    int transfered;
    char* cur_data_ptr = data;
    socklen_t remaining_len = data_len;
    while(remaining_len > 0) {
        transfered = do_send
                ? send(socket, cur_data_ptr, remaining_len, 0)
                : recv(socket, cur_data_ptr, remaining_len, 0);
        if(transfered <= 0) {
            if(transfered < 0) {
                //TODO notify error
            }
            break;
        }
        cur_data_ptr += transfered;
        remaining_len -= transfered;
    }
    return cur_data_ptr - data;
}

int send_all(int socket, const char* data, socklen_t data_len) {
   return transfer_all(socket, true, data, data_len);
}

int recv_all(int socket, const char* data, socklen_t data_len) {
   return transfer_all(socket, false, data, data_len);
}

void* client_loop(void* args) {
    endpoint* ep = (endpoint*) args;
    int client_sock = ep->socket;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (char*) &recv_timeout, sizeof(recv_timeout));

    send_all(client_sock, (char*)&welcome_pkt[0], welcome_pkt_len);

    uint16 res_len;
    int recvd = recv_all(client_sock, (char*) &res_len, sizeof(res_len));
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
		return NULL;
	}

    uint8 op_code = *res_pkt++;
    printf("Opcode %d\n", op_code);
    if(op_code != OP_CODE_CMP_LIST) {
        perror("Client is inadequate\n");
        free(res_pkt_ptr);
        free(args);
        return NULL;
    }

    uint8 compilers_count = *res_pkt++;
    printf("Compilers count: %d\n", compilers_count);
    uint16* compilers_ids = (uint16*) malloc(compilers_count * sizeof(uint16));
    for(int i = 0; i < compilers_count; i++) {
        compilers_ids[i] = ntohs(*((uint16*)res_pkt));
        printf("\tcompiler %d\n", compilers_ids[i]);
        res_pkt += sizeof(uint16);
    }

    // TODO test all this stuff
    pthread_mutex_lock(clients_map_lock);
    uint32 new_client_id = last_client_id++;
    client_def* client = client_create(client_sock, new_client_id);
    HASH_ADD_INT(clients_map, id, client);
    pthread_mutex_unlock(clients_map_lock);

    pthread_mutex_lock(compilers_map_lock);
    for(int i = 0; i < compilers_count; i++) {
        uint16 compiler_id = compilers_ids[i];
        compiler_def* compiler;
        HASH_FIND_INT(compilers_map, &compiler_id, compiler);
        if(compiler == NULL) {
            compiler = compiler_create(compiler_id);
            HASH_ADD_INT(compilers_map, id, compiler);
        }
        client_ptr* clnt_ptr = (client_ptr*) malloc(sizeof(client_ptr));
        clnt_ptr->id = new_client_id;
        DL_PREPEND(compiler->clients_list, clt_ptr);
    }
    pthread_mutex_unlock(compilers_map_lock);

    while(1) {
        // TODO ...and this
        process_solutions(client);

        recv_all(client_sock, (char*) &res_len, sizeof(res_len));
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            continue; //timeout
        }

        //TODO implement response processing
    }

    close(client->socket);
    free(compilers_ids);
    free(res_pkt_ptr);
    free(args);
}

client_def* client_create(int sock, uint32 id) {
    client_def* c = (client_def*) malloc(sizeof(client_def));
    c->id = id;
    c->socket = sock;
    c->loading = 0;
    c->solutions_queue = queue_init();
    c->queue_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(c->queue_mutex, NULL);
    return c;
}

compiler_def* compiler_create(uint16 id) {
    compiler_def* c = (compiler_def*) malloc(sizeof(compiler_def));
    c->id = id;
    c->clients_list = NULL;
    c->list_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(c->list_mutex, NULL);
    return c;
}

void process_solutions(client_def* client) {
    pthread_mutex_lock(client->queue_mutex);
    if(client->solutions_queue->elems_count == 0) {
        return;
    }
    data_block* sln_block = queue_pop(client->solutions_queue);
    pthread_mutex_unlock(client->queue_mutex);

    solution* sln = (solution*) sln_block->data;
    uint32 pkt_length = sizeof(char) + sizeof(sln->id) + sizeof(uint32) + sln->source_len;
    char* pkt = (char*) malloc(pkt_length * sizeof(char));
    char* pkt_ptr = pkt;
    *pkt++ = OP_CODE_CHK_SLN;
    uint32 sln_id_n = htonl(sln->id);
    uint32 src_len_n = htonl(sln->source_len);
    pkt = (char*) &sln_id_n;
    pkt += sizeof(uint32);
    pkt = (char*) &src_len_n;
    pkt += sizeof(uint32);
    memcpy(pkt, sln->source, (size_t) sln->source_len);
    send_all(client->socket, pkt_ptr, pkt_length);

    block_free(sln_block);
}

void* poll_database(void *args) {
	solution* new_solutions = NULL;
	uint64* count = NULL;
	
    while(true) {
        sleep(5);
        pthread_mutex_lock(db_lock);
        int result = solutions_extract_new(new_solutions, count);
        pthread_mutex_unlock(db_lock);
        
        if(result != 0) {
			perror("Error while extracting solutions from DB\n");
			continue;
		}
		for(uint64 i = 0; i < *count; i++) {
			solution* sln = new_solutions[i];
			uint32 compiler_id = sln->compiler_id;
			compiler_def* compiler = NULL;
			HASH_FIND_INT(compilers_map, &compiler_id, compiler);
			if(compiler == NULL) {
				//no available compiler
				//TODO report error via DB row
				continue;
			}
			
			pthread_mutex_lock(clients_map_lock);
			pthread_mutex_lock(compiler->list_mutex);
			int min_loading = 99999;
			client_def* cur_client;
			client_def* most_free_client = NULL;
			DL_FOREACH(compiler->clients_list, cur_client) {
				uint32 id = cur_client->id;
				HASH_FIND_INT(clients_map, &id, cur_client);
				if(cur_client == NULL) {
					perror("There isn't client in the hash map (but it's still in DL list)");
					continue;
				}
				if(cur_client->loading == 0) {
					most_free_client = cur_client;
					break;
				}
				if(cur_client->loading < min_loading) {
					min_loading = client->loading;
					most_free_client = cur_client;
				}
			}
			if(most_free_client == NULL) {
				perror("Most free client-checker wasn't found");
				//TODO find another client
			}
			pthread_mutex_unlock(compiler->list_mutex);
			pthread_mutex_unlock(clients_map_lock);
			
			data_block* solution_block = block_init(sln, sizeof(sln));
			pthread_mutex_lock(most_free_client->queue_mutex);
			queue_push(most_free_client->solutions_queue, solution_block);
			pthread_mutex_unlock(most_free_client->queue_mutex);
			
			solution_free(sln);
		}
    }
}
