#include <assert.h>
#include "solutions_db.h"

MYSQL* conn;

logger_t* logger;
db_config* config;

int query(logger_t* logger, const char* sql) {
    if(mysql_query(conn, sql)) {
        logger_printf(logger, "MYSQL %s", mysql_error(conn));
        return ERR_DB;
    }
    return 0;
}

void solutions_configure_db(db_config* conf) {
    config = conf;
}

int solutions_init_db(logger_t* in_logger) {
    assert(config != NULL);
    conn = mysql_init(NULL);
    logger = in_logger;
    if(!mysql_real_connect(conn, config->host, config->user, config->password, config->db_name, 0, NULL, 0)) {
        logger_printf(logger, "MYSQL %s", mysql_error(conn));
        return ERR_DB;
    }
    return 0;
}

int solutions_extract_new(solution_t** sln_arr_ptr, uint64* sln_arr_len) {
    if(sln_arr_len == NULL) {
        return ERR_ARGS;
    }

    if (query(logger, "select * from solutions where checking = 0")) {
        return ERR_DB;
    }
    MYSQL_ROW row;
    MYSQL_RES* result = mysql_store_result(conn);
    if(mysql_field_count(conn) != FIELDS_COUNT) {
        return ERR_SCHEMA;
    }

    *sln_arr_len = mysql_num_rows(result);
    if(*sln_arr_len == 0) {
        return 0;
    }

    *sln_arr_ptr = (solution_t*) calloc((*sln_arr_len), sizeof(solution_t));
    solution_t* cur_ptr = *sln_arr_ptr;
    uint64* rows_lengths;

    char upd_query[100];
    for(int i = 0; i < *sln_arr_len; i++) {
        row = mysql_fetch_row(result);
        cur_ptr = &((*sln_arr_ptr)[i]);
        rows_lengths = mysql_fetch_lengths(result);
        cur_ptr->id = atoi(row[0]);
        cur_ptr->user_id = atoi(row[1]);
        cur_ptr->task_id = atoi(row[2]);
        cur_ptr->time = NULL; // @todo converting db string to time
        cur_ptr->source_len = rows_lengths[4] + 1;
        cur_ptr->source = (char*) calloc(cur_ptr->source_len, sizeof(char));
        memcpy(cur_ptr->source, row[4], (size_t)cur_ptr->source_len - 1);
        cur_ptr->compiler_id = atoi(row[5]);
        cur_ptr->checking = atoi(row[6]);
        cur_ptr->accepted = atoi(row[7]);
        cur_ptr->response_len = rows_lengths[8];
        cur_ptr->response = (char*) malloc(cur_ptr->response_len * sizeof(char));
        memcpy(cur_ptr->response, row[8], cur_ptr->response_len);
    }

    mysql_free_result(result);
    if(query(logger, "start transaction")) {
        return ERR_DB;
    }
    for(uint64 i = 0; i < *sln_arr_len; i++) {
        snprintf(&upd_query[0], 100, "update solutions set checking = 1 where id = %u", (*sln_arr_ptr)[i].id);
        if(query(logger, &upd_query[0])) {
            query(logger, "rollback");
            return ERR_DB;
        }
    }
    if(query(logger, "commit")) {
        return ERR_DB;
    }

    return 0;
}


void solutions_close_db() {
    logger = NULL;
    config = NULL;
    mysql_close(conn);
}

void solution_free(solution_t* sln) {
	if(sln == NULL) {
		return;
	}
	free(sln->source);
	free(sln->response);
	free(sln);
}

int solution_post_result(uint32 sln_id, int8 accepted, char* response_text) {
    char sql[100];
    snprintf(&sql[0], 100, "update solutions set checking = 2, ac = %u, response = '%s' where id = %u", accepted, response_text, sln_id);
    return query(logger, &sql[0]);
}
