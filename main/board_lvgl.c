/*
 * SPDX-License-Identifier: MIT
 *
 * BMGR -> LVGL port integration.
 *
 * The "lvgl" BMGR custom device describes mappings:
 *
 *   LVGL display #0 -> lcd device + optional touch device
 *   LVGL display #1 -> lcd device + optional touch device
 *   ...
 *
 * The physical LCD/touch devices themselves are initialized by BMGR.
 * This layer wraps them with the espressif/esp_lvgl_port managed component
 * (same one the factory Guition Demo_IDF BSP uses).
 */

#include "board_lvgl.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_board_manager.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "dev_display_lcd.h"
#include "dev_lcd_touch.h"
#include "gen_board_device_custom.h"

static const char *TAG = "board_lvgl";


#define BOARD_LVGL_ARRAY_SIZE(a) \
    (sizeof(a) / sizeof((a)[0]))


/*
 * Build the common lvgl_port_display_cfg_t shared across bus types.
 */
static lvgl_port_display_cfg_t board_lvgl_build_disp_cfg(
    const dev_display_lcd_handles_t *lcd,
    const dev_display_lcd_config_t *lcd_cfg,
    const dev_custom_lvgl_displays_t *policy)
{
    (void)policy;

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = lcd->io_handle,
        .panel_handle  = lcd->panel_handle,
        .control_handle = NULL,
        .buffer_size   = (uint32_t)lcd_cfg->lcd_width * (uint32_t)policy->buffer_height,
        .double_buffer = policy->double_buffer,
        .hres          = lcd_cfg->lcd_width,
        .vres          = lcd_cfg->lcd_height,
        .monochrome    = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .flags = {
            // Factory lvgl_demo_v9 main.c: .buff_dma=true, .buff_spiram=false,
            // .sw_rotate=false. use_psram in yaml lets user override for boards
            // where the draw buffer won't fit in internal SRAM.
            .buff_dma    = !policy->use_psram,
            .buff_spiram = policy->use_psram,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes  = false,
#endif
            .sw_rotate   = false,
        },
    };

    return disp_cfg;
}


/*
 * Register one BMGR LCD device with esp_lvgl_port.
 */
static lv_display_t *board_lvgl_register_display(
    const dev_display_lcd_handles_t *lcd,
    const dev_display_lcd_config_t *lcd_cfg,
    const dev_custom_lvgl_displays_t *policy)
{
    assert(lcd != NULL);
    assert(lcd_cfg != NULL);
    assert(policy != NULL);

    lvgl_port_display_cfg_t disp_cfg =
        board_lvgl_build_disp_cfg(lcd, lcd_cfg, policy);

    /*
     * SPI / I80 / PARLIO — generic path.
     */
    if (strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_SPI) == 0 ||
        strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_I80) == 0 ||
        strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_PARLIO) == 0) {

        return lvgl_port_add_disp(&disp_cfg);
    }

    /*
     * RGB parallel — needs the extra rgb cfg.
     */
    if (strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_RGB) == 0 ||
        strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_RGB_3WIRE_SPI) == 0) {

#if CONFIG_BOARD_LVGL_USE_RGB || CONFIG_BOARD_LVGL_USE_RGB_3WIRE_SPI
        const lvgl_port_display_rgb_cfg_t rgb_cfg = {
            .flags = {
                .bb_mode      = false,
                .avoid_tearing = false,
            },
        };
        return lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
#else
        ESP_LOGE(TAG,
                 "LCD '%s' is RGB but RGB support is disabled",
                 policy->lcd);
        return NULL;
#endif
    }

    /*
     * MIPI DSI — factory Guition path.
     */
    if (strcmp(lcd_cfg->sub_type,
               ESP_BOARD_DEVICE_LCD_SUB_TYPE_DSI) == 0) {

#if CONFIG_BOARD_LVGL_USE_DSI
        // Match Guition JC1060P470C_I_W_Y "New Panel" demo
        // (lvgl_demo_v9/main/main.c bsp_display_cfg_t): the app overrides the
        // BSP defaults with buff_dma=false, buff_spiram=false, sw_rotate=true.
        // These are the flags that produced the confirmed-working image.
        disp_cfg.flags.buff_dma    = false;
        disp_cfg.flags.buff_spiram = false;
        disp_cfg.flags.sw_rotate   = true;

        const lvgl_port_display_dsi_cfg_t dpi_cfg = {
            .flags = {
                .avoid_tearing = false,
            },
        };
        return lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
#else
        ESP_LOGE(TAG,
                 "LCD '%s' is MIPI DSI but "
                 "CONFIG_BOARD_LVGL_USE_DSI is disabled",
                 policy->lcd);
        return NULL;
#endif
    }

    ESP_LOGE(TAG,
             "LCD '%s' has unsupported subtype '%s'",
             policy->lcd,
             lcd_cfg->sub_type);
    return NULL;
}


/*
 * Register the optional touch device belonging to one LVGL display.
 */
static esp_err_t board_lvgl_register_touch(
    const dev_custom_lvgl_displays_t *policy,
    lv_display_t *display)
{
    assert(policy != NULL);
    assert(display != NULL);

    if (policy->touch == NULL || policy->touch[0] == '\0') {
        ESP_LOGI(TAG,
                 "LVGL display '%s' has no touch device",
                 policy->lcd);
        return ESP_OK;
    }

    void *touch_handle = NULL;

    esp_err_t ret = esp_board_manager_get_device_handle(
        policy->touch,
        &touch_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to get touch device '%s': %s",
                 policy->touch,
                 esp_err_to_name(ret));
        return ret;
    }

    if (touch_handle == NULL) {
        ESP_LOGE(TAG,
                 "Touch device '%s' returned NULL handle",
                 policy->touch);
        return ESP_ERR_INVALID_STATE;
    }

    dev_lcd_touch_handles_t *touch =
        (dev_lcd_touch_handles_t *)touch_handle;

    if (touch->touch_handle == NULL) {
        ESP_LOGE(TAG,
                 "Touch device '%s' has NULL touch_handle",
                 policy->touch);
        return ESP_ERR_INVALID_STATE;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = display,
        .handle = touch->touch_handle,
    };

    lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);

    if (indev == NULL) {
        ESP_LOGE(TAG,
                 "Failed to register touch '%s' for display '%s'",
                 policy->touch,
                 policy->lcd);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "Registered touch '%s' -> display '%s'",
             policy->touch,
             policy->lcd);

    return ESP_OK;
}


/*
 * Public entry point.
 *
 * BMGR must already have been initialized before this function is called.
 */
esp_err_t board_lvgl_init(void)
{
    dev_custom_lvgl_config_t *lvgl_cfg = NULL;

    esp_err_t ret = esp_board_manager_get_device_config(
        "lvgl",
        (void **)&lvgl_cfg);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to get LVGL policy configuration: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    if (lvgl_cfg == NULL) {
        ESP_LOGE(TAG, "LVGL policy configuration is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG,
             "Initializing LVGL port for %zu display(s)",
             BOARD_LVGL_ARRAY_SIZE(lvgl_cfg->displays));

    /*
     * 1. Init the LVGL port task exactly once.
     *
     * Values below mirror lvgl_demo_v9/main/main.c bsp_display_cfg_t.lvgl_port_cfg
     * from the Guition JC1060P470C_I_W_Y "New Panel" working demo. The stack
     * lives in INTERNAL memory so the LVGL task keeps working even when PSRAM
     * cache is being stalled by DPI refresh.
     */
    const lvgl_port_cfg_t port_cfg = {
        .task_priority     = 4,
        .task_stack        = 16384,
        .task_affinity     = -1,
        .task_max_sleep_ms = 500,
        .task_stack_caps   = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT,
        .timer_period_ms   = 5,
    };
    ret = lvgl_port_init(&port_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "lvgl_port_init failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    /*
     * 2. Register every LVGL display mapping.
     */
    for (size_t i = 0;
         i < BOARD_LVGL_ARRAY_SIZE(lvgl_cfg->displays);
         ++i) {

        const dev_custom_lvgl_displays_t *policy =
            &lvgl_cfg->displays[i];

        if (policy->lcd == NULL || policy->lcd[0] == '\0') {
            continue;
        }

        ESP_LOGI(TAG,
                 "Registering LVGL display #%zu: lcd='%s', touch='%s', "
                 "rotation=%d, double_buffer=%d, use_psram=%d, "
                 "buffer_height=%d",
                 i,
                 policy->lcd,
                 policy->touch ? policy->touch : "(none)",
                 policy->rotation,
                 policy->double_buffer,
                 policy->use_psram,
                 policy->buffer_height);

        void *lcd_handle = NULL;

        ret = esp_board_manager_get_device_handle(
            policy->lcd,
            &lcd_handle);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "Failed to get LCD device '%s': %s",
                     policy->lcd,
                     esp_err_to_name(ret));
            return ret;
        }

        if (lcd_handle == NULL) {
            ESP_LOGE(TAG,
                     "LCD device '%s' returned NULL handle",
                     policy->lcd);
            return ESP_ERR_INVALID_STATE;
        }

        dev_display_lcd_handles_t *lcd =
            (dev_display_lcd_handles_t *)lcd_handle;

        dev_display_lcd_config_t *lcd_cfg = NULL;

        ret = esp_board_manager_get_device_config(
            policy->lcd,
            (void **)&lcd_cfg);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "Failed to get LCD config '%s': %s",
                     policy->lcd,
                     esp_err_to_name(ret));
            return ret;
        }

        if (lcd_cfg == NULL) {
            ESP_LOGE(TAG,
                     "LCD config '%s' is NULL",
                     policy->lcd);
            return ESP_ERR_INVALID_STATE;
        }

        lv_display_t *display =
            board_lvgl_register_display(lcd, lcd_cfg, policy);

        if (display == NULL) {
            ESP_LOGE(TAG,
                     "Failed to register LVGL display #%zu "
                     "from LCD '%s'",
                     i,
                     policy->lcd);
            return ESP_FAIL;
        }

        ret = board_lvgl_register_touch(policy, display);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "Failed to register touch for LVGL display #%zu",
                     i);
            return ret;
        }

        ESP_LOGI(TAG,
                 "LVGL display #%zu registered successfully",
                 i);
    }

    ESP_LOGI(TAG, "LVGL port initialized successfully");
    return ESP_OK;
}
