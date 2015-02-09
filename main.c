#include "server.h"

#include <signal.h>
#include <sys/stat.h>

logger_t* logger;

void cleanup(int);

void daemon_init(const char* dir) {
    pid_t pid;
    pid = fork();
    if(pid == -1) {
        printf("Error: Start Daemon failed (%s)\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(!pid) {
        umask(0);
        setsid();
        chdir(dir);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        FILE* pidf;
        pidf = fopen("mypid", "w+");
        if (pidf) {
            fprintf(pidf, "%u", getpid());
            fclose(pidf);
        }
        return;
    } else {
        printf("Daemon has been started successfully\n");
        exit(EXIT_SUCCESS);
    }
}

int main(int argc, char* argv[]) {
    const char* dirname;
    if(argc == 2) {
        dirname = argv[1];
    } else {
        dirname = "~";
    }
    daemon_init(dirname);
    logger = logger_init();
    run_server(logger);
    return 0;
}

void cleanup(int s) {
    logger_destroy(logger);
}
