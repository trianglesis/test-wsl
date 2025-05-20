#include <stdio.h>
#include "littlefs_driver.h"

static const char *TAG = "littlefs";

float littlefs_total = 0;
float littlefs_used = 0;

const char *file_index_html = LFS_MOUNT_POINT"/index.html";
const char *lfs_file_hello = LFS_MOUNT_POINT"/hello.txt";

void littlefs_driver(void) {
    printf("\n\n- Init:\t\tLittleFS (spi flash) driver debug info!\n");
    ESP_LOGI(TAG, "LFS_MOUNT_POINT: %s", LFS_MOUNT_POINT);
    ESP_LOGI(TAG, "LFS_PARTITION_NAME: %s", LFS_PARTITION_NAME);
}

/*
Todo add functions for read and write here
*/

// Based on two different examples
esp_err_t little_fs_file_sum_test(void) {
    ESP_LOGI(TAG, "Computing hello.txt MD5 hash and test reading");
    FILE* f = fopen(lfs_file_hello, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open hello.txt");
    }
    // Read file and compute the digest chunk by chunk
    #define MD5_MAX_LEN 16

    char buf[64];
    mbedtls_md5_context ctx;
    unsigned char digest[MD5_MAX_LEN];

    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);

    size_t read;

    do {
        read = fread((void*) buf, 1, sizeof(buf), f);
        mbedtls_md5_update(&ctx, (unsigned const char*) buf, read);
    } while(read == sizeof(buf));

    mbedtls_md5_finish(&ctx, digest);

    // Create a string of the digest
    char digest_str[MD5_MAX_LEN * 2];

    for (int i = 0; i < MD5_MAX_LEN; i++) {
        sprintf(&digest_str[i * 2], "%02x", (unsigned int)digest[i]);
    }

    // For reference, MD5 should be d25b9ac261c79341e71548ddc7101d24
    ESP_LOGI(TAG, "Computed MD5 hash of hello.txt: %s", digest_str);

    fclose(f);
    return ESP_OK;
}

// Just test
esp_err_t littlefs_read_test(void) {
    ESP_LOGI(TAG, "Reading from littlefs: hello.txt");
    // Open for reading hello.txt
    FILE* f = fopen(lfs_file_hello, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open hello.txt");
    }
    char buf[64];
    memset(buf, 0, sizeof(buf));
    fread(buf, 1, sizeof(buf), f);
    fclose(f);
    // Display the read contents from the file
    ESP_LOGI(TAG, "Read from hello.txt: %s", buf);
    return ESP_OK;
}

esp_err_t littlefs_write_test(void) {
    ESP_LOGI(TAG, "Opening file %s", lfs_file_hello);
    FILE *f = fopen(lfs_file_hello, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    fprintf(f, "Test file for SD card init");
    fclose(f);
    ESP_LOGI(TAG, "File written");

    return ESP_OK;
}

void little_fs_space(void) {
    esp_err_t ret;

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(LFS_PARTITION_NAME, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    }
    else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    littlefs_total = (total / 1024); // Convert to Kb
    littlefs_used = (used / 1024); // Convert to Kb
    // https://cplusplus.com/reference/cstdio/printf/
    ESP_LOGI(TAG, "Partition size: %.2f/%.2f KB", littlefs_total, littlefs_used);
}

esp_err_t fs_setup(void) {

    littlefs_driver();
    // 
    esp_vfs_littlefs_conf_t conf = {
        .base_path = LFS_MOUNT_POINT,
        .partition_label = LFS_PARTITION_NAME,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    // Use settings defined above to initialize and mount LittleFS filesystem.
    // Note: esp_vfs_littlefs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        }
        else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }
    
    // Space total and used
    little_fs_space();
    // Test, 1,2,3
    littlefs_write_test();      // 1
    littlefs_read_test();       // 2
    little_fs_file_sum_test();  // 3 

    return ESP_OK;

}