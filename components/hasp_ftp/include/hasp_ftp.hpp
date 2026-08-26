#pragma once

#include "hasp_event.hpp"
#include "hasp_service.hpp"
#include "hasp_service_manager.hpp"
#include "simple_ftp_server.h"

class HaspFtp : public HaspService {
public:
    explicit HaspFtp(ServiceManager& mgr) : mgr_(mgr) {
        mode_ = ServiceMode::KeepAlive;   // default for ftp
        
        esp_event_handler_instance_register(
            HASP_EVENT, HASP_EVENT_CONNECTED,
            &HaspFtp::hasp_event_handler, this, nullptr);

        esp_event_handler_instance_register(
            HASP_EVENT, HASP_EVENT_DISCONNECTED,
            &HaspFtp::hasp_event_handler, this, nullptr);
    }
    ~HaspFtp() override;

    const char* name() const override { return "ftp"; }
    bool isRunning() const override;

    // For now we keep ftp config empty / unused
    esp_err_t get_config(JsonObject obj) const override { return ESP_OK; }
    esp_err_t set_config(JsonObjectConst obj) override { return ESP_OK; }

protected:
    esp_err_t start_backend() override;
    esp_err_t stop_backend() override;

    void on_network_up() override;
    void on_network_down() override;

private:
    ServiceManager& mgr_;
    simple_ftp_config_t cfg_;
    simple_ftp_handle_t server_ = nullptr;

    static void hasp_event_handler(
        void* arg,
        esp_event_base_t base,
        int32_t id,
        void* event_data);

    static void on_ftp_event(simple_ftp_event_t evt, uint32_t free_b, uint32_t total_b, void *ctx);
    static void on_ftp_xfer(simple_ftp_xfer_event_t evt, const char *name, uint32_t n, void *ctx);

};
