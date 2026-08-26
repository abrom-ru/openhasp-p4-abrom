#pragma once

#include "hasp_service.hpp"
#include <string>
#include "esp_event_base.h"

class HaspWifi : public HaspService {
public:
    HaspWifi() = default;
    ~HaspWifi() override;

    const char* name() const override { return "wifi"; }
    bool isRunning() const override;

    esp_err_t get_config(JsonObject doc) const override;
    esp_err_t set_config(const JsonObjectConst doc) override;

    // Optional helpers
    bool isConnected() const;
    std::string getIp() const;

protected:
    esp_err_t start_backend() override;
    esp_err_t stop_backend() override;

    void on_network_up() override;
    void on_network_down() override;

private:
    bool started_   = false;
    bool connected_ = false;

    std::string ssid_;
    std::string password_;
    std::string hostname_;

    // Event handling
    static void wifi_event_handler(void* arg, esp_event_base_t base,
                                   int32_t id, void* data);
    static void ip_event_handler(void* arg, esp_event_base_t base,
                                 int32_t id, void* data);

    esp_err_t load_from_nvs();          // fill ssid_/password_/hostname_
    esp_err_t apply_and_connect();      // set config + connect
};