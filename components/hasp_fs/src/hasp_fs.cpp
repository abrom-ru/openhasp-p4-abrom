#include "hasp_fs.hpp"

#include "esp_log.h"
#include "esp_littlefs.h"

static const char* TAG = "HASP_FS";

static bool s_ready = false;

esp_err_t hasp_fs_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path              = HASP_FS_BASE_PATH;
    conf.partition_label        = HASP_FS_PARTITION_LABEL;
    conf.format_if_mount_failed = true;   // first boot / corrupt → format
    conf.dont_mount             = false;
    conf.read_only              = false;
#if defined(ESP_LITTLEFS_VERSION) || 1
    // Present in joltwallet/esp_littlefs recent versions
    conf.grow_on_mount          = true;   // expand image if partition is larger
#endif

    ESP_LOGI(TAG, "Mounting LittleFS on '%s' at '%s'",
             conf.partition_label, conf.base_path);

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Mount/format failed");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Partition '%s' not found – check partition table",
                     conf.partition_label);
        } else {
            ESP_LOGE(TAG, "esp_vfs_littlefs_register: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_littlefs_info failed: %s", esp_err_to_name(ret));
        // Still mounted; treat as success
    } else {
        ESP_LOGI(TAG, "LittleFS ready – total: %u, used: %u",
                 (unsigned)total, (unsigned)used);
    }

    s_ready = true;
    return ESP_OK;
}

bool hasp_fs_ready(void)
{
    return s_ready;
}

const char* hasp_fs_mount_point(void)
{
    return HASP_FS_BASE_PATH;
}

const char* hasp_fs_partition_label(void)
{
    return HASP_FS_PARTITION_LABEL;
}

esp_err_t hasp_fs_info(size_t* total, size_t* used)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_littlefs_info(HASP_FS_PARTITION_LABEL, total, used);
}

esp_err_t hasp_fs_deinit(void)
{
    if (!s_ready) {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_littlefs_unregister(HASP_FS_PARTITION_LABEL);
    if (ret == ESP_OK) {
        s_ready = false;
        ESP_LOGI(TAG, "LittleFS unmounted");
    }
    return ret;
}