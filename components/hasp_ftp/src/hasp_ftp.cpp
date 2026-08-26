#include "hasp_ftp.hpp"
#include "esp_log.h"
#include <ArduinoJson.h>

#define FTP_USER "hasp"
#define FTP_PASS "hasp"
#define FTP_BASE_PATH "/"

static const char *TAG = "HASP_FTP";

void HaspFtp::on_ftp_event(simple_ftp_event_t evt, uint32_t free_b, uint32_t total_b, void *ctx)
{
    (void)ctx;
    switch (evt)
    {
    case SIMPLE_FTP_EVT_CONNECT:
        ESP_LOGI(TAG, "FTP: client connected");
        break;
    case SIMPLE_FTP_EVT_DISCONNECT:
        ESP_LOGI(TAG, "FTP: client disconnected");
        break;
    case SIMPLE_FTP_EVT_FREE_SPACE_CHANGE:
        ESP_LOGI(TAG, "FTP: location %lu / %lu", (unsigned long)free_b, (unsigned long)total_b);
        break;
    }
}

void HaspFtp::on_ftp_xfer(simple_ftp_xfer_event_t evt, const char *name, uint32_t n, void *ctx)
{
    (void)ctx;
    switch (evt)
    {
    case SIMPLE_FTP_XFER_UPLOAD_START:
        ESP_LOGI(TAG, "upload start: %s", name);
        break;
    case SIMPLE_FTP_XFER_DOWNLOAD_START:
        ESP_LOGI(TAG, "download start: %s (%lu)", name, (unsigned long)n);
        break;
    case SIMPLE_FTP_XFER_STOP:
        ESP_LOGI(TAG, "transfer done: %s (%lu bytes)", name, (unsigned long)n);
        break;
    case SIMPLE_FTP_XFER_ERROR:
        ESP_LOGW(TAG, "transfer error: %s", name);
        break;
    default:
        break;
    }
}

HaspFtp::~HaspFtp()
{
    stop();
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------
esp_err_t HaspFtp::start_backend()
{
    if (server_)
        return ESP_OK;

    log_memory();

    simple_ftp_config_t cfg_ = SIMPLE_FTP_CONFIG_DEFAULT();
    cfg_.user = FTP_USER;
    cfg_.pass = FTP_PASS;
    cfg_.base_path = FTP_BASE_PATH;
    cfg_.event_cb = on_ftp_event;
    cfg_.xfer_cb = on_ftp_xfer;

    esp_err_t err = simple_ftp_create(&cfg_, &server_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "simple_ftp_create failed: %s", esp_err_to_name(err));
        return err;
    }
    err = simple_ftp_start(server_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "simple_ftp_start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "FTP server started on port 21");
    log_memory();
    return ESP_OK;
}

esp_err_t HaspFtp::stop_backend()
{
    if (!server_)
        return ESP_OK;

    simple_ftp_stop(server_);
    server_ = nullptr;
    ESP_LOGI(TAG, "FTP server stopped");
    return ESP_OK;
}

bool HaspFtp::isRunning() const
{
    return server_ != nullptr;
}

void HaspFtp::hasp_event_handler(
    void *arg,
    esp_event_base_t base,
    int32_t id,
    void *event_data)
{
    auto *self = static_cast<HaspFtp *>(arg);

    if (id == HASP_EVENT_CONNECTED)
    {
        self->on_network_up();
    }
    else if (id == HASP_EVENT_DISCONNECTED)
    {
        self->on_network_down();
    }
}

void HaspFtp::on_network_up()
{
    switch (mode_)
    {
    case ServiceMode::Never:
    case ServiceMode::Manual:
        return;

    case ServiceMode::Once:
        if (ran_once_)
            return;
        if (start_backend() == ESP_OK)
            ran_once_ = true;
        break;

    case ServiceMode::KeepAlive:
        start_backend();
        break;

    case ServiceMode::OnBoot:
        // already started at boot; optional no-op or ensure running
        if (!isRunning())
            start_backend();
        break;
    }
}

void HaspFtp::on_network_down()
{
    switch (mode_)
    {
    case ServiceMode::KeepAlive:
        stop_backend();
        break;

    case ServiceMode::Once:
    case ServiceMode::OnBoot:
    case ServiceMode::Manual:
    case ServiceMode::Never:
        // leave running (or already stopped)
        break;
    }
}