#include <stdio.h>
#include "sqlite_driver.h"

static const char *TAG = "sqlite";

MessageBufferHandle_t xMessageBufferQuery;
static SemaphoreHandle_t sql_done;

/*
Battery
*/
#define SQL(...) #__VA_ARGS__
const char *battery_table_create = SQL(
    CREATE TABLE "battery_stats" (
        "adc_raw"               INTEGER,
        "voltage"               INTEGER,
        "voltage_m"             INTEGER,
        "percentage"            INTEGER,
        "max_masured_voltage"   INTEGER,
        "measure_freq"          INTEGER,
        "measure_loop_count"    INTEGER
    );
);

/*
CO2 Sensor
*/
#define SQL(...) #__VA_ARGS__
const char *co2_table_create = SQL(
    CREATE TABLE "co2_stats" (
        "temperature"    INTEGER,
        "humidity"       INTEGER,
        "co2_ppm"        INTEGER,
        "measure_freq"   INTEGER
    );
);

/*
BME680 Sensor
    float temperature;
    float humidity;
    float pressure;
    float resistance;
    uint16_t air_q_index;
    int measure_freq;
*/
#define SQL(...) #__VA_ARGS__
const char *bme680_table_create = SQL(
    CREATE TABLE "air_temp_stats" (
        "temperature"    INTEGER,
        "humidity"       INTEGER,
        "pressure"       INTEGER,
        "resistance"     INTEGER,
        "air_q_index"    INTEGER,
        "measure_freq"   INTEGER
    );
);

void sqlite_info(void) {
    printf("\n\n- Init:\t\tSQLite Driver debug info!\n");
    ESP_LOGI(TAG, "DB_ROOT: %s", DB_ROOT);
}

static int callback(void *data, int argc, char **argv, char **azColName) {
    MessageBufferHandle_t *xMessageBuffer = (MessageBufferHandle_t *)data;
    ESP_LOGD(TAG, "data=[%p] xMessageBuffer=[%p]", data, xMessageBuffer);
    int i;
    char tx_buffer[128];
    for (i = 0; i<argc; i++){
        // printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
        int tx_length = sprintf(tx_buffer, "%s = %s", azColName[i], argv[i] ? argv[i] : "NULL");
        if (xMessageBuffer) {
            size_t sended = xMessageBufferSendFromISR((MessageBufferHandle_t)xMessageBuffer, tx_buffer, tx_length, NULL);
            ESP_LOGD(TAG, "sended=%d tx_length=%d", sended, tx_length);
            if (sended != tx_length) {
                ESP_LOGE(TAG, "xMessageBufferSendFromISR fail tx_length=%d sended=%d", tx_length, sended);
            }
        } else {
            ESP_LOGE(TAG, "xMessageBuffer is NULL");
        }
    }
    //printf("\n");
    return 0;
}

int db_query(MessageBufferHandle_t xMessageBuffer, sqlite3 *db, const char *sql) {
	ESP_LOGD(TAG, "xMessageBuffer=[%p]", xMessageBuffer);
	char *zErrMsg = 0;
	printf("%s\n", sql);
	int rc = sqlite3_exec(db, sql, callback, xMessageBuffer, &zErrMsg);
	if (rc != SQLITE_OK) {
		printf("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
		printf("Operation done successfully\n");
	}
	return rc;
}

/*
Use int cols - to indicate an actual amount of table columns (COL) selected.
    Other counters should increment or reset at each COL and ROW

Each ROW have N COLs in it. 
After iterating N cols:
     - update ROW counter by 1
     - reset COLs counter to 0
*/
void parse_sql_response_to_json(void *sql_args_handle) {
    sql_args_t* sql_args = (sql_args_t*) sql_args_handle;
    
    // Create JSON
    cJSON *root = NULL;
    cJSON *object[128];
    int colIndex = 0;  // Increment for each new column value
    int rowIndex = 0;  // Increment for each new row. After N of cols
    char itemName[128];
	char itemValue[128];
    
	uint32_t startHeap = 0;
	uint32_t endHeap = 0;
    startHeap = esp_get_free_heap_size();
    
    char sqlmsg[256];
    size_t readBytes;
    // Init JSON structure
    root = cJSON_CreateArray();
    rowIndex = 0;  // Set the first ROW
    int count = 0;  // Simple counter, for each col:value pairs
    // Read selected
    while (1) {
        readBytes = xMessageBufferReceive(xMessageBufferQuery, sqlmsg, sizeof(sqlmsg), 100);
        if (readBytes == 0) break;  // Exit when EOF
        sqlmsg[readBytes] = 0;
        count++;  // Natural order 1=1, 3=3

        // Each line parse as key:value pairs
        char *pos = strstr(sqlmsg, " = ");
        if (pos == NULL) continue;
        ESP_LOGD(TAG, "%d pos=[%p] sqlmsg=[%p] length=[%d] pos+3=[%s]", count, pos, sqlmsg, pos - sqlmsg, pos+3);
        memset(itemName, 0, sizeof(itemName));
        strncpy(itemName, sqlmsg, (int)(pos - sqlmsg));
        memset(itemValue, 0, sizeof(itemValue));
        strcpy(itemValue, pos+3);

        /*
            If current element index is not divisable by number or columns selected
             - assume this is just a single ROW from database
            When element index IS divisible by NUMBER of columns selected 
            - assume this is the END of a single ROW from database
            - increment ROW Index by 1
        */

        ESP_LOGD(TAG, "%d ROW: %d, COL: %d\n", count, rowIndex, colIndex);
        // When this is the first COL in current ROW: create an object
        if (colIndex == 0) {
            object[rowIndex] = cJSON_CreateObject();  // New row for each 0 COL
        }

        // Add each key:value pair into the object
        ESP_LOGD(TAG, "%d \tROW: %d values: %s:%s\n", count, rowIndex, itemName, itemValue);
        cJSON_AddStringToObject(object[rowIndex], itemName, itemValue); // New k:v pair into the row
        colIndex++; // Increment when single COL parsed

        // When overall count is divisible by cols selected - this is one ROW's end
        if (count % sql_args->cols == 0) {
            // Add one object into the common root
            cJSON_AddItemToArray(root, object[rowIndex]);
            // One row = one JSON object
            ESP_LOGD(TAG, "%d \t\tEnd of ROW: %d\n", count, rowIndex);
            rowIndex++;  // Increment ROW when finished with current one
            colIndex = 0; // Reset column index to start parsing new set of COLs
        }
    } // WHILE

    // Now JSON and FILE
    sql_args->json_str = cJSON_PrintUnformatted(root);

    if (sql_args->save_file) {
        FILE* f = NULL;
        char JSONFile[64];
        sprintf(JSONFile, "%s/local.json", DB_ROOT);
        f = fopen(JSONFile, "w");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open local file");
        }
        fprintf(f, "%s", sql_args->json_str);
        if (f != NULL) fclose(f);
    }

    // Free this as soon as we send it
    // printf("1 JSON\n%s\n", sql_args->json_str);
    // cJSON_free(json_str);  // Should probably be freed by free(sql_args)
    cJSON_Delete(root);  // Delete JSON objects
    root = NULL;

    endHeap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "startHeap=%"PRIi32" endHeap=%"PRIi32, startHeap, endHeap);
    xSemaphoreGive( sql_args->sql_done );  // Release in task after finishing the job
}

/*
Select limited amout of records from the table.
Use SQLArgs to set LIMIT and OFFSET for SQL Query

*/
void select_co2_stats(void *sql_args_handle) {
    sql_args_t* sql_args = (sql_args_t*) sql_args_handle;
    ESP_LOGI(TAG, "SQL SELECT: Columns: %d Limit %d Offset %d", sql_args->cols, sql_args->limit, sql_args->offset);
    
    char db_name[32];
    snprintf(db_name, sizeof(db_name)-1, "%s/stats.db", DB_ROOT);
    sqlite3 *db;
    sqlite3_initialize();
    int rc = db_open(db_name, &db); // will print "Opened database successfully"
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "DB SELECT Cannot open database");
    }

    // SELECT
    char table_sql[128];
    snprintf(table_sql, sizeof(table_sql) + 1, "SELECT ROWID, co2_ppm, measure_freq FROM co2_stats ORDER BY rowid DESC LIMIT %d OFFSET %d;", sql_args->limit, sql_args->offset);
    rc = db_query(xMessageBufferQuery, db, table_sql);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "DB SELECT, failed: \n%s\n", table_sql);
    }

    // Create JSON
    parse_sql_response_to_json(sql_args);
    sqlite3_close(db);
    vTaskDelete(NULL);
}

/*
Create multiple tabled in one go.
Already initialized.
Open - create tables - close
Iterate over array of tables: 0 is always the test table.

Optimizations and lower footprint hints:
- https://www.sqlite.org/withoutrowid.html
- https://www.sqlite.org/pragma.html#pragma_page_size

*/
void table_check_tsk(void *arg) {
    // Tables to create
    char tables[][16] = { 
        "test_table", 
        "battery_stats", 
        "air_temp_stats",
        "co2_stats"
    };
    
    char db_name[32];
    snprintf(db_name, sizeof(db_name)-1, "%s/stats.db", DB_ROOT);
    // Open database
    sqlite3 *db;
    int rc = db_open(db_name, &db); // will print "Opened database successfully"
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Cannot open database: %s resp: %d", db_name, rc);
        vTaskDelete(NULL);
    } else {
        ESP_LOGI(TAG, "Opened database: %s resp: %d", db_name, rc);
    }

    // Set page size, read page size: "PRAGMA page_size;"
    // Inquiry
    rc = db_query(xMessageBufferQuery, db, "PRAGMA page_size=512; VACUUM;");
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Set PRAGMA page_size FAILED!");
    }

    for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        ESP_LOGI(TAG, "%d Check table existence:\n\tDB\t%s\n\tTable\t%s", i, db_name, tables[i]);

        char create_table_sql[256];
        if (i == 0) {
            snprintf(create_table_sql, sizeof(create_table_sql)-1, "%s", "CREATE TABLE test (id INTEGER, content);");
        } else if (i == 1) {
            snprintf(create_table_sql, sizeof(create_table_sql)-1, "%s", battery_table_create);
        } else if (i == 2) {
            snprintf(create_table_sql, sizeof(create_table_sql)-1, "%s", bme680_table_create);
        } else if (i == 3) {
            snprintf(create_table_sql, sizeof(create_table_sql)-1, "%s", co2_table_create);
        } else {
            ESP_LOGW(TAG, "No such table to create: %s", tables[i]);
        }

        // Inquiry
        char table_name_sql[96];
        snprintf(table_name_sql, sizeof(table_name_sql)-1, "select count(*) from sqlite_master where name = '%s';", tables[i]);
        rc = db_query(xMessageBufferQuery, db, table_name_sql);
        if (rc != SQLITE_OK) {
            ESP_LOGE(TAG, "SELECT count from 'sqlite_master' FAILED!\n\tTable\t%s", tables[i]);
            continue;
        }

        // Read reply
        char sqlmsg[256];
        size_t readBytes;
        readBytes = xMessageBufferReceive(xMessageBufferQuery, sqlmsg, sizeof(sqlmsg), 100);
        ESP_LOGI(TAG, "%d readBytes=%d", i, readBytes);
        if (readBytes == 0) {
            ESP_LOGE(TAG, "SELECT query is EMPTY!\n\tTable\t%s", tables[i]);
            continue;
        }
        sqlmsg[readBytes] = 0;
        ESP_LOGI(TAG, "%d sqlmsg=[%s]", i, sqlmsg);

        // Create table
        if (strcmp(sqlmsg, "count(*) = 0") == 0) {
            int rc = db_query(xMessageBufferQuery, db, create_table_sql);
            if (rc != SQLITE_OK) {
                ESP_LOGE(TAG, "%d Table cannot be created: FAIL!\n\tTable\t%s", i, tables[i]);
                continue;
            } else {
                ESP_LOGI(TAG, "%d Table created: OK!\n\tTable\t%s", i, tables[i]);
            }
        } else {
            ESP_LOGI(TAG, "%d Table already exists, OK!\n\tTable\t%s", i, tables[i]);
        }

        // Inquiry
        char select_count_sql[96];
        snprintf(select_count_sql, sizeof(select_count_sql)-1, "select count(*) from %s;", tables[i]);
        rc = db_query(xMessageBufferQuery, db, select_count_sql);
        if (rc != SQLITE_OK) {
            ESP_LOGE(TAG, "%d Select from the table FAILED!\n\tTable\t%s", i, tables[i]);
            continue;
        }

        // Read reply
        readBytes = xMessageBufferReceive(xMessageBufferQuery, sqlmsg, sizeof(sqlmsg), 100);
        ESP_LOGI(TAG, "%d readBytes=%d", i, readBytes);
        if (readBytes == 0) {
            ESP_LOGE(TAG, "%d Select from the table EMPTY response: FAILED!\n\tTable\t%s", i, tables[i]);
            vTaskDelete(NULL);
        }
        sqlmsg[readBytes] = 0;
        ESP_LOGI(TAG, "%d sqlmsg=[%s]", i, sqlmsg);

        int record_count = 0;
        if (strncmp(sqlmsg, "count(*) =", 10) == 0) {
            record_count = atoi(&sqlmsg[10]);
        } else {
            ESP_LOGE(TAG, "%d illegal reply\n\tTable\t%s", i, tables[i]);
        }

        if (record_count == 0) {
            ESP_LOGI(TAG, "%d Table is empty record_count=%d\n\tTable\t%s", i, record_count, tables[i]);
        } else if (record_count >= 3000) {
            // Add routine to delete older records after a few thousands collected
            ESP_LOGW(TAG, "%d Table is HEAVY, clean old recods! record_count=%d\n\tTable\t%s", i, record_count, tables[i]);
        } else {
            ESP_LOGI(TAG, "%d Table is not empty but ok record_count=%d\n\tTable\t%s", i, record_count, tables[i]);
        }

    } // FOR
    
    // Close and clean
    sqlite3_close(db);
    xSemaphoreGive( sql_done );  // Release in task after finishing the job
    vTaskDelete(NULL);
}

void insert_task(void *pvParameters) {
    char *sql = (char *)pvParameters;
    // Open database
    char db_name[32];
    snprintf(db_name, sizeof(db_name)-1, "%s/stats.db", DB_ROOT);
    sqlite3 *db;
    sqlite3_initialize();
    // vTaskDelay(pdMS_TO_TICKS(500));
    int rc = db_open(db_name, &db); // will print "Opened database successfully"
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Cannot open database: %s, resp: %d", db_name, rc);
        vTaskDelete(NULL);
    }
    // Insert record
    rc = db_query(xMessageBufferQuery, db, sql);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Cannot insert at %s\n%s\n", db_name, sql);
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "SQL routine ended, DB is closed: %s", db_name);
    sqlite3_close(db);
    vTaskDelete(NULL);
}

void ins_task(void *pvParameters) {
    char *sql = (char *)pvParameters;
    // Open database
    char db_name[32];
    snprintf(db_name, sizeof(db_name)-1, "%s/stats.db", DB_ROOT);
    sqlite3 *db;
    sqlite3_initialize();

    int rc = db_open(db_name, &db); // will print "Opened database successfully"
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "DB INSERT Cannot open database");
        vTaskDelete(NULL);
    }
    
	rc = db_exec(db, sql);
	if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "DB INSERT, cannot insert: \n%s\n", sql);
		vTaskDelete(NULL);
	}

    sqlite3_close(db);
    ESP_LOGI(TAG, "DB INSERT, DB is closed");
	vTaskDelete(NULL);
}

void check_create(void) {
    sql_done = xSemaphoreCreateBinary();
    TaskHandle_t xHandle;
    xTaskCreatePinnedToCore(table_check_tsk, "check-tables", 1024*6, NULL, 5, &xHandle, tskNO_AFFINITY);

    //Wait for completion in task
    xSemaphoreTake(sql_done, portMAX_DELAY);
    // Cleanup
    sqlite3_shutdown();  // close
    vSemaphoreDelete(sql_done);
}

esp_err_t setup_db(void) {
    sqlite_info();
    // Compose DB name and pointer
    char db_name[32];
    snprintf(db_name, sizeof(db_name)-1, "%s/stats.db", DB_ROOT);
    
    // DELETE previous table for now, at each startup.
    // unlink(db_name);
    sqlite3_initialize();  // Do not init again in task!
    
    // Create Message Buffer
	xMessageBufferQuery = xMessageBufferCreate(4096);
	configASSERT( xMessageBufferQuery );
    if( xMessageBufferQuery == NULL ) {
        ESP_LOGE(TAG, "Cannot create a message buffer for SQL operations!");
    }

    // Skip check
    // check_create();

    return ESP_OK;
}