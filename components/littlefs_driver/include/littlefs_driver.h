#pragma once
#include <string.h>
#include "esp_log.h"

// 
#include "esp_log.h"
#include "esp_err.h"
// File system
#include "esp_system.h"
#include "esp_littlefs.h"
#include "mbedtls/md5.h"


#define LFS_MOUNT_POINT CONFIG_LFS_MOUNT_POINT
#define LFS_PARTITION_NAME CONFIG_LFS_PARTITION_NAME

extern float littlefs_total;
extern float littlefs_used;

void littlefs_driver(void);
esp_err_t fs_setup(void);

// Lated add read and write helpers
