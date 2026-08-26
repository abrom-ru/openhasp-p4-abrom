#pragma once

#include "hasp_event.hpp"
#include "hasp_service.hpp"
#include "hasp_service_manager.hpp"
#include "esp_http_server.h"

static constexpr size_t SESSION_TOKEN_LEN = 32;
struct Session
{
    bool active;
    uint8_t token[SESSION_TOKEN_LEN];
    uint32_t last_activity;
};

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

    // For now we keep http config empty / unused
    esp_err_t get_config(JsonObject obj) const override { return ESP_OK; }
    esp_err_t set_config(JsonObjectConst obj) override { return ESP_OK; }

protected:
    esp_err_t start_backend() override;
    esp_err_t stop_backend() override;

    void on_network_up() override;
    void on_network_down() override;

private:
    ServiceManager &mgr_;
    httpd_handle_t server_ = nullptr;

    static void hasp_event_handler(
        void *arg,
        esp_event_base_t base,
        int32_t id,
        void *event_data);

    static esp_err_t create_session(httpd_req_t *req);
    static void clear_session(Session &session);
    static esp_err_t authenticate_request(httpd_req_t *req);
    static esp_err_t require_auth(httpd_req_t *req);

    static esp_err_t config_get_handler(httpd_req_t *req);
    static esp_err_t config_post_handler(httpd_req_t *req);
    static esp_err_t console_get_handler(httpd_req_t *req);

    static esp_err_t ws_handler(httpd_req_t *req);
    static esp_err_t ws_auth_handler(httpd_req_t *req);

    // Helper to get the HaspHttp instance from the request
    static HaspHttp *from_req(httpd_req_t *req)
    {
        return static_cast<HaspHttp *>(req->user_ctx);
    }
};
