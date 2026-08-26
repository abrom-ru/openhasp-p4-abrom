#include "hasp_wifi.hpp"
#include "hasp_event.hpp"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include <cstring>

static const char *TAG = "HASP_WIFI";

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

HaspWifi::~HaspWifi()
{
    stop_backend();
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

esp_err_t HaspWifi::get_config(JsonObject obj) const
{
    // We write into obj[name()]  i.e. obj["wifi"]
    JsonObject wifi = obj[name()].to<JsonObject>();

    std::string ssid, password, hostname;

    if (!ssid_.empty())
        ssid = ssid_;
    else
        nvs_get_string("ssid", ssid);

    if (!password_.empty())
        password = password_;
    else
        nvs_get_string("password", password);

    if (!hostname_.empty())
        hostname = hostname_;
    else
        nvs_get_string("hostname", hostname);

    wifi["ssid"] = ssid;
    wifi["password"] = "******"; // always masked
    wifi["hostname"] = hostname;

    return ESP_OK;
}

esp_err_t HaspWifi::set_config(JsonObjectConst obj)
{
    // Caller may pass either the full document or just the "wifi" object.
    // Prefer the nested object if it exists.
    JsonObjectConst wifi = obj[name()] | obj; // fallback to obj itself

    if (wifi["ssid"].is<const char *>())
    {
        ssid_ = wifi["ssid"].as<std::string>();
        nvs_set_string("ssid", ssid_);
    }

    if (wifi["password"].is<const char *>())
    {
        std::string pw = wifi["password"].as<std::string>();
        if (pw != "******")
        { // only set when it really changed
            password_ = pw;
            nvs_set_string("password", password_);
        }
    }

    if (wifi["hostname"].is<const char *>())
    {
        hostname_ = wifi["hostname"].as<std::string>();
        nvs_set_string("hostname", hostname_);
    }

    return ESP_OK;
}

esp_err_t HaspWifi::load_from_nvs()
{
    nvs_get_string("ssid", ssid_);
    nvs_get_string("password", password_);
    nvs_get_string("hostname", hostname_);

    if (ssid_.empty())
    {
        ESP_LOGW(TAG, "No SSID configured");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------

esp_err_t HaspWifi::start_backend()
{
    if (started_)
        return ESP_OK;

    log_memory();

    // Load credentials if we don't have them yet
    if (ssid_.empty())
    {
        esp_err_t err = load_from_nvs();
        if (err != ESP_OK)
            return err;
    }

    // TCP/IP stack + default event loop (safe to call multiple times)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &HaspWifi::wifi_event_handler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &HaspWifi::ip_event_handler, this, nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Hostname (optional)
    if (!hostname_.empty())
    {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif)
        {
            esp_netif_set_hostname(netif, hostname_.c_str());
        }
    }

    esp_err_t err = apply_and_connect();
    if (err != ESP_OK)
        return err;

    started_ = true;
    ESP_LOGI(TAG, "WiFi service started, connecting to '%s'", ssid_.c_str());

    log_memory();
    return ESP_OK;
}

esp_err_t HaspWifi::apply_and_connect()
{
    wifi_config_t conf = {};
    strncpy(reinterpret_cast<char *>(conf.sta.ssid), ssid_.c_str(),
            sizeof(conf.sta.ssid) - 1);
    strncpy(reinterpret_cast<char *>(conf.sta.password), password_.c_str(),
            sizeof(conf.sta.password) - 1);
    conf.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &conf));
    ESP_ERROR_CHECK(esp_wifi_start());
    return esp_wifi_connect();
}

esp_err_t HaspWifi::stop_backend()
{
    if (!started_)
        return ESP_OK;

    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();

    // Note: we leave the default event loop and netif alone
    // (other services may still need them)

    started_ = false;
    connected_ = false;
    ESP_LOGI(TAG, "WiFi service stopped");
    return ESP_OK;
}

bool HaspWifi::isRunning() const
{
    return started_;
}

bool HaspWifi::isConnected() const
{
    return connected_;
}

std::string HaspWifi::getIp() const
{
    if (!connected_)
        return {};

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif)
        return {};

    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK)
        return {};

    char buf[16];
    snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.ip));

    return buf;
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void HaspWifi::wifi_event_handler(void *arg, esp_event_base_t base,
                                  int32_t id, void *data)
{
    auto *self = static_cast<HaspWifi *>(arg);

    switch (id)
    {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        self->on_network_down();
        // Simple auto-reconnect
        esp_wifi_connect();
        break;

    default:
        break;
    }
}

void HaspWifi::ip_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    auto *self = static_cast<HaspWifi *>(arg);

    if (id == IP_EVENT_STA_GOT_IP)
    {
        auto *event = static_cast<ip_event_got_ip_t *>(data);
        char buf[16];
        snprintf(buf, sizeof(buf), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", buf);
        self->on_network_up();
    }
}

void HaspWifi::on_network_up()
{
    connected_ = true;
    std::string ip = getIp();
    ESP_LOGI(TAG, "Got IP: %s", ip.c_str());
    esp_event_post(HASP_EVENT, HASP_EVENT_CONNECTED, nullptr, 0, 0);
}

void HaspWifi::on_network_down()
{
    connected_ = false;
    ESP_LOGW(TAG, "Disconnected");
    esp_event_post(HASP_EVENT, HASP_EVENT_DISCONNECTED, nullptr, 0, 0);
}