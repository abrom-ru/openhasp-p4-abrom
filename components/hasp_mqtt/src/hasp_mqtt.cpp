#include "hasp_mqtt.hpp"
#include "hasp_event.hpp"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "HASP_MQTT";

HaspMqtt::~HaspMqtt()
{
    stop();
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

esp_err_t HaspMqtt::get_config(JsonObject obj) const
{
    JsonObject mqtt = obj[name()].to<JsonObject>();

    std::string host, user, password, client_id;
    uint32_t port = 1883;

    if (!host_.empty()) host = host_;
    else nvs_get_string("host", host);

    if (port_ != 0) port = port_;
    else nvs_get_u32("port", port);

    if (!user_.empty()) user = user_;
    else nvs_get_string("user", user);

    nvs_get_string("password", password);  // only to know if set

    if (!client_id_.empty()) client_id = client_id_;
    else nvs_get_string("client_id", client_id);

    mqtt["host"]      = host;
    mqtt["port"]      = port;
    mqtt["user"]      = user;
    mqtt["password"]  = "******";
    mqtt["client_id"] = client_id;

    // mode is handled by base later; for MVP you can store it the same way as other services
    return ESP_OK;
}

esp_err_t HaspMqtt::set_config(JsonObjectConst obj)
{
    JsonObjectConst mqtt = obj[name()] | obj;

    if (mqtt["host"].is<const char*>()) {
        host_ = mqtt["host"].as<std::string>();
        nvs_set_string("host", host_);
    }
    if (mqtt["port"].is<uint32_t>() || mqtt["port"].is<int>()) {
        port_ = mqtt["port"].as<uint32_t>();
        nvs_set_u32("port", port_);
    }
    if (mqtt["user"].is<const char*>()) {
        user_ = mqtt["user"].as<std::string>();
        nvs_set_string("user", user_);
    }
    if (mqtt["password"].is<const char*>()) {
        std::string pw = mqtt["password"].as<std::string>();
        if (pw != "******") {
            password_ = pw;
            nvs_set_string("password", password_);
        }
    }
    if (mqtt["client_id"].is<const char*>()) {
        client_id_ = mqtt["client_id"].as<std::string>();
        nvs_set_string("client_id", client_id_);
    }
    return ESP_OK;
}

esp_err_t HaspMqtt::load_from_nvs()
{
    nvs_get_string("host", host_);
    nvs_get_u32("port", port_);
    if (port_ == 0) port_ = 1883;
    nvs_get_string("user", user_);
    nvs_get_string("password", password_);
    nvs_get_string("client_id", client_id_);

    if (host_.empty()) {
        ESP_LOGW(TAG, "No MQTT host configured");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Backend start/stop
// ---------------------------------------------------------------------------

esp_err_t HaspMqtt::start_backend()
{
    if (client_) return ESP_OK;

    if (host_.empty() && load_from_nvs() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = nullptr;  // use hostname + port
    cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    cfg.broker.address.hostname = host_.c_str();
    cfg.broker.address.port = port_;
    cfg.credentials.username = user_.empty() ? nullptr : user_.c_str();
    cfg.credentials.authentication.password =
        password_.empty() ? nullptr : password_.c_str();
    cfg.credentials.client_id =
        client_id_.empty() ? nullptr : client_id_.c_str();

    // Simple URI build if you prefer one field later:
    // cfg.broker.address.uri = "mqtt://host:1883";

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        client_, MQTT_EVENT_ANY, mqtt_event_handler, this));

    esp_err_t err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        return err;
    }

    started_ = true;
    ESP_LOGI(TAG, "MQTT client starting → %s:%u", host_.c_str(), (unsigned)port_);
    return ESP_OK;
}

esp_err_t HaspMqtt::stop_backend()
{
    if (!client_) {
        started_ = false;
        connected_ = false;
        return ESP_OK;
    }

    esp_mqtt_client_stop(client_);
    esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    started_ = false;
    connected_ = false;
    ESP_LOGI(TAG, "MQTT client stopped");
    return ESP_OK;
}

esp_err_t HaspMqtt::start()
{
    register_hasp_handlers();
    // For MVP: start when explicitly asked; network policy can call start_backend from on_network_up
    return start_backend();
}

esp_err_t HaspMqtt::stop()
{
    unregister_hasp_handlers();
    return stop_backend();
}

bool HaspMqtt::isRunning() const
{
    return started_;
}

esp_err_t HaspMqtt::publish(const char* topic, const char* data, int qos, bool retain)
{
    if (!client_ || !connected_ || !topic || !data) return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_publish(client_, topic, data, 0, qos, retain);
    return (msg_id < 0) ? ESP_FAIL : ESP_OK;
}

// ---------------------------------------------------------------------------
// HASP events (network)
// ---------------------------------------------------------------------------

void HaspMqtt::register_hasp_handlers()
{
    if (handlers_registered_) return;

    esp_event_handler_instance_register(
        HASP_EVENT, HASP_EVENT_CONNECTED,
        &HaspMqtt::hasp_event_handler, this, &hasp_conn_inst_);
    esp_event_handler_instance_register(
        HASP_EVENT, HASP_EVENT_DISCONNECTED,
        &HaspMqtt::hasp_event_handler, this, &hasp_disc_inst_);

    handlers_registered_ = true;
}

void HaspMqtt::unregister_hasp_handlers()
{
    if (!handlers_registered_) return;

    if (hasp_conn_inst_) {
        esp_event_handler_instance_unregister(HASP_EVENT, HASP_EVENT_CONNECTED, hasp_conn_inst_);
        hasp_conn_inst_ = nullptr;
    }
    if (hasp_disc_inst_) {
        esp_event_handler_instance_unregister(HASP_EVENT, HASP_EVENT_DISCONNECTED, hasp_disc_inst_);
        hasp_disc_inst_ = nullptr;
    }
    handlers_registered_ = false;
}

void HaspMqtt::hasp_event_handler(void* arg, esp_event_base_t, int32_t id, void*)
{
    auto* self = static_cast<HaspMqtt*>(arg);
    if (id == HASP_EVENT_CONNECTED) {
        self->on_network_up();
    } else if (id == HASP_EVENT_DISCONNECTED) {
        self->on_network_down();
    }
}

void HaspMqtt::on_network_up()
{
    // MVP: always (re)start client when IP is up
    // Later: gate with ServiceMode KeepAlive / Once / Manual
    start_backend();
}

void HaspMqtt::on_network_down()
{
    stop_backend();
}

// ---------------------------------------------------------------------------
// MQTT client events
// ---------------------------------------------------------------------------

void HaspMqtt::mqtt_event_handler(void* arg, esp_event_base_t, int32_t id, void* data)
{
    auto* self = static_cast<HaspMqtt*>(arg);
    auto* event = static_cast<esp_mqtt_event_handle_t>(data);

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        self->on_mqtt_connected();
        break;
    case MQTT_EVENT_DISCONNECTED:
        self->on_mqtt_disconnected();
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data %.*s = %.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

void HaspMqtt::on_mqtt_connected()
{
    connected_ = true;
    ESP_LOGI(TAG, "MQTT connected");

    // MVP heartbeat
    publish("hasp/status", "online", 0, true);
}

void HaspMqtt::on_mqtt_disconnected()
{
    connected_ = false;
    ESP_LOGW(TAG, "MQTT disconnected");
}