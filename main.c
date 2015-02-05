#include "server.h"

logger_t* logger;

void cleanup();

int main(int argc, char* argv[]) {

    logger = logger_init();
    atexit(cleanup);
    run_server(logger);
    return 0;
}

void cleanup() {
    logger_destroy(logger);
}
