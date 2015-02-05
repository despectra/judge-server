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

typedef struct {
    uint32 id;
    uint32 user_id;
    uint32 task_id;
    time_t time;
    char* source;
    uint32 source_len;
    uint32 compiler_id;
    uint8 checking;
    bool accepted;
    char* response;
    uint8 response_len;
} solution;

int solutions_init_db(logger_t* logger);
void solutions_close_db();
int solutions_extract_new(solution* sln_arr, uint64* sln_arr_len);
void solution_free(solution* sln);

#endif // SOLUTIONS_DB_H
