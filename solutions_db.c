#include "solutions_db.h"

MYSQL* conn;

const char* server = "localhost";
const char* user = "contestphp";
const char* password = "c0nt3st";
const char* database = "contestphp";

logger_t* logger;

int solutions_init_db(logger_t* in_logger) {
    conn = mysql_init(NULL);
    if(!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0)) {
        logger_printf(logger, "MYSQL %s", mysql_error(conn));
        return 1;
    }
    return 0;
}

int solutions_extract_new(solution* sln_arr, uint64* sln_arr_len) {
    if (mysql_query(conn, "select * from solutions where checking = 0")) {
        logger_printf(logger, "MYSQL %s\n", mysql_error(conn));
        return 1;
    }
    MYSQL_ROW row;
    MYSQL_RES* result = mysql_use_result(conn);
    if(mysql_field_count(result) != FIELDS_COUNT) {
        return 2;
    }

    if(sln_arr_len == NULL) {
        sln_arr_len = (uint64) malloc(sizeof(uint64));
    }

    *sln_arr_len = (uint64) mysql_num_rows(result);
    sln_arr = (solution*) malloc((*sln_arr_len) * sizeof(solution));
    if(sln_arr == NULL) {
        return 3;
    }
    
    solution* cur_ptr = sln_arr;
    uint32* rows_lengths;
    while((row = mysql_fetch_row(result)) != NULL) {
        rows_lengths = mysql_fetch_lengths(result);
        cur_ptr->id = atoi(row[0]);
        cur_ptr->user_id = atoi(row[1]);
        cur_ptr->task_id = atoi(row[2]);
        cur_ptr->time = NULL; // @todo converting db string to time
        cur_ptr->source_len = rows_lengths[4];
        cur_ptr->source = (char*) malloc(cur_ptr->source_len * sizeof(char));
        memcpy(cur_ptr->source, row[4], (size_t)cur_ptr->source_len);
        cur_ptr->compiler_id = atoi(row[5]);
        cur_ptr->checking = atoi(row[6]);
        cur_ptr->accepted = atoi(row[7]);
        cur_ptr->response_len = rows_lengths[8];
        cur_ptr->response = (char*) malloc(cur_ptr->response_len * sizeof(char));
        memcpy(cur_ptr->response, row[8], cur_ptr->response_len);
        cur_ptr += sizeof(solution);
    }
    
    mysql_free_result(result);
    return 0;
}


void solutions_close_db() {
    logger = NULL;
    mysql_close(conn);
}

void solution_free(solution* sln) {
	if(sln == NULL) {
		return;
	}
	free(sln->source);
	free(sln->response);
	free(sln);
}
