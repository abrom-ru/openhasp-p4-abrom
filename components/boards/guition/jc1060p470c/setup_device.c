/*
 * Guition JC1060P470C_I_W_Y device factory hooks for esp_board_manager.
 *
 * Provides the weak `lcd_dsi_panel_factory_entry_t` / `lcd_touch_factory_entry_t`
 * symbols that esp_board_manager expects for the JD9165 DSI panel and the GT911
 * capacitive touch controller.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_board_manager_includes.h"

#include "esp_lcd_panel_ops.h"

#if __has_include(<esp_lcd_jd9165.h>)
#define HAS_JD9165  1
#include "esp_lcd_jd9165.h"
#endif

#if __has_include(<esp_lcd_touch_gt911.h>)
#define HAS_GT911  1
#include "esp_lcd_touch_gt911.h"
#endif

static const char *TAG = "GUITION_JC1060P470C_SETUP_DEVICE";

#if defined(HAS_JD9165)
// Vendor init sequence for Guition JC1060P470C_I_W_Y "New Panel"
// (HKC 7.0" IPS 1024x600, JD9165 controller, 2-lane MIPI).
// Source: Guition working demo — copied verbatim from
// JC1060P470C_I_W_Y/1-Demo/Demo_IDF/ESP-IDF_5.5.4/JC1060P470C_I_W_Y_New_Panel/
// common_components/espressif__esp32_p4_function_ev_board/
// esp32_p4_function_ev_board.c (`lcd_cmd[]`, lines 404-466).
static const jd9165_lcd_init_cmd_t s_jd9165_guition_init_cmds[] = {
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0xF7, (uint8_t[]){0x49, 0x61, 0x02, 0x00}, 4, 0},
    {0x30, (uint8_t[]){0x01}, 1, 0},
    {0x04, (uint8_t[]){0x0C}, 1, 0},
    {0x05, (uint8_t[]){0x08}, 1, 0},
    {0x0B, (uint8_t[]){0x11}, 1, 0},
    {0x20, (uint8_t[]){0x04}, 1, 0},
    {0x1F, (uint8_t[]){0x05}, 1, 0},
    {0x23, (uint8_t[]){0x38}, 1, 0},
    {0x28, (uint8_t[]){0x18}, 1, 0},
    {0x29, (uint8_t[]){0x29}, 1, 0},
    {0x2A, (uint8_t[]){0x01}, 1, 0},
    {0x2B, (uint8_t[]){0x29}, 1, 0},
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x30, (uint8_t[]){0x02}, 1, 0},
    {0x00, (uint8_t[]){0x05}, 1, 0},
    {0x01, (uint8_t[]){0x22}, 1, 0},
    {0x02, (uint8_t[]){0x08}, 1, 0},
    {0x03, (uint8_t[]){0x12}, 1, 0},
    {0x04, (uint8_t[]){0x16}, 1, 0},
    {0x05, (uint8_t[]){0x64}, 1, 0},
    {0x06, (uint8_t[]){0x00}, 1, 0},
    {0x07, (uint8_t[]){0x00}, 1, 0},
    {0x08, (uint8_t[]){0x78}, 1, 0},
    {0x09, (uint8_t[]){0x00}, 1, 0},
    {0x0A, (uint8_t[]){0x04}, 1, 0},
    {0x0B, (uint8_t[]){0x16, 0x17, 0x0B, 0x0D, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x07, 0x09}, 11, 0},
    {0x0C, (uint8_t[]){0x09, 0x1E, 0x1E, 0x1C, 0x1C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0D, (uint8_t[]){0x0A, 0x05, 0x0B, 0x0D, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x06, 0x08}, 11, 0},
    {0x0E, (uint8_t[]){0x08, 0x1F, 0x1F, 0x1D, 0x1D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0F, (uint8_t[]){0x0A, 0x05, 0x0D, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x1D, 0x1D, 0x1F}, 11, 0},
    {0x10, (uint8_t[]){0x1F, 0x08, 0x08, 0x06, 0x06, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x11, (uint8_t[]){0x16, 0x17, 0x0D, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x1C, 0x1C, 0x1E}, 11, 0},
    {0x12, (uint8_t[]){0x1E, 0x09, 0x09, 0x07, 0x07, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x13, (uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4, 0},
    {0x14, (uint8_t[]){0x00, 0x00, 0x41, 0x41}, 4, 0},
    {0x15, (uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x18, (uint8_t[]){0x85}, 1, 0},
    {0x19, (uint8_t[]){0x06, 0x09}, 2, 0},
    {0x1A, (uint8_t[]){0x05, 0x08}, 2, 0},
    {0x1B, (uint8_t[]){0x0A, 0x04}, 2, 0},
    {0x26, (uint8_t[]){0x00}, 1, 0},
    {0x27, (uint8_t[]){0x00}, 1, 0},
    {0x30, (uint8_t[]){0x06}, 1, 0},
    {0x12, (uint8_t[]){0x3F, 0x25, 0x27, 0x35, 0x1D, 0x1B, 0x1B, 0x1A, 0x18, 0x0A, 0x2A, 0x21, 0x19, 0x30}, 14, 0},
    {0x13, (uint8_t[]){0x3F, 0x26, 0x27, 0x35, 0x1E, 0x1C, 0x1C, 0x1A, 0x18, 0x0B, 0x2A, 0x21, 0x19, 0x30}, 14, 0},
    {0x30, (uint8_t[]){0x0A}, 1, 0},
    {0x02, (uint8_t[]){0x4F}, 1, 0},
    {0x0B, (uint8_t[]){0x40}, 1, 0},
    {0x30, (uint8_t[]){0x0D}, 1, 0},
    {0x0D, (uint8_t[]){0x04}, 1, 0},
    {0x10, (uint8_t[]){0x05}, 1, 0},
    {0x11, (uint8_t[]){0x0C}, 1, 0},
    {0x12, (uint8_t[]){0x05}, 1, 0},
    {0x13, (uint8_t[]){0x0C}, 1, 0},
    {0x30, (uint8_t[]){0x00}, 1, 0},

    {0x3A, (uint8_t[]){0x55}, 1, 0},   // RGB565
    {0x11, (uint8_t[]){0x00}, 1, 120}, // SLEEP_OUT + 120ms
    {0x29, (uint8_t[]){0x00}, 1, 20},  // DISPLAY_ON + 20ms
};

__attribute__((weak)) esp_err_t lcd_dsi_panel_factory_entry_t(esp_lcd_dsi_bus_handle_t dsi_handle,
                                                              dev_display_lcd_config_t *lcd_cfg,
                                                              dev_display_lcd_handles_t *lcd_handles)
{
    jd9165_vendor_config_t vendor_config = {
        .init_cmds = s_jd9165_guition_init_cmds,
        .init_cmds_size = sizeof(s_jd9165_guition_init_cmds) / sizeof(s_jd9165_guition_init_cmds[0]),
        .mipi_config = {
            .dsi_bus = dsi_handle,
            .dpi_config = &lcd_cfg->sub_cfg.dsi.dpi_config,
        },
    };

    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .reset_gpio_num = lcd_cfg->sub_cfg.dsi.reset_gpio_num,
        .rgb_ele_order = lcd_cfg->rgb_ele_order,
        .bits_per_pixel = lcd_cfg->bits_per_pixel,
        .data_endian = lcd_cfg->data_endian,
        .flags = {
            .reset_active_high = lcd_cfg->sub_cfg.dsi.reset_active_high,
        },
        .vendor_config = &vendor_config,
    };

    esp_err_t ret = esp_lcd_new_panel_jd9165(lcd_handles->io_handle, &lcd_dev_config, &lcd_handles->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create jd9165 panel: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    // Do NOT call esp_lcd_panel_reset() / esp_lcd_panel_init() here.
    // The BMGR wrapper `dev_display_lcd_init` (dev_display_lcd.c:125-140)
    // runs both of them right after this factory entry returns. Calling
    // panel_init a second time re-enters the JD9165 driver's init routine,
    // which begins with esp_lcd_panel_io_rx_param(0x04, ID, 3). On the
    // second run the panel does not ACK that read, the DBI RX FIFO stays
    // empty, and the CPU spins in mipi_dsi_host_ll_gen_is_read_fifo_empty
    // until the WDT fires.
    // The 46-command vendor init sequence is attached via
    // jd9165_vendor_config_t.init_cmds above and gets sent by the driver's
    // panel_init(), which BMGR calls exactly once.
    ESP_LOGI(TAG, "JD9165 panel created (reset + init deferred to BMGR wrapper)");
    return ESP_OK;
}
#endif  /* defined(HAS_JD9165) */

#if defined(HAS_GT911)
__attribute__((weak)) esp_err_t lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                                          const esp_lcd_touch_config_t *touch_dev_config,
                                                          esp_lcd_touch_handle_t *ret_touch)
{
    esp_err_t ret = esp_lcd_touch_new_i2c_gt911(io, touch_dev_config, ret_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create gt911 touch driver: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}
#endif  /* defined(HAS_GT911) */
