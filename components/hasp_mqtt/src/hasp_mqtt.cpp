#include "hasp_mqtt.hpp"
#include "hasp_event.hpp"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

// Step 4c: forward-decl instead of including "hasp_dispatch.h" — avoids a
// circular CMake dep (hasp component already REQUIRES hasp_mqtt for the 4b
// state-publish path). Symbol resolves at link time via main's REQUIRES hasp.
extern "C" void hasp_dispatch_command(const char* line);

static const char* TAG = "HASP_MQTT";

// Singleton pointer for the C-callable state publisher (step 4b).
// Set in start_backend after topics are built; cleared in stop_backend.
// hasp component publishes via hasp_mqtt_publish_state() without knowing
// anything about ServiceManager or HaspMqtt storage layout.
static HaspMqtt* s_instance = nullptr;

// S3 topic layout: hasp/<hostname>/{LWT,state/pXbY,command,...}.
// Prefix + subtopics are constants; hostname is discovered at start_backend
// from the netif that HaspWifi configured.
static constexpr const char* MQTT_PREFIX      = "hasp";
static constexpr const char* MQTT_TOPIC_LWT   = "LWT";
static constexpr const char* MQTT_TOPIC_CMD   = "command";
static constexpr const char* MQTT_LWT_ONLINE  = "online";
static constexpr const char* MQTT_LWT_OFFLINE = "offline";
// Step 4d/7A: S3 hasp_conf.h defines MQTT_GROUPNAME "plates" and
// MQTT_TOPIC_BROADCAST "broadcast". Group name is now runtime-configurable
// via mqtt.group (see set_config); this constant is the compile-time fallback
// used only when NVS/config.json have no value. Broadcast is a fixed
// hasp/broadcast/command topic shared by every plate on the broker.
static constexpr const char* MQTT_GROUPNAME       = "plates";
static constexpr const char* MQTT_TOPIC_BROADCAST = "broadcast";

// Step 4c: downlink command queue capacity. S3 uses 64 (mqttSetup:
// xQueueCreate(64, sizeof(mqtt_message_t))). Same value here so behaviour
// under bursts (mass jsonl push from HA) is identical.
static constexpr UBaseType_t CMD_QUEUE_LEN = 64;

// Queued MQTT command (mirrors S3 mqtt_message_t in hasp_mqtt_esp.cpp:33).
// topic and payload are heap-alloc'd C strings (owned by the message);
// drainer frees both after dispatch.
struct mqtt_cmd_msg_t {
    char* topic;         // subtopic AFTER "hasp/<host>/command" prefix, "" if bare
    char* payload;       // NUL-terminated payload
    const char* source;  // static string: "node" | "group" | "bcast" (log-only, step 4d)
};

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

    std::string group;
    if (!group_.empty()) group = group_;
    else nvs_get_string("group", group);

    mqtt["host"]      = host;
    mqtt["port"]      = port;
    mqtt["user"]      = user;
    mqtt["password"]  = "******";
    mqtt["client_id"] = client_id;
    mqtt["group"]     = group;

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
    // Step 7A: mqtt.group -> group_prefix_ at start_backend. S3 puts group
    // in the mqtt section (mqttSetConfig FP_CONFIG_GROUP_TOPIC).
    if (mqtt["group"].is<const char*>()) {
        group_ = mqtt["group"].as<std::string>();
        nvs_set_string("group", group_);
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
    nvs_get_string("group", group_);

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

    // ------------------------------------------------------------------
    // Build hostname-derived strings (S3 hasp_mqtt_esp.cpp::mqttStart).
    // The netif hostname was set by HaspWifi::start_backend before us.
    // ------------------------------------------------------------------
    const char* netif_hostname = nullptr;
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta) {
        esp_netif_get_hostname(sta, &netif_hostname);
    }
    std::string hostname;
    if (netif_hostname && *netif_hostname) {
        hostname = netif_hostname;
    } else if (!client_id_.empty()) {
        hostname = client_id_;       // config fallback
    } else {
        hostname = "plate";          // final fallback (must never leak in prod)
    }

    // "<hostname>_<last3bytes_mac>" — S3 uses last 3 MAC bytes as a stable
    // per-device suffix so multiple plates on one broker don't collide.
    uint8_t mac[6] = {};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char mac_suffix[8];
    snprintf(mac_suffix, sizeof(mac_suffix), "%02x%02x%02x",
             mac[3], mac[4], mac[5]);
    full_client_id_ = hostname + "_" + mac_suffix;

    // "hasp/<hostname>/LWT"
    lwt_topic_ = std::string(MQTT_PREFIX) + "/" + hostname + "/" + MQTT_TOPIC_LWT;
    // "hasp/<hostname>/state/" — trailing slash so callers concat subtopic
    // directly (S3 mqttNodeStateTopic is built the same way with add_slash=true
    // in mqttParseTopic; see hasp_mqtt_esp.cpp:549).
    state_prefix_ = std::string(MQTT_PREFIX) + "/" + hostname + "/state/";
    // "hasp/<hostname>/command" — NO trailing slash. Subscribe uses
    // "<prefix>/#" (matches parent + all subs per MQTT spec 4.7).
    // Strip in MQTT_EVENT_DATA leaves "" (bare command) or "/subtopic".
    command_prefix_ = std::string(MQTT_PREFIX) + "/" + hostname + "/" + MQTT_TOPIC_CMD;
    // Step 4d/7A: parallel prefixes for group + broadcast command topics.
    // Same layout as command_prefix_ (no trailing slash); subscribe with "/#"
    // and MQTT_EVENT_DATA strips the prefix the same way for all three.
    // Group name from config (mqtt.group), compile-time MQTT_GROUPNAME fallback.
    const char* gname = group_.empty() ? MQTT_GROUPNAME : group_.c_str();
    group_prefix_     = std::string(MQTT_PREFIX) + "/" + gname + "/" + MQTT_TOPIC_CMD;
    broadcast_prefix_ = std::string(MQTT_PREFIX) + "/" + MQTT_TOPIC_BROADCAST + "/" + MQTT_TOPIC_CMD;

    ESP_LOGI(TAG, "client_id=%s  lwt=%s  state=%s*  cmd=%s[/#]  grp=%s[/#]  bcast=%s[/#]",
             full_client_id_.c_str(), lwt_topic_.c_str(),
             state_prefix_.c_str(), command_prefix_.c_str(),
             group_prefix_.c_str(), broadcast_prefix_.c_str());

    // Step 4c: create the downlink queue on first start; keep it alive across
    // reconnects (S3 mqttSetup creates it once at boot). Draining is idempotent
    // when the queue is empty, so leaving it live during disconnect is safe.
    if (!cmd_queue_) {
        cmd_queue_ = xQueueCreate(CMD_QUEUE_LEN, sizeof(mqtt_cmd_msg_t));
        if (!cmd_queue_) {
            ESP_LOGE(TAG, "cmd queue alloc failed");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = nullptr;  // use hostname + port
    cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    cfg.broker.address.hostname = host_.c_str();
    cfg.broker.address.port = port_;
    cfg.credentials.username = user_.empty() ? nullptr : user_.c_str();
    cfg.credentials.authentication.password =
        password_.empty() ? nullptr : password_.c_str();
    cfg.credentials.client_id = full_client_id_.c_str();

    // LWT: broker publishes hasp/<hostname>/LWT="offline" retained when we
    // drop. On (re)connect we publish "online" ourselves (see
    // on_mqtt_connected). qos=1 matches S3 (guarantees LWT delivery).
    cfg.session.last_will.topic  = lwt_topic_.c_str();
    cfg.session.last_will.msg    = MQTT_LWT_OFFLINE;
    cfg.session.last_will.qos    = 1;
    cfg.session.last_will.retain = 1;

    // S3 tuning (see mqttStart in hasp_mqtt_esp.cpp).
    cfg.session.disable_clean_session = true;   // keep subs across reconnect
    cfg.session.keepalive             = 15;     // seconds; default 120 is too long
    cfg.network.reconnect_timeout_ms  = 5000;
    cfg.buffer.size                   = 2048;   // room for HA discovery payloads
    cfg.buffer.out_size               = 512;    // matches S3 out_buffer_size
    cfg.task.priority                 = 1;

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
    s_instance = this; // enable hasp_mqtt_publish_state()
    ESP_LOGI(TAG, "MQTT client starting → %s:%u", host_.c_str(), (unsigned)port_);
    return ESP_OK;
}

esp_err_t HaspMqtt::stop_backend()
{
    if (s_instance == this) s_instance = nullptr;

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

esp_err_t HaspMqtt::publish_state(const char* subtopic, const char* payload)
{
    if (!subtopic || !payload) return ESP_ERR_INVALID_ARG;
    if (state_prefix_.empty()) return ESP_ERR_INVALID_STATE;
    // Build "hasp/<hostname>/state/<subtopic>" on stack (bounded — hasp
    // subtopics are pXbY or short pagename.bN, always well under 64 bytes).
    char topic[128];
    int n = snprintf(topic, sizeof(topic), "%s%s",
                     state_prefix_.c_str(), subtopic);
    if (n <= 0 || n >= (int)sizeof(topic)) return ESP_ERR_INVALID_SIZE;
    // S3 defaults: qos=0, retain=false (mqtt_send_state signature).
    return publish(topic, payload, 0, false);
}

// C-callable free function — accessible from hasp component without pulling
// in ServiceManager or C++ singleton machinery. No-op when MQTT is not up so
// callers (event handlers) can fire regardless of connection state.
extern "C" int hasp_mqtt_publish_state(const char* subtopic, const char* payload)
{
    HaspMqtt* self = s_instance;
    if (!self) return ESP_ERR_INVALID_STATE;
    return self->publish_state(subtopic, payload);
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
    case MQTT_EVENT_DATA: {
        // Step 4c/4d: try node → group → broadcast prefix in that order
        // (S3 hasp_mqtt_pubsubclient.cpp:141-166 mqttCallback). Order matters
        // for logs only — MQTT broker guarantees per-topic dispatch, but a
        // subscription to hasp/plates/command/# on a device whose hostname is
        // literally "plates" would match both node and group; node wins,
        // which matches S3 behavior.
        if (self->match_and_enqueue(self->command_prefix_,   "node",
                                    event->topic, event->topic_len,
                                    event->data, event->data_len)) {
            // matched node
        } else if (self->match_and_enqueue(self->group_prefix_, "group",
                                          event->topic, event->topic_len,
                                          event->data, event->data_len)) {
            // matched group
        } else if (self->match_and_enqueue(self->broadcast_prefix_, "bcast",
                                          event->topic, event->topic_len,
                                          event->data, event->data_len)) {
            // matched broadcast
        } else {
            ESP_LOGW(TAG, "MQTT unmatched topic %.*s",
                     event->topic_len, event->topic);
        }
        break;
    }
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

    // Retained "online" cancels the retained LWT for external consumers.
    // qos=1 mirrors LWT so external subscribers see the toggle reliably.
    if (!lwt_topic_.empty()) {
        publish(lwt_topic_.c_str(), MQTT_LWT_ONLINE, 1, true);
    }

    // Step 4c/4d: subscribe to node + group + broadcast command topics.
    // Wildcard "#" matches parent too, so a single subscribe covers both bare
    // and sub-topics per MQTT spec 4.7 (S3 mqttSubscribeTo pattern in
    // onMqttConnect / hasp_mqtt_pubsubclient.cpp:302-323). Broker keeps
    // subscriptions across reconnects because we set disable_clean_session=
    // true, but the esp-mqtt client resends SUBSCRIBE on each CONNECT
    // anyway — resub here is safe (broker returns SUBACK either way).
    if (client_) {
        auto sub = [this](const std::string& prefix) {
            if (prefix.empty()) return;
            std::string wildcard = prefix + "/#";
            int msg_id = esp_mqtt_client_subscribe(client_, wildcard.c_str(), 0);
            if (msg_id < 0) {
                ESP_LOGE(TAG, "subscribe failed: %s", wildcard.c_str());
            } else {
                ESP_LOGI(TAG, "subscribed: %s (msg_id=%d)", wildcard.c_str(), msg_id);
            }
        };
        sub(command_prefix_);
        sub(group_prefix_);
        sub(broadcast_prefix_);
    }
}

void HaspMqtt::on_mqtt_disconnected()
{
    connected_ = false;
    ESP_LOGW(TAG, "MQTT disconnected");
}

// ---------------------------------------------------------------------------
// Downlink command queue (step 4c)
// ---------------------------------------------------------------------------

bool HaspMqtt::match_and_enqueue(const std::string& prefix, const char* source,
                                 const char* topic, int topic_len,
                                 const char* payload, int payload_len)
{
    // Step 4d: shared prefix-strip used for node/group/broadcast command
    // topics. esp-mqtt does NOT NUL-terminate event->topic / event->data —
    // caller passes explicit lengths. Returns true if the topic matched
    // this prefix (both the "bare" == prefix and "prefix/subtopic" cases).
    int plen = (int)prefix.size();
    if (plen == 0 || topic_len < plen) return false;
    if (memcmp(topic, prefix.data(), plen) != 0) return false;
    if (topic_len != plen && topic[plen] != '/') return false;

    const char* subtopic = topic + plen;
    int         sub_len  = topic_len - plen;
    // Skip leading '/' so "" == bare command, "page" / "p1b2.text" for subs
    // (S3 hasp_mqtt_esp.cpp:289 does the same).
    if (sub_len > 0 && *subtopic == '/') { subtopic++; sub_len--; }
    char sub_buf[96];
    if (sub_len >= (int)sizeof(sub_buf)) sub_len = sizeof(sub_buf) - 1;
    memcpy(sub_buf, subtopic, sub_len);
    sub_buf[sub_len] = '\0';
    enqueue_command(sub_buf, payload, payload_len, source);
    return true;
}

void HaspMqtt::enqueue_command(const char* subtopic, const char* payload,
                               int payload_len, const char* source)
{
    if (!cmd_queue_) return;
    if (!subtopic) subtopic = "";
    if (!payload || payload_len < 0) { payload = ""; payload_len = 0; }

    mqtt_cmd_msg_t msg = {};
    msg.source = source ? source : "node";
    size_t tlen = strlen(subtopic);
    msg.topic   = (char*)malloc(tlen + 1);
    msg.payload = (char*)malloc((size_t)payload_len + 1);
    if (!msg.topic || !msg.payload) {
        free(msg.topic);
        free(msg.payload);
        ESP_LOGE(TAG, "enqueue oom (topic=%s len=%d)", subtopic, payload_len);
        return;
    }
    memcpy(msg.topic, subtopic, tlen);
    msg.topic[tlen] = '\0';
    memcpy(msg.payload, payload, payload_len);
    msg.payload[payload_len] = '\0';

    // Non-blocking send. If the queue is full (drainer starved), drop the
    // OLDEST entry to make room — S3 blocks up to 500ms retrying, but here
    // we're on the esp-mqtt task which must not stall (heartbeat loss →
    // reconnect storm). Losing a stale command is preferable.
    if (xQueueSend(cmd_queue_, &msg, 0) != pdTRUE) {
        mqtt_cmd_msg_t drop = {};
        if (xQueueReceive(cmd_queue_, &drop, 0) == pdTRUE) {
            ESP_LOGW(TAG, "cmd queue full, dropped oldest %s", drop.topic);
            free(drop.topic);
            free(drop.payload);
        }
        if (xQueueSend(cmd_queue_, &msg, 0) != pdTRUE) {
            free(msg.topic);
            free(msg.payload);
            ESP_LOGE(TAG, "cmd queue send failed");
        }
    }
}

// Drainer — call from LVGL task (lv_timer). Reconstructs a text command
// line and hands it to hasp_dispatch_command:
//   subtopic == ""              → payload as-is (e.g. "page 2", "{...}", "[...]")
//   subtopic contains '.'       → "<subtopic>=<payload>"  (attribute set,
//                                  e.g. "p1b2.text=Hello")
//   subtopic otherwise          → "<subtopic> <payload>"  (verb form,
//                                  e.g. "page 2")
// hasp_dispatch_command already splits on the first '=' or ' ' (S3 line
// 404-420), so both forms round-trip through the same code path used by
// the local text-command tests in main.cpp.
int HaspMqtt::drain_command_queue()
{
    if (!cmd_queue_) return 0;

    int dispatched = 0;
    mqtt_cmd_msg_t msg;
    while (xQueueReceive(cmd_queue_, &msg, 0) == pdTRUE) {
        const char* subtopic = msg.topic ? msg.topic : "";
        const char* payload  = msg.payload ? msg.payload : "";

        ESP_LOGI(TAG, "cmd rx [%s]  topic='%s'  payload='%s'",
                 msg.source ? msg.source : "?", subtopic, payload);

        if (subtopic[0] == '\0') {
            hasp_dispatch_command(payload);
        } else {
            size_t sn = strlen(subtopic);
            size_t pn = strlen(payload);
            std::string line;
            line.reserve(sn + 1 + pn);
            line.assign(subtopic, sn);
            line.push_back(strchr(subtopic, '.') ? '=' : ' ');
            line.append(payload, pn);
            hasp_dispatch_command(line.c_str());
        }

        free(msg.topic);
        free(msg.payload);
        dispatched++;
    }
    return dispatched;
}

extern "C" int hasp_mqtt_process_incoming(void)
{
    HaspMqtt* self = s_instance;
    if (!self) return 0;
    return self->drain_command_queue();
}