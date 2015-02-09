#ifndef SOLUTIONS_DB_H
#define SOLUTIONS_DB_H

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <mysql/mysql.h>
#include <mysql/typelib.h>
#include <time.h>
#include "utils.h"

#define FIELDS_COUNT 9

#define SERVER "localhost"
#define USER "judge"
#define PASSWORD "12345"
#define DATABASE "judgeweb"

#define ERR_DB 1
#define ERR_SCHEMA 2
#define ERR_ARGS 3

typedef struct {
    uint32 id;
    uint32 user_id;
    uint32 task_id;
    time_t* time;
    char* source;
    uint32 source_len;
    uint32 compiler_id;
    uint8 checking;
    bool accepted;
    char* response;
    uint8 response_len;
} solution_t;

typedef struct {
    const char* host;
    const char* user;
    const char* password;
    const char* db_name;
} db_config;

void solutions_configure_db(db_config* conf);
int solutions_init_db(logger_t* logger);
void solutions_close_db();
int solutions_extract_new(solution_t** sln_arr_ptr, uint64* sln_arr_len);
int solution_post_result(uint32 sln_id, int8 accepted, char* response_text);
void solution_free(solution_t* sln);

#endif // SOLUTIONS_DB_H
