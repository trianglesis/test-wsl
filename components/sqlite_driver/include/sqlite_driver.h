#pragma once
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_err.h"

#include "sqlite3.h"
#include "sqllib.h"

#include "cJSON.h"

#define DB_ROOT                     "/littlefs"
#define SQLITE_DEFAULT_PAGE_SIZE    512

typedef struct SQLArgs {
    int limit;
    int offset;
    int cols;
    char *json_str;
    bool save_file;
    SemaphoreHandle_t sql_done;
} sql_args_t;


esp_err_t setup_db(void);

void insert_task(void *pvParameters);
void ins_task(void *pvParameters);

void select_co2_stats(void *args);