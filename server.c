#include "server.h"

const uint8 welcome_pkt[] = {0x00, 0x01, 0xFF};
const uint8 welcome_pkt_len = 3;

pthread_mutex_t* db_lock;

void* client_loop(void *args);
void* poll_database(void* args);

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

    send_all(client->socket, (char*)&welcome_pkt[0], welcome_pkt_len);

    //get response packet length
    uint16 res_len;
    recv_all(client->socket, (char*)&res_len, sizeof(uint16));
    res_len = ntohs(res_len);

    char* res_pkt = (char*) malloc(res_len * sizeof(char));
    char* res_pkt_ptr = res_pkt;
    recv_all(client->socket, res_pkt, res_len);

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

    close(client->socket);
    free(compilers_ids);
    free(res_pkt_ptr);
    free(args);
}

void* poll_database(void *args) {
    while(true) {
        sleep(5);
        pthread_mutex_lock(db_lock);
        //poll
        pthread_mutex_unlock(db_lock);
    }
}
