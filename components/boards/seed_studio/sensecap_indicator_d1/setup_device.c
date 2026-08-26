#include "esp_err.h"
#include "esp_check.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"

#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"

#include "esp_io_expander.h"
#include "esp_io_expander_pca9535.h"

#include "sensecap_d1_st7701.h"

#define SENSECAP_D1_LCD_RST_EXPANDER_PIN  5
#define SENSECAP_D1_TOUCH_RST_EXPANDER_PIN 7
#define SENSECAP_D1_LCD_CS_EXPANDER_PIN   4

esp_err_t lcd_panel_factory_entry_t(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(
        io != NULL &&
        panel_dev_config != NULL &&
        ret_panel != NULL,
        ESP_ERR_INVALID_ARG,
        "sensecap_d1",
        "invalid LCD factory arguments"
    );

    /*
     * BMGR supplies the RGB configuration through vendor_config for
     * rgb_3wire_spi when lcd_panel_config.vendor_config is NULL.
     *
     * Do not mutate the caller's panel_dev_config.
     */
    esp_lcd_panel_dev_config_t config = *panel_dev_config;

    st7701_vendor_config_t vendor_config = {
        .rgb_config =
            (esp_lcd_rgb_panel_config_t *)panel_dev_config->vendor_config,

        .init_cmds = sensecap_d1_init_cmds,
        .init_cmds_size = sensecap_d1_init_cmds_count,

        .flags = {
            /*
             * Keep the 3-wire IO alive. It is not shared with the RGB
             * data pins and BMGR owns the panel IO lifecycle.
             */
            .auto_del_panel_io = 0,

            /*
             * ST7701 supports MADCTL-based mirroring.
             */
            .mirror_by_cmd = 1,
        },
    };

    config.vendor_config = &vendor_config;

    return esp_lcd_new_panel_st7701(
        io,
        &config,
        ret_panel
    );
}


esp_err_t lcd_touch_factory_entry_t(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_touch_config_t *touch_dev_config,
    esp_lcd_touch_handle_t *ret_touch)
{
    ESP_RETURN_ON_FALSE(
        io != NULL &&
        touch_dev_config != NULL &&
        ret_touch != NULL,
        ESP_ERR_INVALID_ARG,
        "sensecap_d1",
        "invalid touch factory arguments"
    );

    return esp_lcd_touch_new_i2c_ft5x06(
        io,
        touch_dev_config,
        ret_touch
    );
}


esp_err_t io_expander_factory_entry_t(
    i2c_master_bus_handle_t i2c_bus,
    const uint16_t dev_addr,
    esp_io_expander_handle_t *handle_ret)
{
    ESP_RETURN_ON_FALSE(
        i2c_bus != NULL &&
        handle_ret != NULL,
        ESP_ERR_INVALID_ARG,
        "sensecap_d1",
        "invalid IO-expander factory arguments"
    );

    return esp_io_expander_new_i2c_pca9535(
        i2c_bus,
        dev_addr,
        handle_ret
    );
}