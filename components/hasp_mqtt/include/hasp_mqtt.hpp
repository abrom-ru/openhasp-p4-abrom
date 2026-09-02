#pragma once

#include "hasp_event.hpp"
#include "hasp_service.hpp"
#include "hasp_service_manager.hpp"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
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

    /** Drain the downlink command queue and dispatch each entry through
     *  hasp_dispatch_command(). Must be called from the LVGL task (holds
     *  the LVGL lock). Returns number of commands dispatched. */
    int drain_command_queue();

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
    std::string command_prefix_;  // "hasp/<hostname>/command" (no trailing slash, S3-compat)
    // Step 4d: additional command sources — mirror S3 mqttGroupTopic +
    // HASP_USE_BROADCAST. Group name defaults to "plates" (S3 MQTT_GROUPNAME),
    // configurable via NVS later. Broadcast prefix is fixed across all plates.
    std::string group_prefix_;    // "hasp/<groupname>/command"
    std::string broadcast_prefix_;// "hasp/broadcast/command"

    // Downlink command queue (step 4c). MQTT_EVENT_DATA runs on the esp-mqtt
    // task with a ~6KB stack (see S3 comment in mqtt_process_topic_payload) —
    // dispatching there would overflow on heavy commands like `run`. Enqueue
    // here, drain from the LVGL task via hasp_mqtt_process_incoming().
    // Queue persists across reconnects (created once, kept alive until dtor).
    QueueHandle_t cmd_queue_ = nullptr;

    esp_event_handler_instance_t hasp_conn_inst_ = nullptr;
    esp_event_handler_instance_t hasp_disc_inst_ = nullptr;

    esp_err_t load_from_nvs();
    esp_err_t start_backend();
    esp_err_t stop_backend();

    // Enqueue a received message (called from MQTT_EVENT_DATA). Copies topic
    // and payload onto the heap; drainer frees them after dispatch. `source`
    // is a static-lifetime tag ("node"/"group"/"bcast") kept only for logging.
    void enqueue_command(const char* topic_after_prefix,
                         const char* payload, int payload_len,
                         const char* source);

    // Step 4d: try to match one command prefix (node/group/broadcast) against
    // the incoming topic. On hit, strips the prefix + optional leading '/',
    // enqueues via enqueue_command, and returns true. `source` is stored in
    // the queued message for log attribution. Zero-length prefix skips.
    bool match_and_enqueue(const std::string& prefix, const char* source,
                           const char* topic, int topic_len,
                           const char* payload, int payload_len);

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

/* C-callable downlink drainer (step 4c). Pop everything currently in the
 * command queue and dispatch each via hasp_dispatch_command(). Intended to
 * be called from an lv_timer on the LVGL task (already under the LVGL lock).
 * Returns the number of messages dispatched. No-op if MQTT not started. */
int hasp_mqtt_process_incoming(void);
#ifdef __cplusplus
}
#endif