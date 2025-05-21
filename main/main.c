/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_err.h"

#include "esp_littlefs.h"
#include "sqlite3.h"
#include "sqllib.h"

#include "littlefs_driver.h"
#include "sqlite_driver.h"

static SemaphoreHandle_t sql_done;

static const char *TAG = "Test-WSL-EMUL";


void debug_chip_info(void) {
    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");
    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }
    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());
}

void app_main(void) {
    ESP_LOGI(TAG, "TEST WSL!!!!!");
    debug_chip_info();
    ESP_ERROR_CHECK(fs_setup());
    // Skip tables check in setup
    ESP_ERROR_CHECK(setup_db());

    // DEBUG and TEST:
    // Select CO2 values once
    sql_done = xSemaphoreCreateBinary();

    int offset = 0;
    sql_args_t* sql_args = (sql_args_t*) calloc(1, sizeof(sql_args_t));
    for (size_t i = 0; i < 3; i++) {
        sql_args->limit = 100;
        sql_args->offset = offset;
        sql_args->cols = 3;
        sql_args->save_file = false;
        sql_args->sql_done = sql_done;

        xTaskCreate(select_co2_stats, "SQL-Select", 1024*6, sql_args, 5, NULL);
        xSemaphoreTake(sql_args->sql_done, portMAX_DELAY); //Wait for completion in task

        ESP_LOGI(TAG, "3 JSON:\n%s\n", sql_args->json_str);
        offset += 100;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "Finished DB Querying and converting to JSON, now clean and free!");
    free(sql_args);
    vSemaphoreDelete(sql_done);
}
