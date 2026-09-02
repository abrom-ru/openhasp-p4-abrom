#pragma once

#include "hasp_event.hpp"
#include "hasp_service.hpp"
#include "hasp_service_manager.hpp"
#include "esp_http_server.h"

#include <string>

class HaspHttp : public HaspService
{
public:
    explicit HaspHttp(ServiceManager &mgr) : mgr_(mgr)
    {
        mode_ = ServiceMode::KeepAlive; // default for http

        esp_event_handler_instance_register(
            HASP_EVENT, HASP_EVENT_CONNECTED,
            &HaspHttp::hasp_event_handler, this, nullptr);

        esp_event_handler_instance_register(
            HASP_EVENT, HASP_EVENT_DISCONNECTED,
            &HaspHttp::hasp_event_handler, this, nullptr);
    }
    ~HaspHttp() override;

    const char *name() const override { return "http"; }
    bool isRunning() const override;

    // Step 7C-1: Basic-Auth credentials (S3-mirror httpUser / httpPassword)
    // plus listen port. Password is masked in get_config the same way
    // wifi/mqtt do it — real value only in NVS.
    esp_err_t get_config(JsonObject obj) const override;
    esp_err_t set_config(JsonObjectConst obj) override;

protected:
    esp_err_t start_backend() override;
    esp_err_t stop_backend() override;

    void on_network_up() override;
    void on_network_down() override;

private:
    ServiceManager &mgr_;
    httpd_handle_t server_ = nullptr;

    // Step 7C-1: runtime config (S3-mirror hasp_http_config_t)
    std::string user_;
    std::string password_;
    uint16_t port_ = 80;

    esp_err_t load_from_nvs();

    static void hasp_event_handler(
        void *arg,
        esp_event_base_t base,
        int32_t id,
        void *event_data);

    // Step 7C-1: HTTP Basic Auth. When password is empty the server is
    // fully open — matches S3 default httpIsAuthenticated().
    esp_err_t check_basic_auth(httpd_req_t *req) const;
    static esp_err_t require_auth(httpd_req_t *req);

    // --- URI handlers (S3-mirror SSR) ---
    static esp_err_t root_get_handler(httpd_req_t *req);
    static esp_err_t config_get_handler(httpd_req_t *req);
    static esp_err_t config_post_handler(httpd_req_t *req);
    static esp_err_t wifi_get_handler(httpd_req_t *req);
    static esp_err_t mqtt_get_handler(httpd_req_t *req);
    static esp_err_t hasp_get_handler(httpd_req_t *req);
    static esp_err_t http_get_handler(httpd_req_t *req);
    static esp_err_t info_get_handler(httpd_req_t *req);
    static esp_err_t reboot_get_handler(httpd_req_t *req);
    static esp_err_t css_get_handler(httpd_req_t *req);
    static esp_err_t console_get_handler(httpd_req_t *req);

    static esp_err_t ws_handler(httpd_req_t *req);

    // Helper: pull HaspHttp* back out of user_ctx
    static HaspHttp *from_req(httpd_req_t *req)
    {
        return static_cast<HaspHttp *>(req->user_ctx);
    }
};
