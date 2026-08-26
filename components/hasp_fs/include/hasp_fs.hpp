#pragma once

#include "esp_err.h"
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

/** Mount point used by the rest of the firmware (POSIX paths). */
#define HASP_FS_BASE_PATH       "/littlefs"
#define HASP_FS_PARTITION_LABEL "storage"

/**
 * Mount LittleFS on the data partition and register it with VFS.
 * Safe to call once at boot. Idempotent if already mounted.
 *
 * @return ESP_OK on success
 */
esp_err_t hasp_fs_init(void);

/** true after a successful hasp_fs_init() */
bool hasp_fs_ready(void);

/** "/littlefs" */
const char* hasp_fs_mount_point(void);

/** Partition label used for mount / info / format */
const char* hasp_fs_partition_label(void);

/** Total and used bytes (0 if not mounted) */
esp_err_t hasp_fs_info(size_t* total, size_t* used);

/**
 * Unmount and unregister. Normally not needed (FS stays up for device lifetime).
 */
esp_err_t hasp_fs_deinit(void);

#ifdef __cplusplus
}
#endif