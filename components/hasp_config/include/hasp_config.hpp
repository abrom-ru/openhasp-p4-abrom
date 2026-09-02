#pragma once

#include "esp_err.h"

class ServiceManager;

// Step 6 (S3-mirror of hasp_config.cpp::configSetup / configWrite).
//
// Load /littlefs/config.json and hand the document to mgr.set_config().
// Each HaspService::set_config writes changed keys into its own NVS
// namespace, so after this call the runtime + NVS are both populated.
// path == nullptr uses hasp_fs_mount_point() + "/config.json".
//
// Returns:
//   ESP_OK              — file loaded and applied
//   ESP_ERR_NOT_FOUND   — file missing; caller may fall back to NVS-only
//   ESP_ERR_INVALID_ARG — JSON parse failure
//   ESP_ERR_INVALID_SIZE— file empty or larger than the 8 KiB budget
esp_err_t hasp_config_load(ServiceManager& mgr, const char* path = nullptr);

// Serialize mgr.get_config() to /littlefs/config.json. Passwords are
// masked ("********" — S3 D_PASSWORD_MASK) by each service's get_config,
// matching S3 configWrite → configOutput semantics — the on-flash file
// is safe to leak and cannot be used to re-derive secrets. Real secrets
// stay in NVS; on reboot set_config skips "********" and load_from_nvs restores
// them.
esp_err_t hasp_config_save(ServiceManager& mgr, const char* path = nullptr);
