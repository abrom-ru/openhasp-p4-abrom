#include "hasp_http.hpp"
#include "hasp_config.hpp"
#include "hasp_dispatch.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"

#include "mbedtls/base64.h"

#include <ArduinoJson.h>

#include <cstring>

static const char *TAG = "HASP_HTTP";

// -----------------------------------------------------------------------------
// Step 7C-1: HTTP Basic Auth (S3-mirror httpIsAuthenticated)
// -----------------------------------------------------------------------------
//
// If password_ is empty the server is fully open — matches S3 default where
// httpIsAuthenticated() returns true whenever http_config.password[0] == '\0'.
// Otherwise every protected request must carry `Authorization: Basic <b64>`
// with user:password matching the configured credentials. On failure we
// answer 401 with a WWW-Authenticate challenge so browsers pop up the
// standard login dialog.
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

    // "Basic " + base64(user:pass) — cap at 256 which covers any realistic
    // credentials pair.
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

    // Split "user:password"
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

#define REQUIRE_AUTH(req)                              \
    do                                                 \
    {                                                  \
        if (HaspHttp::require_auth(req) != ESP_OK)     \
            return ESP_FAIL;                           \
    } while (0)

HaspHttp::~HaspHttp()
{
    stop();
}

// -----------------------------------------------------------------------------
// Step 7C-1: Config (S3-mirror httpGetConfig / httpSetConfig)
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

    // For the mask we only need to know whether a password is set. Read from
    // NVS if the in-memory copy is empty (i.e. after a fresh boot before
    // load_from_nvs ran, though in practice load_from_nvs is called from
    // start_backend so this is defensive).
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
    http["password"] = password.empty() ? std::string() : std::string("******");
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
        if (pw != "******")
        {
            password_ = pw;
            nvs_set_string("password", password_);
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

// ---------------------------------------------------------------------------
// GET /api/config
// ---------------------------------------------------------------------------
esp_err_t HaspHttp::config_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);

    HaspHttp *self = from_req(req);

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();

    self->mgr_.get_config(obj);

    std::string body;
    serializeJson(doc, body);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body.c_str(), body.size());
    return ESP_OK;
}

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
            break; // EOF
        }
    }

    fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

// ---------------------------------------------------------------------------
// POST /api/config
// ---------------------------------------------------------------------------
esp_err_t HaspHttp::config_post_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);

    HaspHttp *self = from_req(req);

    char buf[2048];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    JsonObjectConst obj = doc.as<JsonObjectConst>();
    self->mgr_.set_config(obj);

    // Step 6 (S3-mirror configWrite): persist to /littlefs/config.json.
    // Passwords are masked by get_config; real values remain in NVS.
    hasp_config_save(self->mgr_);

    JsonDocument out;
    JsonObject outObj = out.to<JsonObject>();
    self->mgr_.get_config(outObj);

    std::string body;
    serializeJson(out, body);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body.c_str(), body.size());
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// WebSocket /ws
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Step 7C-2: Static asset serving from /littlefs/webui/
// ---------------------------------------------------------------------------
//
// Wildcard handler: any GET that didn't match a specific URI lands here.
// Maps `/foo/bar.html` → `/littlefs/webui/foo/bar.html`. `/` maps to
// `/littlefs/webui/index.html`. If the plain file is missing but a
// `<file>.gz` sibling exists we serve that with `Content-Encoding: gzip`
// (S3 does the same for its pre-gzipped bundles under data/static/).
//
// Path traversal guard: reject any path containing `..`. The httpd URI
// parser already resolves things like `%2e%2e/`, but a plain `..` in the
// path would otherwise let a caller reach anywhere on LittleFS.
static const char *mime_from_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return "application/octet-stream";

    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".js") == 0)
        return "application/javascript";
    if (strcmp(dot, ".json") == 0)
        return "application/json";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".svg") == 0)
        return "image/svg+xml";
    if (strcmp(dot, ".ico") == 0)
        return "image/x-icon";
    if (strcmp(dot, ".txt") == 0)
        return "text/plain; charset=utf-8";
    if (strcmp(dot, ".woff2") == 0)
        return "font/woff2";
    if (strcmp(dot, ".woff") == 0)
        return "font/woff";
    if (strcmp(dot, ".ttf") == 0)
        return "font/ttf";
    return "application/octet-stream";
}

esp_err_t HaspHttp::static_get_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);

    // Strip query string.
    char uri_path[128];
    const char *q = strchr(req->uri, '?');
    size_t uri_len = q ? static_cast<size_t>(q - req->uri) : strlen(req->uri);
    if (uri_len >= sizeof(uri_path))
    {
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "URI too long");
        return ESP_FAIL;
    }
    memcpy(uri_path, req->uri, uri_len);
    uri_path[uri_len] = '\0';

    // Traversal guard.
    if (strstr(uri_path, ".."))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_FAIL;
    }

    // Map / → /index.html
    const char *path = uri_path;
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0'))
    {
        path = "/index.html";
    }

    // Build filesystem paths.
    char fs_path[192];
    int n = snprintf(fs_path, sizeof(fs_path), "/littlefs/webui%s", path);
    if (n <= 0 || n >= (int)sizeof(fs_path))
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Path build failed");
        return ESP_FAIL;
    }

    bool is_gz = false;
    FILE *f = fopen(fs_path, "r");

    // `.html` fallback: extension-less URLs (`/config`, `/config/wifi`, S3
    // convention) resolve to `<path>.html`. Only kicks in when the raw path
    // has no dot in its last segment.
    if (!f)
    {
        const char *slash = strrchr(path, '/');
        const char *tail = slash ? slash + 1 : path;
        if (strchr(tail, '.') == nullptr)
        {
            char html_path[200];
            int hn = snprintf(html_path, sizeof(html_path), "%s.html", fs_path);
            if (hn > 0 && hn < (int)sizeof(html_path))
            {
                f = fopen(html_path, "r");
                if (f)
                {
                    strncpy(fs_path, html_path, sizeof(fs_path) - 1);
                    fs_path[sizeof(fs_path) - 1] = '\0';
                    path = ".html"; // for MIME detection
                }
            }
        }
    }

    if (!f)
    {
        char gz_path[200];
        int gn = snprintf(gz_path, sizeof(gz_path), "%s.gz", fs_path);
        if (gn > 0 && gn < (int)sizeof(gz_path))
        {
            f = fopen(gz_path, "r");
            if (f)
            {
                is_gz = true;
            }
        }
    }

    if (!f)
    {
        ESP_LOGD(TAG, "static: 404 %s", fs_path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, mime_from_ext(path));
    if (is_gz)
    {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    char buf[1024];
    while (true)
    {
        size_t r = fread(buf, 1, sizeof(buf), f);
        if (r > 0)
        {
            if (httpd_resp_send_chunk(req, buf, r) != ESP_OK)
            {
                fclose(f);
                return ESP_FAIL;
            }
        }
        if (r < sizeof(buf))
        {
            if (ferror(f))
            {
                ESP_LOGE(TAG, "static: read error %s", fs_path);
                fclose(f);
                return ESP_FAIL;
            }
            break;
        }
    }

    fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

// ---------------------------------------------------------------------------
// Step 7C-4: GET /info — on-demand statusupdate snapshot.
// ---------------------------------------------------------------------------
//
// Same JSON body that gets pushed to hasp/<host>/state/statusupdate every
// teleperiod seconds (mirrors S3 webHandleInfoJson). We take the LVGL lock
// because hasp_dispatch_build_statusupdate_json reads lv_display_get_default
// — it's cheap so a short timeout is fine.
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
        // Fall back to a naked call — worst case lv_display_get_default is
        // benign to read concurrently (single writer at boot).
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

// ---------------------------------------------------------------------------
// Step 7C-4: POST /api/reboot — reply first, restart after ~1s.
// ---------------------------------------------------------------------------
//
// esp_restart from inside the httpd worker would drop the response before
// the client reads it. Schedule a one-shot esp_timer for a delayed restart
// and return "OK" immediately.
static void reboot_timer_cb(void * /*arg*/)
{
    esp_restart();
}

esp_err_t HaspHttp::reboot_post_handler(httpd_req_t *req)
{
    REQUIRE_AUTH(req);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

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
        esp_timer_start_once(s_reboot_timer, 1000000ULL); // 1 s
    }

    ESP_LOGW(TAG, "Reboot requested via HTTP; restarting in 1 s");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------
esp_err_t HaspHttp::start_backend()
{
    if (server_)
        return ESP_OK;

    // Step 7C-1: load credentials before we bind the port.
    if (user_.empty() && password_.empty())
    {
        load_from_nvs();
    }

    log_memory();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    config.lru_purge_enable = true;
    // Default httpd stack is 4 KB — LittleFS commit path (lfs_dir_traverse
    // recursion + esp_flash_read) overflows it when POST /api/config writes
    // config.json. Bump to 8 KB to fit.
    config.stack_size = 8192;
    // Step 7C-2: enable wildcard URI matching so `/*` static handler catches
    // whatever wasn't matched by the explicit routes below.
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t get_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = config_get_handler,
        .user_ctx = this,
    };
    err = httpd_register_uri_handler(server_, &get_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register GET /api/config: %s",
                 esp_err_to_name(err));
        return err;
    }

    httpd_uri_t post_uri = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = config_post_handler,
        .user_ctx = this};
    err = httpd_register_uri_handler(server_, &post_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register POST /api/config: %s",
                 esp_err_to_name(err));
        return err;
    }

    httpd_uri_t console_uri = {
        .uri = "/console.html",
        .method = HTTP_GET,
        .handler = console_get_handler,
        .user_ctx = this,
    };

    err = httpd_register_uri_handler(server_, &console_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register /console.html: %s",
                 esp_err_to_name(err));
        return err;
    }

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = this,
        .is_websocket = true,
        .ws_pre_handshake_cb = nullptr};

    err = httpd_register_uri_handler(server_, &ws_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register /ws: %s", esp_err_to_name(err));
        return err;
    }

    // Step 7C-4: /info (statusupdate snapshot) + /api/reboot (delayed restart).
    httpd_uri_t info_uri = {
        .uri = "/info",
        .method = HTTP_GET,
        .handler = info_get_handler,
        .user_ctx = this,
    };
    err = httpd_register_uri_handler(server_, &info_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register GET /info: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t reboot_uri = {
        .uri = "/api/reboot",
        .method = HTTP_POST,
        .handler = reboot_post_handler,
        .user_ctx = this,
    };
    err = httpd_register_uri_handler(server_, &reboot_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register POST /api/reboot: %s", esp_err_to_name(err));
        return err;
    }

    // Step 7C-2: wildcard static handler — registered LAST so specific URIs
    // above win the match. Matches any GET (esp_http_server evaluates
    // wildcards after literal matches when uri_match_fn=wildcard).
    httpd_uri_t static_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = static_get_handler,
        .user_ctx = this,
    };
    err = httpd_register_uri_handler(server_, &static_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register /* static: %s", esp_err_to_name(err));
        return err;
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
