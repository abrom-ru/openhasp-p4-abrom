#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Runtime log routing destination bitmask channels. */
typedef enum {
    HASP_LOG_DEST_NONE    = 0,
    HASP_LOG_DEST_UART0   = (1 << 0),
    HASP_LOG_DEST_USB     = (1 << 1),
    HASP_LOG_DEST_WS      = (1 << 2),
    HASP_LOG_DEST_SYSLOG  = (1 << 3),
} hasp_log_dest_t;

#define HASP_LOG_DEST_SERIAL    (hasp_log_dest_t)(HASP_LOG_DEST_UART0 | HASP_LOG_DEST_USB)
#define HASP_LOG_DEST_ALL       (hasp_log_dest_t)(HASP_LOG_DEST_UART0 | HASP_LOG_DEST_USB | HASP_LOG_DEST_WS)

/**
 * Initialize UART0 and Native USB Serial drivers, then hook the log router.
 * Safe to call once at boot. Idempotent if already active.
 * 
 * @return ESP_OK on success, or driver installation error codes.
 */
esp_err_t hasp_log_init(void);

/** true after a successful hasp_log_init() */
bool hasp_log_ready(void);

/** Get the currently active output channel bitmask */
hasp_log_dest_t hasp_log_get_destination(void);

/** 
 * Change where standard ESP_LOGx output routes at runtime.
 * This can be updated instantly from any thread context.
 */
void hasp_log_set_destination(hasp_log_dest_t dest);

/**
 * Unhook the custom log router and uninstall driver channels.
 * Normally not needed (Logging stays up for device lifetime).
 */
esp_err_t hasp_log_deinit(void);

void hasp_log_printf_with_tag(esp_log_level_t level, const char* tag, const char* fmt, ...);

#ifdef __cplusplus
}
#endif
