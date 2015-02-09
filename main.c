#include "server.h"

#include <sys/stat.h>
#include <pwd.h>

logger_t* logger;

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

db_config* read_db_config() {
    FILE* cfg_file;
    struct passwd* pw = getpwuid(getuid());
    const char* homedir = pw->pw_dir;
    char cfg_fname[100];
    snprintf(cfg_fname, 100, "%s/.judge-server/db.conf", homedir);
    if((cfg_file = fopen(cfg_fname, "r")) == NULL) {
        printf("Error! Config file not found: %s. Create it manually and fill with MySQL DB credentials\n", cfg_fname);
        exit(EXIT_FAILURE);
    }
    long size;
    fseek(cfg_file, 0L, SEEK_END);
    size = ftell(cfg_file);
    fseek(cfg_file, 0L, SEEK_SET);

    char* cfg_buf = malloc(size);
    fread(cfg_buf, 1, size, cfg_file);
    fclose(cfg_file);

    db_config* conf = malloc(sizeof(db_config));
    conf->host = strtok(cfg_buf, "\n");
    conf->user = strtok(NULL, "\n");
    conf->password = strtok(NULL, "\n");
    conf->db_name = strtok(NULL, "\n");

    return conf;
}

int main(int argc, char* argv[]) {

    db_config* conf = read_db_config();
    solutions_configure_db(conf);

    const char* dirname;
    if(argc == 2) {
        dirname = argv[1];
    } else {
        struct passwd* pw = getpwuid(getuid());
        dirname = pw->pw_dir;
    }
    daemon_init(dirname);
    logger = logger_init();
    run_server(logger);

    free(conf->host);
    free(conf);
    logger_destroy(logger);
    return 0;
}
