#include "nvs_flash.h"
#include "esp_log.h"

#include "esp_board_manager.h"
#include "esp_board_device.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
// or the generated / device headers that declare the structs
#include "dev_display_lcd.h" // for dev_display_lcd_handles_t
#include "dev_lcd_touch.h"   // for the touch handle type (if needed)

#include "esp_lvgl_port.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "board_lvgl.h"
#include "lvgl.h"
#include "lv_demos.h"

#include "driver/ledc.h"
#include "periph_ledc.h"

#include "hasp.hpp"
#include "hasp_dispatch.h"
#include "hasp_object.h" // 3f smoke: hasp_find_obj_from_page_id
#include "hasp_fs.hpp"
#include "hasp_ftp.hpp"
#include "hasp_http.hpp"
#include "hasp_log.hpp"
#include "hasp_mqtt.hpp"
#include "hasp_service_manager.hpp"
#include "hasp_wifi.hpp"

#include "driver/gpio.h"

#if CONFIG_IDF_TARGET_ESP32P4
// Backlight is driven by LEDC via the `lcd_brightness` BMGR device — this
// GPIO number is only used by the S3 branch.
#define LCD_BL_GPIO 23
// board_devices.yaml: ledc_backlight uses LEDC_TIMER_10_BIT (max duty 1023).
#define LCD_BL_MAX_DUTY 1023
#else
#define LCD_BL_GPIO 45  // WT32-SC01 Plus default
#endif

static const char *TAG = "main";

#if CONFIG_IDF_TARGET_ESP32P4
// Ramp the LEDC-driven backlight to `percent`. Matches factory
// bsp_display_brightness_set() in esp32_p4_function_ev_board.c.
static void board_backlight_set_percent(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    void *handle = NULL;
    esp_err_t err = esp_board_manager_get_device_handle("lcd_brightness", &handle);
    if (err != ESP_OK || handle == NULL) {
        ESP_LOGE(TAG, "backlight: no lcd_brightness handle (%s)", esp_err_to_name(err));
        return;
    }
    periph_ledc_handle_t *ledc = (periph_ledc_handle_t *)handle;
    uint32_t duty = ((uint32_t)LCD_BL_MAX_DUTY * (uint32_t)percent) / 100U;
    ledc_set_duty(ledc->speed_mode, ledc->channel, duty);
    ledc_update_duty(ledc->speed_mode, ledc->channel);
    ESP_LOGI(TAG, "Backlight -> %d%% (duty=%lu)", percent, (unsigned long)duty);
}
#endif

static ServiceManager mgr;
static HaspWifi wifi;
static HaspFtp ftp(mgr);   // only knows the manager
static HaspHttp http(mgr); // only knows the manager
static HaspMqtt mqtt(mgr); // only knows the manager

extern const esp_board_device_desc_t g_esp_board_devices[];

static void lvgl_log_cb(lv_log_level_t level, const char *buf)
{
    /*
    LV_LOG_LEVEL_TRACE: A lot of logs to give detailed information
    LV_LOG_LEVEL_INFO: Log important events.
    LV_LOG_LEVEL_WARN: Log if something unwanted happened but didn't cause a problem.
    LV_LOG_LEVEL_ERROR: Log only critical issues, where the system may fail.
    LV_LOG_LEVEL_USER: Log only custom log messages added by the user.
    */
    esp_log_level_t log_level = ESP_LOG_VERBOSE;

    // Routes directly through the standard formatter engine with an LVGL tag label
    switch (level)
    {
    case LV_LOG_LEVEL_TRACE:
        log_level = ESP_LOG_DEBUG;
        break;
    case LV_LOG_LEVEL_INFO:
        log_level = ESP_LOG_INFO;
        break;
    case LV_LOG_LEVEL_WARN:
        log_level = ESP_LOG_WARN;
        break;
    case LV_LOG_LEVEL_ERROR:
        log_level = ESP_LOG_ERROR;
        break;
    default:
        log_level = ESP_LOG_VERBOSE;
        break;
    }

    hasp_log_printf_with_tag(log_level, "LVGL", "%s", buf);
}

static void app_ui_init()
{
    ESP_LOGI(TAG, "app_ui_init: enter");

    // Use a bounded timeout instead of 0/portMAX so we can diagnose deadlocks.
    bool locked = lvgl_port_lock(2000);
    ESP_LOGI(TAG, "app_ui_init: lock -> %s", locked ? "OK" : "TIMEOUT");

    if (locked)
    {
        lv_obj_t *scr = lv_screen_active();
        const int w = lv_obj_get_width(scr);
        const int h = lv_obj_get_height(scr);
        ESP_LOGI(TAG, "app_ui_init: scr=%p, size=%dx%d", scr, w, h);

        hasp_init();
        // Step 3a widgets on page 1 (all six supported obj types).
        hasp_dispatch_jsonl(
            "{\"page\":1,\"id\":1,\"obj\":\"label\","
            "\"x\":40,\"y\":40,\"w\":260,\"h\":40,\"text\":\"HASP step 3b - page 1\"}");
        hasp_dispatch_jsonl(
            "{\"page\":1,\"id\":2,\"obj\":\"btn\","
            "\"x\":320,\"y\":30,\"w\":180,\"h\":60,\"text\":\"Button\"}");
        hasp_dispatch_jsonl(
            "{\"page\":1,\"id\":3,\"obj\":\"switch\","
            "\"x\":540,\"y\":40,\"w\":80,\"h\":40,\"val\":1}");
        hasp_dispatch_jsonl(
            "{\"page\":1,\"id\":4,\"obj\":\"checkbox\","
            "\"x\":40,\"y\":160,\"w\":220,\"h\":40,\"text\":\"Check me\",\"val\":1}");
        hasp_dispatch_jsonl(
            "{\"page\":1,\"id\":5,\"obj\":\"slider\","
            "\"x\":280,\"y\":170,\"w\":260,\"h\":20,\"val\":42}");
        hasp_dispatch_jsonl(
            "{\"page\":1,\"id\":6,\"obj\":\"bar\","
            "\"x\":580,\"y\":170,\"w\":260,\"h\":20,\"val\":75}");

        // Step 3b: extra widgets on page 2 to prove the page parent switch works.
        hasp_dispatch_jsonl(
            "{\"page\":2,\"id\":1,\"obj\":\"label\","
            "\"x\":40,\"y\":40,\"w\":260,\"h\":40,\"text\":\"HASP step 3b - page 2\"}");
        hasp_dispatch_jsonl(
            "{\"page\":2,\"id\":2,\"obj\":\"slider\","
            "\"x\":40,\"y\":100,\"w\":400,\"h\":20,\"val\":80}");

        // 3d smoke tests via hasp_dispatch_command (text commands, not jsonl):
        //  - pXbY.attr=value  -> parsed, routed to hasp_process_attribute
        //  - "page N"         -> haspPages.set(N)
        // Expected log: "reuse p1b1" (label text update via dispatch), no warnings.
        hasp_dispatch_command("p1b1.text=HASP 3e: attributes OK");
        hasp_dispatch_command("p2b2.val=25");
        // Also exercise jsonl and json array paths through the same entry point.
        hasp_dispatch_command("{\"page\":1,\"id\":6,\"val\":50}"); // update bar via jsonl
        hasp_dispatch_command("[\"p1b5.val=90\", \"p1b3.val=0\"]"); // json array of cmds

        // 3e smoke tests — exercise the SDBM attribute switch introduced in
        // components/hasp/src/hasp_attribute.cpp. Each command should mutate
        // the corresponding widget with no "unknown attribute" warning.
        hasp_dispatch_command("p1b2.bg_color=#4040ff");   // button: blue background
        hasp_dispatch_command("p1b1.text_color=#00ff88"); // label: greenish text
        hasp_dispatch_command("p1b6.opacity=128");        // bar at 50% transparency
        hasp_dispatch_command("p1b2.toggle=1");           // button becomes checkable
        hasp_dispatch_command("p1b2.val=1");              // ... and now shows checked state
        hasp_dispatch_command("p1b1.align=center");       // label text centered
        hasp_dispatch_command("p1b1.mode=scroll");        // label scrolls if too long
        hasp_dispatch_command("p1b5.min=0");              // slider range verify (already 0..100)
        hasp_dispatch_command("p1b5.max=200");
        hasp_dispatch_command("p2b1.hidden=1");           // page-2 label hidden on entry
        hasp_dispatch_command("p2b1.hidden=0");           // ... then shown again

        // 3g smoke tests — exercise the expanded local-style table (radius,
        // border, shadow, outline, padding, bg gradient, text spacing, line).
        // Each command must set the property with NO "unknown attribute" warning;
        // the effect should be visible on the panel (rounded corners, shadow,
        // gradient background on p1b6, etc.). Selector is LV_PART_MAIN for MVP.
        hasp_dispatch_command("p1b2.radius=18");            // button: rounded corners
        hasp_dispatch_command("p1b2.border_width=3");
        hasp_dispatch_command("p1b2.border_color=#ff0000"); // red border
        hasp_dispatch_command("p1b2.border_side=full");
        hasp_dispatch_command("p1b2.shadow_width=12");
        hasp_dispatch_command("p1b2.shadow_ofs_x=4");
        hasp_dispatch_command("p1b2.shadow_ofs_y=4");
        hasp_dispatch_command("p1b2.shadow_color=#000000");
        hasp_dispatch_command("p1b2.shadow_opa=200");
        hasp_dispatch_command("p1b2.outline_width=2");
        hasp_dispatch_command("p1b2.outline_pad=4");
        hasp_dispatch_command("p1b2.outline_color=#ffff00"); // yellow outline
        hasp_dispatch_command("p1b6.bg_color=#004080");      // bar bg base
        hasp_dispatch_command("p1b6.bg_grad_color=#ff8000"); // orange gradient stop
        hasp_dispatch_command("p1b6.bg_grad_dir=hor");       // horizontal gradient
        hasp_dispatch_command("p1b1.pad_left=12");           // label padding
        hasp_dispatch_command("p1b1.pad_top=6");
        hasp_dispatch_command("p1b1.text_letter_space=2");
        hasp_dispatch_command("p1b1.text_line_space=4");
        hasp_dispatch_command("p1b1.text_decor=underline");
        hasp_dispatch_command("p1b4.radius=8");              // checkbox rounded
        hasp_dispatch_command("p1b5.opa_scale=255");         // slider fully opaque

        // 3f smoke tests — synthesize LVGL events on registered objects so we
        // can prove the event handlers now emit real JSON via object_dispatch_state
        // (log line: "state pXbY => {...}"). Real touch on the panel triggers
        // the same path; this exercises it without hardware.
        {
            lv_obj_t* btn = hasp_find_obj_from_page_id(1, 2);   // p1b2 (button)
            lv_obj_t* sw  = hasp_find_obj_from_page_id(1, 3);   // p1b3 (switch)
            lv_obj_t* sld = hasp_find_obj_from_page_id(1, 5);   // p1b5 (slider)
            if (btn) {
                lv_obj_send_event(btn, LV_EVENT_PRESSED,       nullptr); // → "down"
                lv_obj_send_event(btn, LV_EVENT_SHORT_CLICKED, nullptr); // → "up"
                lv_obj_send_event(btn, LV_EVENT_RELEASED,      nullptr); // → "release"
            }
            if (sw) {
                lv_obj_add_state(sw, LV_STATE_CHECKED);
                lv_obj_send_event(sw, LV_EVENT_PRESSED, nullptr);        // → {"event":"down","val":1}
            }
            if (sld) {
                lv_slider_set_value(sld, 77, LV_ANIM_OFF);
                lv_obj_send_event(sld, LV_EVENT_VALUE_CHANGED, nullptr); // → {"event":"changed","val":77}
            }
        }

        // 3d demo: auto-toggle pages 1<->2 via dispatch every 4s (proves the
        // "page N" text command reaches haspPages.set()). Real invocation
        // path in 3f/4 will be MQTT/console -> hasp_dispatch_command.
        lv_timer_t* t = lv_timer_create([](lv_timer_t* /*t*/) {
            uint8_t cur = hasp_get_page();
            hasp_dispatch_command(cur == 1 ? "page 2" : "page 1");
        }, 4000, nullptr);
        (void)t;

        lv_refr_now(NULL);
        ESP_LOGI(TAG, "app_ui_init: HASP step-3b widgets on pages 1+2, current=%u", hasp_get_page());

        lvgl_port_unlock();
    }
    else {
        ESP_LOGE(TAG, "app_ui_init: FAILED to acquire LVGL lock — task not running?");
    }
}

static void test_gpio(gpio_num_t gpio_num)
{
    ESP_LOGI(TAG, "Testing GPIO%d", gpio_num);

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    ESP_LOGI(TAG, "gpio_config(GPIO%d) -> %s",
             gpio_num, esp_err_to_name(err));

    if (err == ESP_OK) {
        gpio_set_level(gpio_num, 0);
        ESP_LOGI(TAG, "GPIO%d -> 0", gpio_num);

        gpio_set_level(gpio_num, 1);
        ESP_LOGI(TAG, "GPIO%d -> 1", gpio_num);

        gpio_set_level(gpio_num, 0);
        ESP_LOGI(TAG, "GPIO%d -> 0", gpio_num);
    }
}

extern "C" void app_main()
{
   // gpio_reset_pin(GPIO_NUM_41);
   // gpio_reset_pin(GPIO_NUM_42);
    gpio_dump_io_configuration(stdout,
                           (1ULL << 41) | (1ULL << 42));

    hasp_log_init();

    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(hasp_fs_init()); // before network services

#if !CONFIG_IDF_TARGET_ESP32P4
    // Registration order = start order
    mgr.add(&wifi);
    mgr.add(&http);
    mgr.add(&mqtt);
    mgr.add(&ftp);
#endif

    // First-time credentials (comment out after first run)
    {
        JsonDocument doc;
        doc["wifi"]["ssid"] = "YOUR_SSID";
        doc["wifi"]["password"] = "YOUR_PASSWORD";
        doc["wifi"]["hostname"] = "openhasp";
        // mgr.set_config(doc.as<JsonObject>());
    }

    {
        JsonDocument doc;
        doc["mqtt"]["host"] = "homeassistant.local";
        doc["mqtt"]["port"] = 1883;
        doc["mqtt"]["user"] = "YOUR_USERNAME";
        doc["mqtt"]["password"] = "YOUR_PASSWORD";
        doc["mqtt"]["client_id"] = "plate01-mvp";
        // mgr.set_config(doc.as<JsonObject>());
    }

    gpio_dump_io_configuration(stdout,
                           (1ULL << 41) | (1ULL << 42));
    ESP_ERROR_CHECK(esp_board_manager_init());
    esp_board_manager_print();

#if CONFIG_IDF_TARGET_ESP32P4
    // Backlight is already OFF: the `lcd_brightness` BMGR device is configured
    // with default_percent=0, which mirrors the factory demo's
    // bsp_display_brightness_init() (LEDC channel installed at duty=0).
    // We ramp it to 100 % via LEDC further below, only after LVGL has flushed
    // its first frame — same ordering as bsp_display_start_with_config().
#else
    // S3 branch: no LEDC ctrl device — keep the original raw-GPIO backlight
    // handling. Off during LVGL bring-up, on once we've drawn a frame.
    gpio_config_t bl_conf = {
        .pin_bit_mask = 1ULL << LCD_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_conf);
    gpio_set_level((gpio_num_t)LCD_BL_GPIO, 0);
    ESP_LOGI(TAG, "Backlight configured OFF (GPIO%d)", LCD_BL_GPIO);
#endif

    ESP_ERROR_CHECK(board_lvgl_init());
    ESP_LOGI(TAG, "board_lvgl_init returned OK");
    lv_log_register_print_cb(lvgl_log_cb);

    // Give the lvgl_port task a moment to actually start scheduling.
    vTaskDelay(pdMS_TO_TICKS(200));

    // Confirm LVGL sees a default display.
    lv_display_t *def_disp = lv_display_get_default();
    ESP_LOGI(TAG, "lv_display_get_default() = %p, hor=%d ver=%d",
             def_disp,
             def_disp ? lv_display_get_horizontal_resolution(def_disp) : -1,
             def_disp ? lv_display_get_vertical_resolution(def_disp)   : -1);

    app_ui_init();

    // Backlight ON via LEDC after LVGL has drawn (matches factory
    // bsp_display_backlight_on() -> bsp_display_brightness_set(100)).
#if CONFIG_IDF_TARGET_ESP32P4
    board_backlight_set_percent(100);
#else
    gpio_set_level((gpio_num_t)LCD_BL_GPIO, 1);
    ESP_LOGI(TAG, "Backlight ON (GPIO%d)", LCD_BL_GPIO);
#endif
    ESP_LOGI(TAG, "app_ui_init returned; LVGL task keeps rendering the demo");

    // // ---------- Fill screen CYAN ----------
    // const int width = 320; // or 480 depending on orientation
    // const int height = 480;

    // // CYAN in RGB565 = 0x07FF
    // uint16_t *line = (uint16_t *)heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_DMA);
    // if (line)
    // {
    //     for (int i = 0; i < width; i++)
    //     {
    //         // 0xF81F;         // Cyan
    //         line[i] = (0xF81F); // Cyan, byte-swapped for little-endian
    //     }
    //     for (int y = 0; y < height; y++)
    //     {
    //         esp_lcd_panel_draw_bitmap(panel, 0, y, width, y + 1, line);
    //     }
    //     free(line);
    // }

    // ESP_LOGI("main", "Screen should now be CYAN with backlight on");


#if !CONFIG_IDF_TARGET_ESP32P4
    mgr.startAll(); // wifi first, then http

    // From here the LVGL task runs; app_main can return or do other work

    // Wait until we have an IP (simple polling for MVP)
    while (!wifi.isConnected())
    {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "IP: %s", wifi.getIp().c_str());
#else
    // P4 bring-up: step 2 (Wi-Fi via esp-hosted + C6) verified separately.
    // Step 3 = wire HASP services (mgr.startAll) same as S3 branch above.
    ESP_LOGI(TAG, "P4 bring-up: HASP services wiring pending (step 3)");
#endif
}
