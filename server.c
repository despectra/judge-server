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


    thread_pool* pool = init_thread_pool(2);
    compilers_map_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    clients_map_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    db_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(compilers_map_lock, NULL);
    pthread_mutex_init(clients_map_lock, NULL);
    pthread_mutex_init(db_lock, NULL);

    //thread_pool_execute(pool, poll_database, NULL);
    while((client_socket = accept(listener, (struct sockaddr*)&client_addr, (socklen_t*)&client_addrlen))) {
        printf("New client\n");
        client_args* args = (client_args*) malloc(sizeof(client_args));
        args->socket = client_socket;
        args->addr = *((struct sockaddr*)&client_addr);
        args->addrlen = client_addrlen;
        thread_pool_execute(pool, client_loop, args);
    }
    pthread_mutex_destroy(db_lock);
    destroy_thread_pool(pool);
}

void transfer_all(int socket, bool do_send, const char* data, socklen_t data_len) {
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
}

void send_all(int socket, const char* data, socklen_t data_len) {
    transfer_all(socket, true, data, data_len);
}

void recv_all(int socket, const char* data, socklen_t data_len) {
    transfer_all(socket, false, data, data_len);
}

void* client_loop(void* args) {
    client_args* client = (client_args*) args;
    int client_sock = client->socket;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (char*) &recv_timeout, sizeof(recv_timeout));

    send_all(client_sock, (char*)&welcome_pkt[0], welcome_pkt_len);

    //get response packet length
    uint16 res_len;
    recv_all(client_sock, (char*) &res_len, sizeof(res_len));
    res_len = ntohs(res_len);

    char* res_pkt = (char*) malloc(res_len * sizeof(char));
    char* res_pkt_ptr = res_pkt;
    recv_all(client_sock, res_pkt, res_len);

    uint8 op_code = *res_pkt++;
    printf("Opcode %d\n", op_code);
    if(op_code != OP_CODE_CMP_LIST) {
        perror("Client is inadequate\n");
        free(res_pkt_ptr);
        free(args);
        return;
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

        client_ptr* clt_ptr = (client_ptr*) malloc(sizeof(client_ptr));
        clt_ptr->id = new_client_id;
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
    uint32 pkt_length = 1 + sizeof(sln->id) + sizeof(uint32) + sln->source_len;
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
    while(true) {
        sleep(5);
        pthread_mutex_lock(db_lock);
        //poll
        pthread_mutex_unlock(db_lock);
    }
}
