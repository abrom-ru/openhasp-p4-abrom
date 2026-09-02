#include "hasp_http.hpp"
#include "hasp_config.hpp"
#include "hasp_dispatch.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_lvgl_port.h"

#include "mbedtls/base64.h"

#include <ArduinoJson.h>

#include <cstring>
#include <cstdio>
#include <map>
#include <string>

static const char *TAG = "HASP_HTTP";

// -----------------------------------------------------------------------------
// Step 7C-refactor: S3-mirror SSR building blocks.
// -----------------------------------------------------------------------------
//
// Layout matches openhasp-abrom/src/sys/svc/hasp_http_async.cpp: every page is
// assembled server-side into HTTP_DOCTYPE + <title>%s</title> + <link ...css>
// (+ optional meta-refresh) + HTTP_HEADER_END + body + HTTP_FOOTER + HTTP_END.
// Same names, same order, ported from PROGMEM to plain static const char[]
// (ESP-IDF has no flash-string wrapper).
//
// Passwords are masked as "********" (D_PASSWORD_MASK, 8 chars) — matches
// S3 lang/lang.h and is the exact literal the modules' set_config recognise
// as "leave unchanged".
static constexpr const char PW_MASK[] = "********";

// clang-format off
static const char HTTP_DOCTYPE[] =
    "<!DOCTYPE html><html lang=\"ru\"><head><meta charset='utf-8'>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"/>";
static const char HTTP_META_GO_BACK[] = "<meta http-equiv='refresh' content='5;url=/'/>";
static const char HTTP_STYLE[]        = "<link rel=\"stylesheet\" href=\"/css\">";
static const char HTTP_HEADER_END[]   = "</head><body><div id='doc'>";
static const char HTTP_FOOTER[]       =
    "<div style='text-align:right;font-size:11px;'><hr/>"
    "<a href='/console.html' style='color:#6cf'>console</a> &middot; openHASP P4</div>";
static const char HTTP_END[]          = "</body></html>";

static const char MAIN_MENU_BUTTON[]  =
    "<p><form method='GET' action='/'><button type='submit' class='plain'>&#8617; Главная</button></form></p>";
static const char BACK_TO_CONFIG[]    =
    "<p><form method='GET' action='/config'><button type='submit' class='plain'>&#8617; Настройки</button></form></p>";

// Dark theme + RU-friendly form styling. Direct port of data/storage/webui/style.css
// (removed in 7C-refactor) minimised into a single inline stylesheet the /css
// endpoint serves once and browsers cache.
static const char HTTP_CSS[] =
    "*{box-sizing:border-box}"
    "html,body{margin:0;padding:0;background:#111;color:#ddd;"
    "font-family:system-ui,-apple-system,sans-serif}"
    "body{max-width:640px;margin:0 auto;padding:24px 16px 48px;line-height:1.5}"
    "h1{font-size:20px;border-bottom:1px solid #333;padding-bottom:8px;margin:0 0 16px}"
    "h2{font-size:16px;color:#6cf;margin:24px 0 8px}"
    "hr{border:0;border-top:1px solid #333;margin:12px 0}"
    "nav{margin-bottom:16px;font-size:14px}"
    "nav a{color:#6cf;text-decoration:none;margin-right:12px}"
    "nav a:hover{text-decoration:underline}"
    "form{display:block;margin:0 0 12px 0}"
    "form p{margin:8px 0}"
    "label,b{display:block;font-size:13px;color:#aaa;margin:8px 0 4px}"
    "small{color:#888;font-weight:normal;margin-left:6px}"
    "input[type=text],input[type=password],input[type=number]{"
    "width:100%;padding:8px 10px;border:1px solid #333;background:#1a1a1a;"
    "color:#fff;border-radius:4px;font:inherit;font-size:14px}"
    "input:focus{outline:none;border-color:#6cf}"
    "select{width:100%;padding:8px 10px;background:#1a1a1a;color:#fff;"
    "border:1px solid #333;border-radius:4px;font:inherit;font-size:14px}"
    "button{padding:10px 16px;border:0;border-radius:4px;background:#2b7;"
    "color:#fff;font:inherit;font-size:14px;font-weight:600;cursor:pointer;"
    "width:100%}"
    "button:hover{background:#38c}"
    "button.red{background:#b33}button.red:hover{background:#d44}"
    "button.plain{background:#333}button.plain:hover{background:#444}"
    "#doc{max-width:100%}"
    "pre{background:#1a1a1a;border:1px solid #333;padding:12px;"
    "border-radius:4px;overflow-x:auto;font-size:12px;color:#ddd}"
    ".ok{background:#163;color:#dfd;padding:6px 10px;border-radius:4px;margin:8px 0}"
    ".err{background:#611;color:#fdd;padding:6px 10px;border-radius:4px;margin:8px 0}";
// clang-format on

// -----------------------------------------------------------------------------
// Basic Auth (S3-mirror httpIsAuthenticated)
// -----------------------------------------------------------------------------
//
// Empty password_ ⇒ server is fully open (S3 default). Otherwise every
// protected request must carry `Authorization: Basic <b64>` with
// user:password matching the configured credentials.
esp_err_t HaspHttp::check_basic_auth(httpd_req_t *req) const
{
    if (password_.empty())
    {
        return ESP_OK;
    }

    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (hdr_len == 0)
    {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    char hdr[256];
    if (hdr_len >= sizeof(hdr))
    {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK)
    {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    const char *prefix = "Basic ";
    const size_t prefix_len = strlen(prefix);
    if (strncmp(hdr, prefix, prefix_len) != 0)
    {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    const unsigned char *b64 = reinterpret_cast<const unsigned char *>(hdr + prefix_len);
    size_t b64_len = strlen(reinterpret_cast<const char *>(b64));

    unsigned char decoded[192];
    size_t decoded_len = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len, b64, b64_len) != 0)
    {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }
    decoded[decoded_len] = '\0';

    char *colon = strchr(reinterpret_cast<char *>(decoded), ':');
    if (!colon)
    {
        return ESP_ERR_HTTPD_INVALID_REQ;
    }
    *colon = '\0';
    const char *req_user = reinterpret_cast<const char *>(decoded);
    const char *req_pass = colon + 1;

    if (user_ == req_user && password_ == req_pass)
    {
        return ESP_OK;
    }

    return ESP_ERR_HTTPD_INVALID_REQ;
}

esp_err_t HaspHttp::require_auth(httpd_req_t *req)
{
    HaspHttp *self = from_req(req);

    if (self->check_basic_auth(req) == ESP_OK)
    {
        return ESP_OK;
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"openHASP\"");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Authentication required", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
}

#define REQUIRE_AUTH(req)                          \
    do                                             \
    {                                              \
        if (HaspHttp::require_auth(req) != ESP_OK) \
            return ESP_FAIL;                       \
    } while (0)

HaspHttp::~HaspHttp()
{
    stop();
}

// -----------------------------------------------------------------------------
// Config (S3-mirror httpGetConfig / httpSetConfig)
// -----------------------------------------------------------------------------

esp_err_t HaspHttp::get_config(JsonObject obj) const
{
    JsonObject http = obj[name()].to<JsonObject>();

    std::string user, password;
    uint32_t port32 = port_;

    if (!user_.empty())
        user = user_;
    else
        nvs_get_string("user", user);

    if (!password_.empty())
    {
        password = password_;
    }
    else
    {
        nvs_get_string("password", password);
    }

    if (port_ == 80)
    {
        nvs_get_u32("port", port32);
    }

    http["user"] = user;
    http["password"] = password.empty() ? std::string() : std::string(PW_MASK);
    http["port"] = static_cast<uint16_t>(port32);

    return ESP_OK;
}

esp_err_t HaspHttp::set_config(JsonObjectConst obj)
{
    JsonObjectConst http = obj[name()] | obj;

    if (http["user"].is<const char *>())
    {
        user_ = http["user"].as<std::string>();
        nvs_set_string("user", user_);
    }

    if (http["password"].is<const char *>())
    {
        std::string pw = http["password"].as<std::string>();
        if (pw != PW_MASK)
        {
            password_ = pw;
            nvs_set_string("password", password_);
        }
        else if (password_.empty())
        {
            // S3-mirror: mask == "keep NVS secret"; hydrate from NVS now so
            // Basic Auth stays enforced after a config-only load path.
            nvs_get_string("password", password_);
        }
    }

    if (http["port"].is<uint16_t>() || http["port"].is<uint32_t>() || http["port"].is<int>())
    {
        uint32_t p = http["port"].as<uint32_t>();
        if (p > 0 && p < 65536)
        {
            port_ = static_cast<uint16_t>(p);
            nvs_set_u32("port", p);
        }
    }

    return ESP_OK;
}

esp_err_t HaspHttp::load_from_nvs()
{
    nvs_get_string("user", user_);
    nvs_get_string("password", password_);

    uint32_t p = 0;
    if (nvs_get_u32("port", p) == ESP_OK && p > 0 && p < 65536)
    {
        port_ = static_cast<uint16_t>(p);
    }
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Helpers (S3-mirror webSendPage + form/URL utils)
// -----------------------------------------------------------------------------

static std::string get_hostname()
{
    // hasp_mqtt already discovers this from the STA netif; reuse the same
    // path so /root prints the same node name as MQTT topics.
    const char *h = nullptr;
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta && esp_netif_get_hostname(sta, &h) == ESP_OK && h && *h)
    {
        return h;
    }
    return "openhasp-plate";
}

// Chunked send equivalent of S3 webSendPage(request, hostname, body, gohome).
static esp_err_t web_send_page(httpd_req_t *req,
                               const std::string &hostname,
                               const std::string &body,
                               bool gohome)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    // <!DOCTYPE ...>
    if (httpd_resp_send_chunk(req, HTTP_DOCTYPE, HTTPD_RESP_USE_STRLEN) != ESP_OK)
        return ESP_FAIL;

    // <title>%s</title>
    char title[96];
    int tn = snprintf(title, sizeof(title), "<title>%s</title>", hostname.c_str());
    if (tn > 0 && httpd_resp_send_chunk(req, title, tn) != ESP_OK)
        return ESP_FAIL;

    if (httpd_resp_send_chunk(req, HTTP_STYLE, HTTPD_RESP_USE_STRLEN) != ESP_OK)
        return ESP_FAIL;

    if (gohome)
    {
        if (httpd_resp_send_chunk(req, HTTP_META_GO_BACK, HTTPD_RESP_USE_STRLEN) != ESP_OK)
            return ESP_FAIL;
    }

    if (httpd_resp_send_chunk(req, HTTP_HEADER_END, HTTPD_RESP_USE_STRLEN) != ESP_OK)
        return ESP_FAIL;

    if (!body.empty())
    {
        if (httpd_resp_send_chunk(req, body.data(), body.size()) != ESP_OK)
            return ESP_FAIL;
    }

    if (httpd_resp_send_chunk(req, HTTP_FOOTER, HTTPD_RESP_USE_STRLEN) != ESP_OK)
        return ESP_FAIL;
    if (httpd_resp_send_chunk(req, "</div>", HTTPD_RESP_USE_STRLEN) != ESP_OK)
        return ESP_FAIL;
    if (httpd_resp_send_chunk(req, HTTP_END, HTTPD_RESP_USE_STRLEN) != ESP_OK)
        return ESP_FAIL;

    return httpd_resp_send_chunk(req, nullptr, 0);
}

// HTML-escape into an appended std::string. Guards user-visible values
// (hostname, form field defaults) so a `<` in NVS can't inject markup.
static void html_escape_append(std::string &out, const char *s)
{
    for (; *s; ++s)
    {
        switch (*s)
        {
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '&':  out += "&amp;";  break;
        case '\'': out += "&#39;";  break;
        case '"':  out += "&quot;"; break;
        default:   out += *s;       break;
        }
    }
}

static void html_escape_append(std::string &out, const std::string &s)
{
    html_escape_append(out, s.c_str());
}

// application/x-www-form-urlencoded → std::map<std::string,std::string>.
// Handles `+` → ` ` and `%XX` percent-decoding; malformed `%` sequences copy
// through unchanged (browsers never emit them).
static int hex_nib(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static void url_decode(const char *src, size_t len, std::string &out)
{
    out.clear();
    out.reserve(len);
    for (size_t i = 0; i < len; ++i)
    {
        char c = src[i];
        if (c == '+')
        {
            out += ' ';
        }
        else if (c == '%' && i + 2 < len)
        {
            int hi = hex_nib(src[i + 1]);
            int lo = hex_nib(src[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            }
            else
            {
                out += c;
            }
        }
        else
        {
            out += c;
        }
    }
}

static std::map<std::string, std::string>
parse_form_urlencoded(const char *body, size_t len)
{
    std::map<std::string, std::string> out;
    size_t i = 0;
    while (i < len)
    {
        size_t k_start = i;
        while (i < len && body[i] != '=' && body[i] != '&') ++i;
        std::string key;
        url_decode(body + k_start, i - k_start, key);

        std::string val;
        if (i < len && body[i] == '=')
        {
            ++i;
            size_t v_start = i;
            while (i < len && body[i] != '&') ++i;
            url_decode(body + v_start, i - v_start, val);
        }
        if (i < len && body[i] == '&') ++i;

        if (!key.empty()) out[key] = val;
    }
    return out;
}

// Build a JsonDocument for a single "save" target and hand it to
// mgr.set_config. Integer fields have to be coerced explicitly because
// service set_config uses `.is<uint32_t>()` — an ArduinoJson value set from
// std::string comes back as string only.
static void save_section(ServiceManager &mgr,
                         const std::string &section,
                         const std::map<std::string, std::string> &form)
{
    JsonDocument doc;
    JsonObject sec = doc[section].to<JsonObject>();

    // Fields that must be numeric per each module's set_config.
    static const std::map<std::string, std::vector<std::string>> INT_FIELDS = {
        {"mqtt", {"port", "teleperiod"}},
        {"hasp", {"theme", "startpage"}},
        {"http", {"port"}},
    };

    const auto ints_it = INT_FIELDS.find(section);
    const std::vector<std::string> *ints =
        (ints_it != INT_FIELDS.end()) ? &ints_it->second : nullptr;

    for (const auto &kv : form)
    {
        if (kv.first == "save") continue;

        bool is_int = false;
        if (ints)
        {
            for (const auto &f : *ints)
            {
                if (f == kv.first) { is_int = true; break; }
            }
        }

        if (is_int)
        {
            // strtoul handles leading whitespace and rejects empty — treat
            // empty as "field not submitted" to avoid clobbering NVS with 0.
            if (!kv.second.empty())
            {
                char *end = nullptr;
                unsigned long v = strtoul(kv.second.c_str(), &end, 10);
                if (end != kv.second.c_str())
                {
                    sec[kv.first] = static_cast<uint32_t>(v);
                }
            }
        }
        else
        {
            sec[kv.first] = kv.second;
        }
    }

    mgr.set_config(doc.as<JsonObjectConst>());
}

// POST /config handler branch: parse form, dispatch by "save" field, persist.
static esp_err_t handle_save(httpd_req_t *req, ServiceManager &mgr)
{
    int total = req->content_len;
    if (total <= 0 || total > 4096)
    {
        return ESP_OK; // nothing to save (S3 saveConfig is a no-op when body absent)
    }

    std::string body;
    body.resize(total);
    int off = 0;
    while (off < total)
    {
        int n = httpd_req_recv(req, body.data() + off, total - off);
        if (n <= 0) return ESP_FAIL;
        off += n;
    }

    auto form = parse_form_urlencoded(body.data(), body.size());
    auto save_it = form.find("save");
    if (save_it == form.end() || save_it->second.empty())
    {
        return ESP_OK;
    }

    const std::string &target = save_it->second;
    if (target == "wifi" || target == "mqtt" || target == "hasp" || target == "http")
    {
        save_section(mgr, target, form);
        // S3-mirror: persist to /littlefs/config.json every time. Passwords
        // are masked by each service's get_config so the on-flash file is
        // safe to leak.
        hasp_config_save(mgr);
        ESP_LOGI(TAG, "Saved section: %s", target.c_str());
    }
    else
    {
        ESP_LOGW(TAG, "Unknown save target: %s", target.c_str());
    }

    return ESP_OK;
}

// Common H1 with device name.
static void append_h1(std::string &out, const std::string &hostname)
{
    out += "<h1>";
    html_escape_append(out, hostname);
    out += "</h1><hr>";
}

// -----------------------------------------------------------------------------
// GET / — main menu (S3 webHandleRoot)
// -----------------------------------------------------------------------------
esp_err_t HaspHttp::root_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);

    std::string host = get_hostname();
    std::string body;
    body.reserve(1024);

    append_h1(body, host);

    body += "<p><form method='GET' action='/info'>"
            "<button type='submit'>Информация (JSON)</button></form></p>";
    body += "<p><form method='GET' action='/config'>"
            "<button type='submit'>Настройки</button></form></p>";
    body += "<p><form method='GET' action='/console.html'>"
            "<button type='submit' class='plain'>Консоль</button></form></p>";
    body += "<p><form method='GET' action='/reboot'>"
            "<button type='submit' class='red' "
            "onclick=\"return confirm('Перезагрузить плату?');\">"
            "Перезагрузка</button></form></p>";

    return web_send_page(req, host, body, false);
}

// -----------------------------------------------------------------------------
// GET/POST /config — sub-menu (+ save handler)
// -----------------------------------------------------------------------------
esp_err_t HaspHttp::config_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);

    std::string host = get_hostname();
    std::string body;
    body.reserve(768);
    append_h1(body, host);

    body += "<p><form method='GET' action='/config/wifi'>"
            "<button type='submit'>Wi-Fi</button></form></p>";
    body += "<p><form method='GET' action='/config/mqtt'>"
            "<button type='submit'>MQTT</button></form></p>";
    body += "<p><form method='GET' action='/config/hasp'>"
            "<button type='submit'>HASP</button></form></p>";
    body += "<p><form method='GET' action='/config/http'>"
            "<button type='submit'>HTTP</button></form></p>";

    body += MAIN_MENU_BUTTON;

    return web_send_page(req, host, body, false);
}

esp_err_t HaspHttp::config_post_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    HaspHttp *self = from_req(req);

    if (handle_save(req, self->mgr_) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Save failed");
        return ESP_FAIL;
    }

    // S3 renders the config menu again after a POST — the user sees the
    // save land and can navigate elsewhere. Meta-refresh brings them back
    // to `/` after 5 s (see HTTP_META_GO_BACK).
    std::string host = get_hostname();
    std::string body;
    body.reserve(384);
    append_h1(body, host);
    body += "<div class='ok'>Сохранено.</div>";
    body += "<p><form method='GET' action='/config'>"
            "<button type='submit'>&#8617; Настройки</button></form></p>";
    body += MAIN_MENU_BUTTON;

    return web_send_page(req, host, body, true);
}

// -----------------------------------------------------------------------------
// GET /config/wifi (S3 webHandleWifiConfig)
// -----------------------------------------------------------------------------
esp_err_t HaspHttp::wifi_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    HaspHttp *self = from_req(req);

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    self->mgr_.get_config(obj);
    JsonObject wifi = obj["wifi"];

    const char *ssid     = wifi["ssid"]     | "";
    const char *hostname = wifi["hostname"] | "";
    const char *pw       = wifi["password"] | "";
    bool pw_set = (pw && pw[0] != '\0');

    std::string host = get_hostname();
    std::string body;
    body.reserve(1024);
    append_h1(body, host);

    body += "<form method='POST' action='/config'>";

    body += "<p><b>SSID</b> <small>(обязательно)</small>";
    body += "<input id='ssid' name='ssid' type='text' required maxlength='31' value='";
    html_escape_append(body, ssid);
    body += "'></p>";

    body += "<p><b>Пароль</b> <small>(пусто = не менять)</small>";
    body += "<input id='password' name='password' type='password' maxlength='63' placeholder='";
    body += pw_set ? PW_MASK : "";
    body += "' value=''></p>";

    body += "<p><b>Hostname</b>"
            "<input id='hostname' name='hostname' type='text' maxlength='31' value='";
    html_escape_append(body, hostname);
    body += "'></p>";

    body += "<p><button type='submit' name='save' value='wifi'>Сохранить</button></p></form>";
    body += BACK_TO_CONFIG;

    return web_send_page(req, host, body, false);
}

// -----------------------------------------------------------------------------
// GET /config/mqtt (S3 webHandleMqttConfig)
// -----------------------------------------------------------------------------
esp_err_t HaspHttp::mqtt_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    HaspHttp *self = from_req(req);

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    self->mgr_.get_config(obj);
    JsonObject m = obj["mqtt"];

    const char *mhost      = m["host"]       | "";
    uint32_t   port        = m["port"]       | 1883U;
    const char *client_id  = m["client_id"]  | "";
    const char *user       = m["user"]       | "";
    const char *pw         = m["password"]   | "";
    const char *group      = m["group"]      | "";
    uint32_t   teleperiod  = m["teleperiod"] | 300U;
    bool pw_set = (pw && pw[0] != '\0');

    std::string hn = get_hostname();
    std::string body;
    body.reserve(1536);
    append_h1(body, hn);

    body += "<form method='POST' action='/config'>";

    body += "<p><b>Брокер (host)</b> <small>(обязательно)</small>";
    body += "<input id='host' name='host' type='text' required maxlength='63' value='";
    html_escape_append(body, mhost);
    body += "'></p>";

    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)port);
    body += "<p><b>Порт</b>"
            "<input id='port' name='port' type='number' min='1' max='65535' required value='";
    body += buf;
    body += "'></p>";

    body += "<p><b>Client ID</b>"
            "<input id='client_id' name='client_id' type='text' maxlength='31' value='";
    html_escape_append(body, client_id);
    body += "'></p>";

    body += "<p><b>Пользователь</b>"
            "<input id='user' name='user' type='text' maxlength='31' value='";
    html_escape_append(body, user);
    body += "'></p>";

    body += "<p><b>Пароль</b> <small>(пусто = не менять)</small>";
    body += "<input id='password' name='password' type='password' maxlength='63' placeholder='";
    body += pw_set ? PW_MASK : "";
    body += "' value=''></p>";

    body += "<p><b>Группа</b>"
            "<input id='group' name='group' type='text' maxlength='31' value='";
    html_escape_append(body, group);
    body += "'></p>";

    snprintf(buf, sizeof(buf), "%u", (unsigned)teleperiod);
    body += "<p><b>Teleperiod</b> <small>(сек, 0=выкл)</small>"
            "<input id='teleperiod' name='teleperiod' type='number' min='0' max='65535' value='";
    body += buf;
    body += "'></p>";

    body += "<p><button type='submit' name='save' value='mqtt'>Сохранить</button></p></form>";
    body += BACK_TO_CONFIG;

    return web_send_page(req, hn, body, false);
}

// -----------------------------------------------------------------------------
// GET /config/hasp (S3 webHandleHaspConfig — theme + startpage subset)
// -----------------------------------------------------------------------------
esp_err_t HaspHttp::hasp_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    HaspHttp *self = from_req(req);

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    self->mgr_.get_config(obj);
    JsonObject h = obj["hasp"];

    uint32_t theme     = h["theme"]     | 2U;
    uint32_t startpage = h["startpage"] | 1U;

    auto opt = [&](std::string &out, int val, const char *label, bool sel) {
        char b[8];
        snprintf(b, sizeof(b), "%d", val);
        out += "<option value='";
        out += b;
        out += "'";
        if (sel) out += " selected";
        out += ">";
        out += label;
        out += "</option>";
    };

    std::string host = get_hostname();
    std::string body;
    body.reserve(1024);
    append_h1(body, host);

    body += "<form method='POST' action='/config'>";

    body += "<p><b>Тема</b>"
            "<select id='theme' name='theme'>";
    opt(body, 0, "0 — default LVGL",   theme == 0);
    opt(body, 1, "1 — simple",         theme == 1);
    opt(body, 2, "2 — dark (по умолчанию)", theme == 2);
    body += "</select></p>";

    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)startpage);
    body += "<p><b>Стартовая страница</b> <small>(1..12)</small>"
            "<input id='startpage' name='startpage' type='number' min='1' max='12' required value='";
    body += buf;
    body += "'></p>";

    body += "<p><button type='submit' name='save' value='hasp'>Сохранить</button></p></form>";
    body += BACK_TO_CONFIG;

    return web_send_page(req, host, body, false);
}

// -----------------------------------------------------------------------------
// GET /config/http (S3 webHandleHttpConfig)
// -----------------------------------------------------------------------------
esp_err_t HaspHttp::http_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    HaspHttp *self = from_req(req);

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    self->mgr_.get_config(obj);
    JsonObject h = obj["http"];

    const char *user = h["user"]     | "";
    const char *pw   = h["password"] | "";
    uint32_t    port = h["port"]     | 80U;
    bool pw_set = (pw && pw[0] != '\0');

    std::string host = get_hostname();
    std::string body;
    body.reserve(1024);
    append_h1(body, host);

    body += "<form method='POST' action='/config'>";

    body += "<p><b>Пользователь</b>"
            "<input id='user' name='user' type='text' maxlength='31' "
            "autocomplete='username' value='";
    html_escape_append(body, user);
    body += "'></p>";

    body += "<p><b>Пароль</b> <small>(пусто = без авторизации)</small>";
    body += "<input id='password' name='password' type='password' maxlength='63' "
            "autocomplete='new-password' placeholder='";
    body += pw_set ? PW_MASK : "";
    body += "' value=''></p>";

    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)port);
    body += "<p><b>Порт</b> <small>(требуется перезагрузка)</small>"
            "<input id='port' name='port' type='number' min='1' max='65535' required value='";
    body += buf;
    body += "'></p>";

    body += "<p><button type='submit' name='save' value='http'>Сохранить</button></p></form>";
    body += BACK_TO_CONFIG;

    return web_send_page(req, host, body, false);
}

// -----------------------------------------------------------------------------
// GET /info — statusupdate snapshot (unchanged from 7C-4)
// -----------------------------------------------------------------------------
//
// Same JSON body that gets pushed to hasp/<host>/state/statusupdate every
// teleperiod seconds (S3 webHandleInfoJson). LVGL lock protects
// hasp_dispatch_build_statusupdate_json (reads lv_display_get_default).
esp_err_t HaspHttp::info_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);

    char buf[400];
    size_t n = 0;

    if (lvgl_port_lock(200))
    {
        n = hasp_dispatch_build_statusupdate_json(buf, sizeof(buf));
        lvgl_port_unlock();
    }
    else
    {
        n = hasp_dispatch_build_statusupdate_json(buf, sizeof(buf));
    }

    if (n == 0)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "statusupdate build failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// GET /reboot (S3 httpHandleReboot) — render page, restart after 1 s.
// -----------------------------------------------------------------------------
static void reboot_timer_cb(void * /*arg*/)
{
    esp_restart();
}

esp_err_t HaspHttp::reboot_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);

    std::string host = get_hostname();
    std::string body;
    body.reserve(256);
    append_h1(body, host);
    body += "<h2>Перезагрузка…</h2><p>Плата уходит в reset через 1 с.</p>";
    body += "<p><small>Страница обновится через 5 с.</small></p>";

    esp_err_t rc = web_send_page(req, host, body, true);

    static esp_timer_handle_t s_reboot_timer = nullptr;
    if (!s_reboot_timer)
    {
        esp_timer_create_args_t args = {};
        args.callback = &reboot_timer_cb;
        args.name = "http_reboot";
        esp_timer_create(&args, &s_reboot_timer);
    }
    if (s_reboot_timer)
    {
        esp_timer_start_once(s_reboot_timer, 1000000ULL);
    }

    ESP_LOGW(TAG, "Reboot requested via HTTP; restarting in 1 s");
    return rc;
}

// -----------------------------------------------------------------------------
// GET /css — shared stylesheet (S3 has a dedicated /css endpoint too).
// -----------------------------------------------------------------------------
esp_err_t HaspHttp::css_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, HTTP_CSS, HTTPD_RESP_USE_STRLEN);
}

// -----------------------------------------------------------------------------
// GET /console.html — still served from LittleFS (S3 has a dedicated console
// page too; not worth inlining as a PSTR).
// -----------------------------------------------------------------------------
esp_err_t HaspHttp::console_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);

    httpd_resp_set_type(req, "text/html");

    FILE *f = fopen("/littlefs/webui/console.html", "r");
    if (!f)
    {
        ESP_LOGE(TAG, "Failed to open /littlefs/webui/console.html");
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "console.html not found");
        return ESP_FAIL;
    }

    char buf[1024];
    while (true)
    {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n > 0)
        {
            esp_err_t err = httpd_resp_send_chunk(req, buf, n);
            if (err != ESP_OK)
            {
                fclose(f);
                return err;
            }
        }
        if (n < sizeof(buf))
        {
            if (ferror(f))
            {
                ESP_LOGE(TAG, "Error reading console.html");
                fclose(f);
                return ESP_FAIL;
            }
            break;
        }
    }

    fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

// -----------------------------------------------------------------------------
// WebSocket /ws (unchanged)
// -----------------------------------------------------------------------------
esp_err_t HaspHttp::ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI("WS", "WebSocket handshake complete");
        return ESP_OK;
    }

    ESP_LOGI("WS", "WebSocket frame received");
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK)
    {
        return err;
    }

    if (frame.len == 0)
    {
        return ESP_OK;
    }

    frame.payload = static_cast<uint8_t *>(malloc(frame.len + 1));
    if (!frame.payload)
    {
        return ESP_ERR_NO_MEM;
    }

    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err == ESP_OK)
    {
        ((char *)frame.payload)[frame.len] = '\0';

        ESP_LOGI("WS", "RX: %s", (char *)frame.payload);

        frame.type = HTTPD_WS_TYPE_TEXT;
        err = httpd_ws_send_frame(req, &frame);
    }

    free(frame.payload);
    return err;
}

// -----------------------------------------------------------------------------
// Start / Stop
// -----------------------------------------------------------------------------
esp_err_t HaspHttp::start_backend()
{
    if (server_)
        return ESP_OK;

    if (user_.empty() && password_.empty())
    {
        load_from_nvs();
    }

    log_memory();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    config.lru_purge_enable = true;
    // 7C-4 bugfix: default httpd stack (4 KB) overflows LittleFS commit
    // chain (lfs_dir_traverse recursion + esp_flash_read lock) when a POST
    // writes config.json. 8 KB comfortably fits.
    config.stack_size = 8192;
    // IDF v6.1 dropped CONFIG_HTTPD_MAX_URI_HANDLERS — bump at runtime for 12 routes.
    config.max_uri_handlers = 16;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    struct Route
    {
        const char *uri;
        httpd_method_t method;
        esp_err_t (*handler)(httpd_req_t *);
        bool ws;
    };

    const Route routes[] = {
        {"/",             HTTP_GET,  root_get_handler,    false},
        {"/config",       HTTP_GET,  config_get_handler,  false},
        {"/config",       HTTP_POST, config_post_handler, false},
        {"/config/wifi",  HTTP_GET,  wifi_get_handler,    false},
        {"/config/mqtt",  HTTP_GET,  mqtt_get_handler,    false},
        {"/config/hasp",  HTTP_GET,  hasp_get_handler,    false},
        {"/config/http",  HTTP_GET,  http_get_handler,    false},
        {"/info",         HTTP_GET,  info_get_handler,    false},
        {"/reboot",       HTTP_GET,  reboot_get_handler,  false},
        {"/css",          HTTP_GET,  css_get_handler,     false},
        {"/console.html", HTTP_GET,  console_get_handler, false},
        {"/ws",           HTTP_GET,  ws_handler,          true},
    };

    for (const auto &r : routes)
    {
        httpd_uri_t u = {};
        u.uri = r.uri;
        u.method = r.method;
        u.handler = r.handler;
        u.user_ctx = this;
        u.is_websocket = r.ws;
        err = httpd_register_uri_handler(server_, &u);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to register %s: %s", r.uri, esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(TAG, "HTTP server started on port %u (auth: %s)",
             (unsigned)port_,
             password_.empty() ? "open" : "basic");

    log_memory();
    return ESP_OK;
}

esp_err_t HaspHttp::stop_backend()
{
    if (!server_)
        return ESP_OK;

    httpd_stop(server_);
    server_ = nullptr;
    ESP_LOGI(TAG, "HTTP server stopped");
    return ESP_OK;
}

bool HaspHttp::isRunning() const
{
    return server_ != nullptr;
}

void HaspHttp::hasp_event_handler(
    void *arg,
    esp_event_base_t base,
    int32_t id,
    void *event_data)
{
    auto *self = static_cast<HaspHttp *>(arg);

    if (id == HASP_EVENT_CONNECTED)
    {
        self->on_network_up();
    }
    else if (id == HASP_EVENT_DISCONNECTED)
    {
        self->on_network_down();
    }
}

void HaspHttp::on_network_up()
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
        if (!isRunning())
            start_backend();
        break;
    }
}

void HaspHttp::on_network_down()
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
        break;
    }
}
