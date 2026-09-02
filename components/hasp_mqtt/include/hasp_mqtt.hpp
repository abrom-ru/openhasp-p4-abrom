#pragma once

#include "hasp_event.hpp"
#include "hasp_service.hpp"
#include "hasp_service_manager.hpp"
#include "mqtt_client.h"
#include <string>

class HaspMqtt : public HaspService {
public:
    explicit HaspMqtt(ServiceManager& mgr) : mgr_(mgr) {
        mode_ = ServiceMode::KeepAlive;   // default for http
        
        esp_event_handler_instance_register(
            HASP_EVENT, HASP_EVENT_CONNECTED,
            &HaspMqtt::hasp_event_handler, this, nullptr);

        esp_event_handler_instance_register(
            HASP_EVENT, HASP_EVENT_DISCONNECTED,
            &HaspMqtt::hasp_event_handler, this, nullptr);
    }
    ~HaspMqtt() override;

    const char* name() const override { return "mqtt"; }

    esp_err_t start() override;
    esp_err_t stop() override;
    bool isRunning() const override;

    esp_err_t get_config(JsonObject obj) const override;
    esp_err_t set_config(JsonObjectConst obj) override;

    /** Publish UTF-8 payload (no-op if not connected) */
    esp_err_t publish(const char* topic, const char* data, int qos = 0, bool retain = false);

    /** Publish "<state_prefix><subtopic>" = "hasp/<hostname>/state/<subtopic>".
     *  qos=0, retain=false — mirrors S3 mqtt_send_state defaults. */
    esp_err_t publish_state(const char* subtopic, const char* payload);

    bool isConnected() const { return connected_; }

private:
    ServiceManager& mgr_;
    esp_mqtt_client_handle_t client_ = nullptr;

    bool started_   = false;
    bool connected_ = false;
    bool handlers_registered_ = false;

    std::string host_;
    uint32_t    port_ = 1883;
    std::string user_;
    std::string password_;
    std::string client_id_;

    // Runtime-built at start_backend (must outlive the mqtt client because
    // esp-mqtt stores raw pointers into these strings via c_str()).
    std::string full_client_id_;  // "<hostname>_<mac3>", S3-compat
    std::string lwt_topic_;       // "hasp/<hostname>/LWT"
    std::string state_prefix_;    // "hasp/<hostname>/state/" (trailing slash, S3-compat)

    esp_event_handler_instance_t hasp_conn_inst_ = nullptr;
    esp_event_handler_instance_t hasp_disc_inst_ = nullptr;

    esp_err_t load_from_nvs();
    esp_err_t start_backend();
    esp_err_t stop_backend();

    void register_hasp_handlers();
    void unregister_hasp_handlers();

    static void mqtt_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data);
    static void hasp_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data);

    void on_mqtt_connected();
    void on_mqtt_disconnected();
    void on_network_up();
    void on_network_down();
};

/* C-callable state publisher (step 4b).
 * Mirrors S3 mqtt_send_state(subtopic, payload, retain=false).
 * Callable from anywhere (e.g. hasp component's dispatch_state_subtopic).
 * No-op if no HaspMqtt instance is registered yet or if not connected. */
#ifdef __cplusplus
extern "C" {
#endif
int hasp_mqtt_publish_state(const char* subtopic, const char* payload);
#ifdef __cplusplus
}
#endif