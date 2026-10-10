#include "rule_web.h"
#include "capability_registry.h"
#include "action_http.h"
#include "app_wifi.h"
#include "app_time.h"
#include "webui_assets.h"
#include "cJSON.h"
#include <math.h>

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RULE_WEB_MAX_BODY 32768u
#define RULE_WEB_MAX_RESPONSE 32768u
#define RULE_WEB_SMALL_BODY 512u
/* Keep IDs distinct across service reopenings so old LCD decisions cannot
 * accidentally match the first request of a new service session. */
static _Atomic uint32_t s_auth_request_id;

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"


static const char *TAG = "RULE_WEB";

static esp_err_t rule_web_http_handler(httpd_req_t *req)
{
    rule_web_t *web = (rule_web_t *)req->user_ctx;
    if (req->method == HTTP_GET && strcmp(req->uri, "/") == 0) {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, (const char *)webui_index_html, (ssize_t)webui_index_html_len) == ESP_OK ? ESP_OK : ESP_FAIL;
    }

    if (req->method == HTTP_GET && strcmp(req->uri, "/favicon.ico") == 0)
        return httpd_resp_send(req, "", 0);
    char token[RULE_WEB_AUTH_TOKEN_LEN + 1u] = {0};
    if (httpd_req_get_hdr_value_len(req, "X-Device-Token") == RULE_WEB_AUTH_TOKEN_LEN)
        (void)httpd_req_get_hdr_value_str(req, "X-Device-Token", token, sizeof(token));
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (strcmp(req->uri, "/api/auth") == 0) {
        /* The only public API: fixed stack buffers, no config access. A custom
         * header prevents cross-origin forms from creating approval prompts. */
        char body[8] = {0}, response[128];
        if (req->content_len >= sizeof(body)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid auth action");
            return ESP_FAIL;
        }
        size_t received = 0;
        while (received < req->content_len) {
            int n = httpd_req_recv(req, body + received, req->content_len - received);
            if (n <= 0) return ESP_FAIL;
            received += (size_t)n;
        }
        if (memchr(body, '\0', received) != NULL) return ESP_FAIL;
        unsigned status = rule_web_handle_auth(web, req->method == HTTP_POST ? RULE_WEB_METHOD_POST : RULE_WEB_METHOD_GET,
            token, body, (uint32_t)(esp_timer_get_time() / 1000), response, sizeof(response));
        httpd_resp_set_status(req, status == 200 ? "200 OK" : status == 409 ? "409 Conflict" :
                                  status == 503 ? "503 Service Unavailable" : "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, response);
    }
    if (!rule_web_authorize(web, token)) {
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Request access and approve it on the StickS3\"}");
        return ESP_FAIL; /* Close without draining an untrusted request body. */
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    const bool config_route = strcmp(req->uri, "/api/config") == 0;
    const size_t body_limit = config_route ? RULE_WEB_MAX_BODY : RULE_WEB_SMALL_BODY;
    const size_t response_limit = config_route && req->method == HTTP_GET ? RULE_WEB_MAX_RESPONSE :
                                  (strcmp(req->uri, "/api/capabilities") == 0 ? 16384u : 2048u);
    if (req->content_len >= body_limit) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }
    char *body = calloc(1, req->content_len + 1u);
    char *response = malloc(response_limit);
    if (body == NULL || response == NULL) {
        free(body); free(response);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rule web allocation failed");
        return ESP_FAIL;
    }
    if (config_route) ESP_LOGI(TAG, "config request: body=%u response=%u internal_free=%u largest=%u minimum=%u",
        (unsigned)(req->content_len + 1), (unsigned)response_limit,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (req->method == HTTP_POST) {
        size_t received = 0;
        while (received < req->content_len) {
            int chunk = httpd_req_recv(req, body + received, req->content_len - received);
            if (chunk <= 0) {
                free(body); free(response);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "failed to read body");
                return ESP_FAIL;
            }
            received += (size_t)chunk;
        }
        if (memchr(body, '\0', received) != NULL) {
            free(body); free(response);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
            return ESP_FAIL;
        }
    }
    rule_web_method_t method = req->method == HTTP_POST ? RULE_WEB_METHOD_POST : RULE_WEB_METHOD_GET;
    if (!rule_web_handle_request(web, method, req->uri, body, response, response_limit)) {
        free(body);
        free(response);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "rule web request failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, strcmp(req->uri, "/") == 0 ? "text/html" : (strcmp(req->uri, "/favicon.ico") == 0 ? "image/x-icon" : "application/json"));
    esp_err_t err = httpd_resp_sendstr(req, response) == ESP_OK ? ESP_OK : ESP_FAIL;
    free(body);
    free(response);
    return err;
}

static bool register_uri(httpd_handle_t server, const char *uri, httpd_method_t method, rule_web_t *web)
{
    httpd_uri_t handler = {
        .uri = uri,
        .method = method,
        .handler = rule_web_http_handler,
        .user_ctx = web,
    };
    esp_err_t err = httpd_register_uri_handler(server, &handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register URI failed: method=%d uri=%s err=%s", (int)method, uri, esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "registered URI: method=%d uri=%s", (int)method, uri);
    return true;
}
#endif


bool rule_web_start(rule_web_t *web, rule_runtime_t *runtime, rule_config_store_t *store)
{
    return rule_web_start_locked(web, runtime, store, NULL, NULL, NULL);
}

bool rule_web_start_locked(rule_web_t *web, rule_runtime_t *runtime, rule_config_store_t *store,
                           rule_web_lock_cb_t lock_cb, rule_web_unlock_cb_t unlock_cb, void *ctx)
{
    if (web == NULL || runtime == NULL || store == NULL) {
        return false;
    }
    memset(web, 0, sizeof(*web));
    atomic_init(&web->started, false);
    web->runtime = runtime;
    web->store = store;
    web->runtime_lock_cb = lock_cb;
    web->runtime_unlock_cb = unlock_cb;
    web->runtime_lock_ctx = ctx;
#ifdef ESP_PLATFORM
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 19;
    config.stack_size = 8192;
    /* Keep the stack internal: configuration handlers write NVS, which may
     * disable the flash/PSRAM cache. Total free heap includes PSRAM and does
     * not describe the memory available to create this task. */
    config.task_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG, "starting HTTP server: port=%u max_uri_handlers=%u stack=%u internal_free=%u largest=%u minimum=%u psram_free=%u",
             (unsigned)config.server_port,
             (unsigned)config.max_uri_handlers,
             (unsigned)config.stack_size,
             (unsigned)heap_caps_get_free_size(config.task_caps),
             (unsigned)heap_caps_get_largest_free_block(config.task_caps),
             (unsigned)heap_caps_get_minimum_free_size(config.task_caps),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    esp_err_t err = httpd_start(&web->server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s stack=%u internal_free=%u largest=%u minimum=%u psram_free=%u",
                 esp_err_to_name(err), (unsigned)config.stack_size,
                 (unsigned)heap_caps_get_free_size(config.task_caps),
                 (unsigned)heap_caps_get_largest_free_block(config.task_caps),
                 (unsigned)heap_caps_get_minimum_free_size(config.task_caps),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return false;
    }
    ESP_LOGI(TAG, "HTTP server started");
    if (!register_uri(web->server, "/", HTTP_GET, web) ||
        !register_uri(web->server, "/favicon.ico", HTTP_GET, web) ||
        !register_uri(web->server, "/api/auth", HTTP_GET, web) ||
        !register_uri(web->server, "/api/auth", HTTP_POST, web) ||
        !register_uri(web->server, "/api/config", HTTP_GET, web) ||
        !register_uri(web->server, "/api/config", HTTP_POST, web) ||
        !register_uri(web->server, "/api/capabilities", HTTP_GET, web) ||
        !register_uri(web->server, "/api/status", HTTP_GET, web) ||
        !register_uri(web->server, "/api/time", HTTP_GET, web) ||
        !register_uri(web->server, "/api/time", HTTP_POST, web) ||
        !register_uri(web->server, "/api/wifi/status", HTTP_GET, web) ||
        !register_uri(web->server, "/api/wifi/scan", HTTP_POST, web) ||
        !register_uri(web->server, "/api/wifi/connect", HTTP_POST, web) ||
        !register_uri(web->server, "/api/wifi/forget", HTTP_POST, web) ||
        !register_uri(web->server, "/api/wifi/ap", HTTP_POST, web) ||
        !register_uri(web->server, "/api/wifi/mode", HTTP_POST, web) ||
        !register_uri(web->server, "/api/rules/test", HTTP_POST, web) ||
        !register_uri(web->server, "/api/gpio/test", HTTP_POST, web) ||
        !register_uri(web->server, "/api/hat/probe", HTTP_POST, web)) {
        ESP_LOGE(TAG, "HTTP server URI registration failed; stopping server");
        httpd_stop(web->server);
        web->server = NULL;
        return false;
    }
#endif
    web->started = true;
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "rule web ready");
#endif
    return true;
}

static bool rule_web_lock_runtime(const rule_web_t *web);
static void rule_web_unlock_runtime(const rule_web_t *web);

static bool auth_token_valid(const char *token)
{
    if (token == NULL || strlen(token) != RULE_WEB_AUTH_TOKEN_LEN) return false;
    for (size_t i = 0; i < RULE_WEB_AUTH_TOKEN_LEN; ++i)
        if (!((token[i] >= '0' && token[i] <= '9') || (token[i] >= 'a' && token[i] <= 'f'))) return false;
    return true;
}

static rule_web_auth_session_t *auth_find(rule_web_t *web, const char *token)
{
    if (!auth_token_valid(token)) return NULL;
    for (size_t i = 0; i < RULE_WEB_AUTH_SESSIONS; ++i) {
        unsigned difference = 0;
        for (size_t j = 0; j < RULE_WEB_AUTH_TOKEN_LEN; ++j)
            difference |= (unsigned char)token[j] ^ (unsigned char)web->auth[i].token[j];
        if (difference == 0 && web->auth[i].state != RULE_WEB_AUTH_NONE) return &web->auth[i];
    }
    return NULL;
}

static void auth_expire(rule_web_t *web, uint32_t now_ms)
{
    for (size_t i = 0; i < RULE_WEB_AUTH_SESSIONS; ++i) {
        rule_web_auth_session_t *session = &web->auth[i];
        /* Subtraction also handles the monotonic clock's 32-bit wrap. */
        if (session->state == RULE_WEB_AUTH_PENDING &&
            (uint32_t)(now_ms - session->requested_ms) >= RULE_WEB_AUTH_TIMEOUT_MS)
            session->state = RULE_WEB_AUTH_EXPIRED;
    }
}

unsigned rule_web_handle_auth(rule_web_t *web, rule_web_method_t method, const char *token,
                              const char *body, uint32_t now_ms, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) return 503;
    if (!auth_token_valid(token) || (method != RULE_WEB_METHOD_GET && method != RULE_WEB_METHOD_POST) ||
        (method == RULE_WEB_METHOD_GET && body != NULL && body[0] != '\0') ||
        (method == RULE_WEB_METHOD_POST && (body == NULL ||
         (strcmp(body, "request") != 0 && strcmp(body, "cancel") != 0)))) {
        (void)snprintf(out, out_len, "{\"error\":\"Invalid access request\"}");
        return 400;
    }
    if (web == NULL || !rule_web_lock_runtime(web)) {
        (void)snprintf(out, out_len, "{\"error\":\"Device busy; try again\"}");
        return 503;
    }
    unsigned status = 200;
    const char *error = NULL;
    rule_web_auth_session_t *session = NULL;
    if (!web->started) {
        status = 503; error = "Web UI is disabled";
    } else {
        auth_expire(web, now_ms);
        session = auth_find(web, token);
        if (method == RULE_WEB_METHOD_POST && strcmp(body, "cancel") == 0) {
            if (session != NULL) memset(session, 0, sizeof(*session));
            session = NULL;
        } else if (method == RULE_WEB_METHOD_POST && session == NULL) {
            rule_web_auth_session_t *available = NULL;
            bool pending = false;
            for (size_t i = 0; i < RULE_WEB_AUTH_SESSIONS; ++i) {
                rule_web_auth_state_t state = web->auth[i].state;
                if (state == RULE_WEB_AUTH_PENDING) pending = true;
                /* Prefer an unused slot so a browser can still read its
                 * rejection/expiry while another requests approval. */
                if (state == RULE_WEB_AUTH_NONE ||
                    (available == NULL && (state == RULE_WEB_AUTH_DENIED || state == RULE_WEB_AUTH_EXPIRED)))
                    available = &web->auth[i];
            }
            if (pending || available == NULL) {
                status = 409;
                error = pending ? "Another browser is waiting for approval; try again shortly" :
                                  "Browser limit reached; lock another browser or reopen Web UI";
            } else {
                session = available;
                memset(session, 0, sizeof(*session));
                memcpy(session->token, token, RULE_WEB_AUTH_TOKEN_LEN);
                session->state = RULE_WEB_AUTH_PENDING;
                session->requested_ms = now_ms;
                session->request_id = atomic_fetch_add(&s_auth_request_id, 1u) + 1u;
                if (session->request_id == 0)
                    session->request_id = atomic_fetch_add(&s_auth_request_id, 1u) + 1u;
            }
        }
    }
    const char *state = session == NULL ? "none" : session->state == RULE_WEB_AUTH_PENDING ? "pending" :
        session->state == RULE_WEB_AUTH_APPROVED ? "approved" : session->state == RULE_WEB_AUTH_DENIED ? "denied" : "expired";
    const int n = error != NULL ? snprintf(out, out_len, "{\"error\":\"%s\"}", error) :
                                 snprintf(out, out_len, "{\"state\":\"%s\",\"request_id\":%lu}", state,
                                     (unsigned long)(session != NULL ? session->request_id : 0));
    rule_web_unlock_runtime(web);
    return n > 0 && (size_t)n < out_len ? status : 503;
}

uint32_t rule_web_pending_auth(rule_web_t *web, uint32_t now_ms)
{
    if (web == NULL || !rule_web_lock_runtime(web)) return 0;
    auth_expire(web, now_ms);
    uint32_t id = 0;
    if (web->started) for (size_t i = 0; i < RULE_WEB_AUTH_SESSIONS; ++i)
        if (web->auth[i].state == RULE_WEB_AUTH_PENDING) id = web->auth[i].request_id;
    rule_web_unlock_runtime(web);
    return id;
}

bool rule_web_decide_auth(rule_web_t *web, uint32_t request_id, bool approve, uint32_t now_ms)
{
    if (web == NULL || request_id == 0 || !rule_web_lock_runtime(web)) return false;
    auth_expire(web, now_ms);
    bool decided = false;
    if (web->started) for (size_t i = 0; i < RULE_WEB_AUTH_SESSIONS; ++i) {
        rule_web_auth_session_t *session = &web->auth[i];
        if (session->state == RULE_WEB_AUTH_PENDING && session->request_id == request_id) {
            session->state = approve ? RULE_WEB_AUTH_APPROVED : RULE_WEB_AUTH_DENIED;
            decided = true;
        }
    }
    rule_web_unlock_runtime(web);
    return decided;
}

bool rule_web_authorize(rule_web_t *web, const char *token)
{
    if (web == NULL || !rule_web_lock_runtime(web)) return false;
    rule_web_auth_session_t *session = auth_find(web, token);
    const bool approved = web->started && session != NULL && session->state == RULE_WEB_AUTH_APPROVED;
    rule_web_unlock_runtime(web);
    return approved;
}

void rule_web_stop(rule_web_t *web)
{
    if (web != NULL) {
        /* Fail closed immediately, even if waiting for the runtime mutex later
         * times out. Do not hold that mutex while httpd_stop joins handlers. */
        web->started = false;
#ifdef ESP_PLATFORM
        if (web->server != NULL) {
            ESP_LOGI(TAG, "stopping HTTP server");
            httpd_stop(web->server);
            web->server = NULL;
        }
#endif
        if (rule_web_lock_runtime(web)) {
            memset(web->auth, 0, sizeof(web->auth));
            rule_web_unlock_runtime(web);
        }
    }
}


static rule_web_sound_status_cb_t s_sound_status_cb;
static void *s_sound_status_ctx;

void rule_web_set_sound_status_builder(rule_web_sound_status_cb_t cb, void *ctx)
{
    s_sound_status_cb = cb;
    s_sound_status_ctx = ctx;
}

void rule_web_set_config_changed_callback(rule_web_t *web, rule_web_config_changed_cb_t cb, void *ctx)
{
    if (web == NULL) {
        return;
    }
    web->config_changed_cb = cb;
    web->config_changed_ctx = ctx;
}

void rule_web_set_runtime_lock(rule_web_t *web, rule_web_lock_cb_t lock_cb, rule_web_unlock_cb_t unlock_cb, void *ctx)
{
    if (web == NULL) {
        return;
    }
    web->runtime_lock_cb = lock_cb;
    web->runtime_unlock_cb = unlock_cb;
    web->runtime_lock_ctx = ctx;
}

static bool rule_web_get_status_json_unlocked(const rule_web_t *web, char *out, size_t out_len)
{
    if (web == NULL || out == NULL || out_len == 0) {
        return false;
    }
    char wifi[512];
    if (!app_wifi_status_json(wifi, sizeof(wifi))) {
        (void)snprintf(wifi, sizeof(wifi), "{\"enabled\":false}");
    }
    char sound[512];
    if (s_sound_status_cb == NULL || !s_sound_status_cb(sound, sizeof(sound), s_sound_status_ctx)) {
        (void)snprintf(sound, sizeof(sound), "{\"enabled\":false,\"running\":false,\"state\":\"disabled\",\"reason\":\"audio_capture_disabled\"}");
    }
    char resources[384];
#ifdef ESP_PLATFORM
    const size_t local_jobs = uxQueueMessagesWaiting(web->runtime->dispatcher.queue_handle);
    const size_t network_jobs = uxQueueMessagesWaiting(web->runtime->dispatcher.network_queue_handle);
    snprintf(resources, sizeof(resources),
        "\"internal_free\":%u,\"internal_min\":%u,\"internal_largest\":%u,\"psram_free\":%u,\"http_stack_margin\":%u,\"local_stack_margin\":%u,\"network_stack_margin\":%u,",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)uxTaskGetStackHighWaterMark(NULL),
        (unsigned)uxTaskGetStackHighWaterMark(web->runtime->dispatcher.worker_task),
        (unsigned)uxTaskGetStackHighWaterMark(web->runtime->dispatcher.network_worker_task));
#else
    const size_t local_jobs = web->runtime->dispatcher.count;
    const size_t network_jobs = web->runtime->dispatcher.network_count;
    resources[0] = '\0';
#endif
    action_result_t result = rule_runtime_get_last_action_result(web->runtime);
    const int written = snprintf(out, out_len,
                                 "{\"started\":%s,\"last_action\":%d,\"capabilities_ready\":true,\"http_network_ready\":%s,\"wifi\":%s,\"sound\":%s,\"resources\":{%s\"local_jobs\":%u,\"network_jobs\":%u,\"enqueue_errors\":%u}}",
                                 web->started ? "true" : "false", (int)result.code,
                                 action_http_network_ready() ? "true" : "false", wifi, sound, resources, (unsigned)local_jobs, (unsigned)network_jobs, (unsigned)web->runtime->enqueue_errors);
    return written > 0 && (size_t)written < out_len;
}

static bool rule_web_lock_runtime(const rule_web_t *web)
{
    return web->runtime_lock_cb == NULL || web->runtime_lock_cb(web->runtime_lock_ctx);
}

static void rule_web_unlock_runtime(const rule_web_t *web)
{
    if (web->runtime_unlock_cb != NULL) {
        web->runtime_unlock_cb(web->runtime_lock_ctx);
    }
}

bool rule_web_get_status_json(const rule_web_t *web, char *out, size_t out_len)
{
    if (web == NULL || !rule_web_lock_runtime(web)) {
        return false;
    }
    const bool ok = rule_web_get_status_json_unlocked(web, out, out_len);
    rule_web_unlock_runtime(web);
    return ok;
}


static const char *comparator_name(rule_comparator_t comparator)
{
    switch (comparator) {
    case RULE_COMPARATOR_EQ:
        return "eq";
    case RULE_COMPARATOR_NE:
        return "ne";
    case RULE_COMPARATOR_GT:
        return "gt";
    case RULE_COMPARATOR_GTE:
        return "gte";
    case RULE_COMPARATOR_LT:
        return "lt";
    case RULE_COMPARATOR_LTE:
        return "lte";
    default:
        return "invalid";
    }
}

static const char *gpio_profile_name(rule_gpio_profile_t profile)
{
    switch (profile) {
    case RULE_GPIO_PROFILE_DIGITAL_HIGH_LOW:
        return "digital_high_low";
    case RULE_GPIO_PROFILE_RISING_EDGE:
        return "rising_edge";
    case RULE_GPIO_PROFILE_FALLING_EDGE:
        return "falling_edge";
    case RULE_GPIO_PROFILE_DEBOUNCED_CONTACT:
        return "debounced_contact";
    case RULE_GPIO_PROFILE_PULSE_COUNT:
        return "pulse_count";
    case RULE_GPIO_PROFILE_FREQUENCY:
        return "frequency";
    default:
        return "none";
    }
}

static bool json_appendf(char **cursor, size_t *remaining, const char *fmt, ...)
{
    if (cursor == NULL || *cursor == NULL || remaining == NULL || *remaining == 0 || fmt == NULL) {
        return false;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(*cursor, *remaining, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= *remaining) {
        return false;
    }

    *cursor += written;
    *remaining -= (size_t)written;
    return true;
}

static bool json_append_string(char **cursor, size_t *remaining, const char *value)
{
    if (!json_appendf(cursor, remaining, "\"")) {
        return false;
    }

    if (value != NULL) {
        for (const unsigned char *ch = (const unsigned char *)value; *ch != '\0'; ++ch) {
            switch (*ch) {
            case '\"':
                if (!json_appendf(cursor, remaining, "\\\"")) {
                    return false;
                }
                break;
            case '\\':
                if (!json_appendf(cursor, remaining, "\\\\")) {
                    return false;
                }
                break;
            case '\b':
                if (!json_appendf(cursor, remaining, "\\b")) {
                    return false;
                }
                break;
            case '\f':
                if (!json_appendf(cursor, remaining, "\\f")) {
                    return false;
                }
                break;
            case '\n':
                if (!json_appendf(cursor, remaining, "\\n")) {
                    return false;
                }
                break;
            case '\r':
                if (!json_appendf(cursor, remaining, "\\r")) {
                    return false;
                }
                break;
            case '\t':
                if (!json_appendf(cursor, remaining, "\\t")) {
                    return false;
                }
                break;
            default:
                if (*ch < 0x20u) {
                    if (!json_appendf(cursor, remaining, "\\u%04x", (unsigned)*ch)) {
                        return false;
                    }
                } else if (!json_appendf(cursor, remaining, "%c", *ch)) {
                    return false;
                }
                break;
            }
        }
    }

    return json_appendf(cursor, remaining, "\"");
}

static bool write_action_fields(char **cursor, size_t *remaining, const rule_action_t *action)
{
    return json_appendf(cursor, remaining,
        "\"action\":\"%s\",\"action_timeout_ms\":%lu,\"http_url\":",
        rule_action_name(action->type), (unsigned long)action->timeout_ms) &&
        json_append_string(cursor, remaining, action->http_url) &&
        json_appendf(cursor, remaining,
        ",\"http_bearer_token\":\"%s\",\"speaker_frequency_hz\":%lu,\"speaker_duration_ms\":%lu,"
        "\"speaker_volume_percent\":%u,\"ir_protocol\":%u,\"ir_carrier_hz\":%lu,"
        "\"ir_address\":%u,\"ir_command\":%u,\"ir_repeat_count\":%u",
        action->http_bearer_token[0] ? "masked" : "empty",
        (unsigned long)action->speaker_frequency_hz, (unsigned long)action->speaker_duration_ms,
        (unsigned)action->speaker_volume_percent, (unsigned)action->ir_protocol,
        (unsigned long)action->ir_carrier_hz, (unsigned)action->ir_address,
        (unsigned)action->ir_command, (unsigned)action->ir_repeat_count);
}

static bool write_rule_fields(char **cursor, size_t *remaining, const automation_rule_t *rule)
{
    bool ok = json_appendf(cursor, remaining, "\"id\":%lu,\"enabled\":%s,\"name\":",
              (unsigned long)rule->id, rule->enabled ? "true" : "false") &&
        json_append_string(cursor, remaining, rule->name) &&
        json_appendf(cursor, remaining, ",\"source\":\"%s\",\"source_key\":", rule_source_name(rule->when.source)) &&
        json_append_string(cursor, remaining, rule->when.source_key) &&
        json_appendf(cursor, remaining,
        ",\"comparator\":\"%s\",\"threshold_kind\":\"%s\",\"threshold_bool\":%s,\"threshold_i32\":%ld,"
        "\"cooldown_ms\":%lu,\"sustain_ms\":%lu,\"gpio_pin\":%d,\"gpio_profile\":\"%s\","
        "\"gpio_active_low\":%s,\"gpio_debounce_ms\":%lu",
        comparator_name(rule->when.comparator), rule->when.threshold.kind == RULE_VALUE_I32 ? "i32" : "bool",
        rule->when.threshold.kind == RULE_VALUE_BOOL && rule->when.threshold.as.bool_value ? "true" : "false",
        (long)(rule->when.threshold.kind == RULE_VALUE_I32 ? rule->when.threshold.as.i32_value : 0),
        (unsigned long)rule->cooldown_ms, (unsigned long)rule->when.sustain_ms,
        rule->when.gpio.pin, gpio_profile_name(rule->when.gpio.profile), rule->when.gpio.active_low ? "true" : "false",
        (unsigned long)rule->when.gpio.debounce_ms);
    ok = ok && json_appendf(cursor, remaining, ",\"actions\":[");
    for (size_t i = 0; i < rule->action_count && ok; ++i) {
        ok = json_appendf(cursor, remaining, "%s{", i ? "," : "") &&
             write_action_fields(cursor, remaining, &rule->actions[i]) && json_appendf(cursor, remaining, "}");
    }
    return ok && json_appendf(cursor, remaining, "]");
}

static bool write_config_json(const automation_config_t *config, char *out, size_t out_len)
{
    if (config == NULL || out == NULL || out_len == 0) return false;
    char *cursor = out;
    size_t remaining = out_len;
    bool ok = json_appendf(&cursor, &remaining, "{\"schema_version\":%lu,\"rule_count\":%u",
              (unsigned long)config->schema_version, (unsigned)config->rule_count);
    ok = ok && json_appendf(&cursor, &remaining, ",\"rules\":[");
    for (size_t i = 0; i < config->rule_count && ok; ++i) {
        ok = json_appendf(&cursor, &remaining, "%s{", i ? "," : "") &&
             write_rule_fields(&cursor, &remaining, &config->rules[i]) && json_appendf(&cursor, &remaining, "}");
    }
    ok = ok && json_appendf(&cursor, &remaining, "]}");
    if (!ok) out[0] = '\0';
    return ok;
}

/* List metadata fits the existing ordinary 2 KiB response allocation. */
static bool write_rule_list_json(const automation_config_t *config, char *out, size_t out_len)
{
    char *cursor = out;
    size_t remaining = out_len;
    bool ok = json_appendf(&cursor, &remaining, "{\"schema_version\":%lu,\"rules\":[", (unsigned long)config->schema_version);
    for (size_t i = 0; i < config->rule_count && ok; ++i) {
        const automation_rule_t *rule = &config->rules[i];
        char name[RULE_NAME_MAX];
        memcpy(name, rule->name, sizeof(name));
        name[sizeof(name) - 1] = '\0';
        /* Display controls as spaces; full export retains the original name. */
        for (size_t j = 0; name[j] != '\0'; ++j) if ((unsigned char)name[j] < 0x20 || name[j] == 0x7f) name[j] = ' ';
        ok = json_appendf(&cursor, &remaining, "%s{\"id\":%lu,\"name\":", i ? "," : "", (unsigned long)rule->id) &&
             json_append_string(&cursor, &remaining, name) &&
             json_appendf(&cursor, &remaining, ",\"enabled\":%s,\"source\":\"%s\",\"action_count\":%u,\"actions\":[{\"action\":\"%s\"}]}",
                          rule->enabled ? "true" : "false", rule_source_name(rule->when.source),
                          (unsigned)rule->action_count, rule_action_name(rule->actions[0].type));
    }
    ok = ok && json_appendf(&cursor, &remaining, "]}");
    if (!ok) out[0] = '\0';
    return ok;
}

/* cJSON is pinned under vendor/cjson; parse complete documents and reject
 * duplicate keys instead of searching arbitrary substrings for field names. */
static bool json_tree_valid(const cJSON *node, unsigned depth, size_t *nodes)
{
    if (depth > 16 || ++*nodes > 1024) return false;
    for (const cJSON *child = node->child; child != NULL; child = child->next) {
        if (cJSON_IsObject(node)) for (const cJSON *previous = node->child; previous != child; previous = previous->next) {
            if (strcmp(previous->string, child->string) == 0) return false;
        }
        if (!json_tree_valid(child, depth + 1, nodes)) return false;
    }
    return true;
}

/* cJSON accepts some non-JSON number spellings, raw string controls and
 * arbitrary byte strings. Validate those tokens before allocating its tree;
 * cJSON still validates document structure and decodes escapes. */
static bool json_string_token_valid(const char **cursor)
{
    const unsigned char *p = (const unsigned char *)*cursor + 1;
    while (*p != '"') {
        if (*p < 0x20u) return false;
        if (*p == '\\') {
            ++p;
            if (*p == '\0') return false;
            if (*p == 'u' && strncmp((const char *)p, "u0000", 5) == 0) return false;
            ++p;
        } else if (*p < 0x80u) {
            ++p;
        } else {
            uint32_t codepoint, minimum;
            unsigned count;
            if (*p >= 0xc2u && *p <= 0xdfu) { count = 2; codepoint = *p & 0x1fu; minimum = 0x80u; }
            else if (*p >= 0xe0u && *p <= 0xefu) { count = 3; codepoint = *p & 0x0fu; minimum = 0x800u; }
            else if (*p >= 0xf0u && *p <= 0xf4u) { count = 4; codepoint = *p & 0x07u; minimum = 0x10000u; }
            else return false;
            ++p;
            for (unsigned i = 1; i < count; ++i, ++p) {
                if ((*p & 0xc0u) != 0x80u) return false;
                codepoint = (codepoint << 6) | (*p & 0x3fu);
            }
            if (codepoint < minimum || codepoint > 0x10ffffu ||
                (codepoint >= 0xd800u && codepoint <= 0xdfffu)) return false;
        }
    }
    *cursor = (const char *)p;
    return true;
}

static bool json_number_token_valid(const char **cursor)
{
    const char *p = *cursor;
    if (*p == '-') ++p;
    if (*p == '0') ++p;
    else {
        if (*p < '1' || *p > '9') return false;
        do { ++p; } while (*p >= '0' && *p <= '9');
    }
    if (*p == '.') {
        ++p;
        if (*p < '0' || *p > '9') return false;
        do { ++p; } while (*p >= '0' && *p <= '9');
    }
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-') ++p;
        if (*p < '0' || *p > '9') return false;
        do { ++p; } while (*p >= '0' && *p <= '9');
    }
    if (*p != '\0' && *p != ',' && *p != '}' && *p != ']' &&
        *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') return false;
    *cursor = p - 1;
    return true;
}

/* Bound allocations before cJSON builds its tree, including ignored fields.
 * Also validate token spellings; cJSON validates the document structure. */
static bool json_node_budget_ok(const char *body)
{
    size_t nodes = 0;
    bool primitive = false;
    for (const char *p = body; *p != '\0'; ++p) {
        if ((unsigned char)*p < 0x20u && *p != '\t' && *p != '\r' && *p != '\n') return false;
        if (*p == '"') {
            if (!json_string_token_valid(&p)) return false;
            const char *next = p + 1;
            while (*next == ' ' || *next == '\t' || *next == '\r' || *next == '\n') ++next;
            if (*next != ':' && ++nodes > 1024) return false;
            primitive = false;
        } else if (*p == '{' || *p == '[') {
            if (++nodes > 1024) return false;
            primitive = false;
        } else if (*p == ',' || *p == ':' || *p == '}' || *p == ']' ||
                   *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            primitive = false;
        } else if (!primitive) {
            if ((unsigned char)*p < 0x20u) return false;
            if ((*p == '-' || (*p >= '0' && *p <= '9')) && !json_number_token_valid(&p)) return false;
            if (++nodes > 1024) return false;
            primitive = true;
        }
    }
    return true;
}

static cJSON *json_parse_object(const char *body)
{
    if (body == NULL || strlen(body) >= RULE_WEB_MAX_BODY) return NULL;
    if (!json_node_budget_ok(body)) return NULL;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(body, strlen(body) + 1, &end, true);
    size_t nodes = 0;
    if (root == NULL || !cJSON_IsObject(root) || !json_tree_valid(root, 0, &nodes)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static bool json_read_string(const cJSON *object, const char *key, char *out, size_t capacity)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (value == NULL) return true;
    if (!cJSON_IsString(value) || strlen(value->valuestring) >= capacity) return false;
    memcpy(out, value->valuestring, strlen(value->valuestring) + 1);
    return true;
}

static bool json_read_bool(const cJSON *object, const char *key, bool *out)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (value == NULL) return true;
    if (!cJSON_IsBool(value)) return false;
    *out = cJSON_IsTrue(value);
    return true;
}

static bool json_read_integer(const cJSON *object, const char *key, int64_t minimum, int64_t maximum, int64_t *out)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (value == NULL) return true;
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
        value->valuedouble < (double)minimum || value->valuedouble > (double)maximum) return false;
    const int64_t n = (int64_t)value->valuedouble;
    if ((double)n != value->valuedouble) return false;
    *out = n;
    return true;
}

static bool json_read_u32(const cJSON *object, const char *key, uint32_t *out)
{
    int64_t n = *out;
    if (!json_read_integer(object, key, 0, UINT32_MAX, &n)) return false;
    *out = (uint32_t)n;
    return true;
}

static bool json_get_string(const char *body, const char *key, char *out, size_t capacity)
{
    cJSON *object = json_parse_object(body);
    const bool ok = object != NULL && cJSON_GetObjectItemCaseSensitive(object, key) != NULL &&
                    json_read_string(object, key, out, capacity);
    cJSON_Delete(object);
    return ok;
}

static bool json_get_bool(const char *body, const char *key, bool *out)
{
    cJSON *object = json_parse_object(body);
    const bool ok = object != NULL && cJSON_GetObjectItemCaseSensitive(object, key) != NULL && json_read_bool(object, key, out);
    cJSON_Delete(object);
    return ok;
}

static bool json_get_i32(const char *body, const char *key, int32_t *out)
{
    cJSON *object = json_parse_object(body);
    int64_t n = 0;
    const bool ok = object != NULL && cJSON_GetObjectItemCaseSensitive(object, key) != NULL &&
                    json_read_integer(object, key, INT32_MIN, INT32_MAX, &n);
    if (ok) *out = (int32_t)n;
    cJSON_Delete(object);
    return ok;
}

static bool gpio_profile_from_name(const char *name, rule_gpio_profile_t *profile)
{
    if (name == NULL || profile == NULL) {
        return false;
    }
    if (strcmp(name, "digital_high_low") == 0) {
        *profile = RULE_GPIO_PROFILE_DIGITAL_HIGH_LOW;
        return true;
    }
    if (strcmp(name, "debounced_contact") == 0) {
        *profile = RULE_GPIO_PROFILE_DEBOUNCED_CONTACT;
        return true;
    }
    if (strcmp(name, "rising_edge") == 0) {
        *profile = RULE_GPIO_PROFILE_RISING_EDGE;
        return true;
    }
    if (strcmp(name, "falling_edge") == 0) {
        *profile = RULE_GPIO_PROFILE_FALLING_EDGE;
        return true;
    }
    if (strcmp(name, "pulse_count") == 0) {
        *profile = RULE_GPIO_PROFILE_PULSE_COUNT;
        return true;
    }
    if (strcmp(name, "frequency") == 0) {
        *profile = RULE_GPIO_PROFILE_FREQUENCY;
        return true;
    }
    return false;
}

static bool json_get_gpio_config(const char *body, rule_gpio_config_t *gpio)
{
    if (body == NULL || gpio == NULL) {
        return false;
    }
    int32_t pin = 0;
    char profile_name[32];
    if (!json_get_i32(body, "gpio_pin", &pin) ||
        !json_get_string(body, "gpio_profile", profile_name, sizeof(profile_name)) ||
        !gpio_profile_from_name(profile_name, &gpio->profile)) {
        return false;
    }
    memset(gpio, 0, sizeof(*gpio));
    gpio->pin = (int)pin;
    (void)gpio_profile_from_name(profile_name, &gpio->profile);
    bool active_low = false;
    (void)json_get_bool(body, "gpio_active_low", &active_low);
    gpio->active_low = active_low;
    int32_t debounce_ms = 20;
    (void)json_get_i32(body, "gpio_debounce_ms", &debounce_ms);
    gpio->debounce_ms = debounce_ms > 0 ? (uint32_t)debounce_ms : 0u;
    return true;
}

static bool source_from_name(const char *name, rule_source_t *source)
{
    if (name == NULL || source == NULL) {
        return false;
    }
    for (rule_source_t candidate = RULE_SOURCE_SOUND_RMS_DBFS; candidate < RULE_SOURCE_COUNT; ++candidate) {
        if (strcmp(name, rule_source_name(candidate)) == 0) {
            *source = candidate;
            return true;
        }
    }
    return false;
}

static bool action_from_name(const char *name, rule_action_type_t *action)
{
    if (name == NULL || action == NULL) {
        return false;
    }
    for (rule_action_type_t candidate = RULE_ACTION_BLE_MESSAGE; candidate < RULE_ACTION_COUNT; ++candidate) {
        if (strcmp(name, rule_action_name(candidate)) == 0) {
            *action = candidate;
            return true;
        }
    }
    return false;
}


static bool comparator_from_name(const char *name, rule_comparator_t *comparator)
{
    if (name == NULL || comparator == NULL) {
        return false;
    }
    if (strcmp(name, "eq") == 0) {
        *comparator = RULE_COMPARATOR_EQ;
        return true;
    }
    if (strcmp(name, "ne") == 0) {
        *comparator = RULE_COMPARATOR_NE;
        return true;
    }
    if (strcmp(name, "gt") == 0) {
        *comparator = RULE_COMPARATOR_GT;
        return true;
    }
    if (strcmp(name, "gte") == 0) {
        *comparator = RULE_COMPARATOR_GTE;
        return true;
    }
    if (strcmp(name, "lt") == 0) {
        *comparator = RULE_COMPARATOR_LT;
        return true;
    }
    if (strcmp(name, "lte") == 0) {
        *comparator = RULE_COMPARATOR_LTE;
        return true;
    }
    return false;
}

static bool parse_json_action(rule_action_t *action, const cJSON *object)
{
    char type[32];
    (void)snprintf(type, sizeof(type), "%s", rule_action_name(action->type));
    rule_action_type_t parsed;
    if (!json_read_string(object, "action", type, sizeof(type)) || !action_from_name(type, &parsed)) return false;
    if (parsed != action->type) {
        memset(action, 0, sizeof(*action));
        action->type = parsed;
        action->timeout_ms = 100;
        action->ir_carrier_hz = 38000;
        action->speaker_frequency_hz = 7000;
        action->speaker_duration_ms = 100;
        action->speaker_volume_percent = 50;
    }
    if (!json_read_string(object, "http_url", action->http_url, sizeof(action->http_url))) return false;
    char token[RULE_HTTP_AUTH_MAX];
    (void)snprintf(token, sizeof(token), "%s", action->http_bearer_token);
    if (!json_read_string(object, "http_bearer_token", token, sizeof(token))) return false;
    if (strcmp(token, "masked") == 0) {
        if (action->http_bearer_token[0] == '\0') return false;
    } else {
        (void)snprintf(action->http_bearer_token, sizeof(action->http_bearer_token), "%s", strcmp(token, "empty") == 0 ? "" : token);
    }
    if (!json_read_u32(object, "action_timeout_ms", &action->timeout_ms) ||
        !json_read_u32(object, "speaker_frequency_hz", &action->speaker_frequency_hz) ||
        !json_read_u32(object, "speaker_duration_ms", &action->speaker_duration_ms) ||
        !json_read_u32(object, "ir_carrier_hz", &action->ir_carrier_hz)) return false;
    int64_t n = action->speaker_volume_percent;
    if (!json_read_integer(object, "speaker_volume_percent", 0, RULE_SPEAKER_MAX_VOLUME_PERCENT, &n)) return false;
    action->speaker_volume_percent = (uint8_t)n;
    n = action->ir_address;
    if (!json_read_integer(object, "ir_address", 0, UINT16_MAX, &n)) return false;
    action->ir_address = (uint16_t)n;
    n = action->ir_command;
    if (!json_read_integer(object, "ir_command", 0, UINT16_MAX, &n)) return false;
    action->ir_command = (uint16_t)n;
    n = action->ir_repeat_count;
    if (!json_read_integer(object, "ir_repeat_count", 0, 5, &n)) return false;
    action->ir_repeat_count = (uint8_t)n;
    n = action->ir_protocol;
    if (!json_read_integer(object, "ir_protocol", 0, RULE_IR_PROTOCOL_COUNT - 1, &n)) return false;
    action->ir_protocol = (rule_ir_protocol_t)n;
    return true;
}

static bool parse_json_rule(automation_rule_t *rule, const cJSON *object)
{
    if (!cJSON_IsObject(object)) return false;
    if (!json_read_bool(object, "enabled", &rule->enabled) || !json_read_u32(object, "id", &rule->id) ||
        !json_read_string(object, "name", rule->name, sizeof(rule->name))) return false;
    char source_name[48];
    (void)snprintf(source_name, sizeof(source_name), "%s", rule_source_name(rule->when.source));
    rule_source_t source;
    if (!json_read_string(object, "source", source_name, sizeof(source_name)) || !source_from_name(source_name, &source)) return false;
    if (source != rule->when.source) {
        memset(&rule->when, 0, sizeof(rule->when));
        rule->when.source = source;
        rule->when.gpio.pin = RULE_GPIO_UNUSED_PIN;
        rule->when.comparator = RULE_COMPARATOR_EQ;
        rule->when.threshold = rule_value_bool(true);
        if (source == RULE_SOURCE_SOUND_RMS_DBFS || source == RULE_SOURCE_SOUND_PEAK_DBFS) {
            rule->when.comparator = RULE_COMPARATOR_GTE;
            rule->when.threshold = rule_value_i32(-20 * 256);
        } else if (source == RULE_SOURCE_BATTERY_PERCENT || source == RULE_SOURCE_ADC_VOLTAGE_MV) {
            rule->when.threshold = rule_value_i32(0);
        }
    }
    if (!json_read_string(object, "source_key", rule->when.source_key, sizeof(rule->when.source_key)) ||
        !json_read_u32(object, "cooldown_ms", &rule->cooldown_ms) ||
        !json_read_u32(object, "sustain_ms", &rule->when.sustain_ms)) return false;
    char comparator[16];
    (void)snprintf(comparator, sizeof(comparator), "%s", comparator_name(rule->when.comparator));
    if (!json_read_string(object, "comparator", comparator, sizeof(comparator)) || !comparator_from_name(comparator, &rule->when.comparator)) return false;
    char kind[8];
    (void)snprintf(kind, sizeof(kind), "%s", rule->when.threshold.kind == RULE_VALUE_I32 ? "i32" : "bool");
    if (!json_read_string(object, "threshold_kind", kind, sizeof(kind))) return false;
    int64_t checked_i32 = 0;
    bool checked_bool = false;
    if (!json_read_integer(object, "threshold_i32", INT32_MIN, INT32_MAX, &checked_i32) ||
        !json_read_bool(object, "threshold_bool", &checked_bool)) return false;
    if (strcmp(kind, "i32") == 0) {
        int64_t n = rule->when.threshold.kind == RULE_VALUE_I32 ? rule->when.threshold.as.i32_value : 0;
        if (!json_read_integer(object, "threshold_i32", INT32_MIN, INT32_MAX, &n)) return false;
        rule->when.threshold = rule_value_i32((int32_t)n);
    } else if (strcmp(kind, "bool") == 0) {
        bool value = rule->when.threshold.kind == RULE_VALUE_BOOL && rule->when.threshold.as.bool_value;
        if (!json_read_bool(object, "threshold_bool", &value)) return false;
        rule->when.threshold = rule_value_bool(value);
    } else return false;
    /* The exporter includes inactive GPIO fields too; their types must still
     * be valid rather than silently accepting a malformed form submission. */
    int64_t checked_pin = rule->when.gpio.pin;
    bool checked_active_low = rule->when.gpio.active_low;
    uint32_t checked_debounce = rule->when.gpio.debounce_ms;
    char checked_profile[32];
    (void)snprintf(checked_profile, sizeof(checked_profile), "%s", gpio_profile_name(rule->when.gpio.profile));
    rule_gpio_profile_t parsed_profile;
    if (!json_read_integer(object, "gpio_pin", RULE_GPIO_UNUSED_PIN, 48, &checked_pin) ||
        !json_read_bool(object, "gpio_active_low", &checked_active_low) ||
        !json_read_u32(object, "gpio_debounce_ms", &checked_debounce) ||
        !json_read_string(object, "gpio_profile", checked_profile, sizeof(checked_profile)) ||
        (strcmp(checked_profile, "none") != 0 && !gpio_profile_from_name(checked_profile, &parsed_profile))) return false;
    if (rule_source_is_gpio(source)) {
        int64_t pin = rule->when.gpio.pin;
        char profile[32];
        (void)snprintf(profile, sizeof(profile), "%s", gpio_profile_name(rule->when.gpio.profile));
        if (!json_read_integer(object, "gpio_pin", 0, 48, &pin) ||
            !json_read_string(object, "gpio_profile", profile, sizeof(profile)) || !gpio_profile_from_name(profile, &rule->when.gpio.profile) ||
            !json_read_bool(object, "gpio_active_low", &rule->when.gpio.active_low) ||
            !json_read_u32(object, "gpio_debounce_ms", &rule->when.gpio.debounce_ms)) return false;
        rule->when.gpio.pin = (int)pin;
        (void)snprintf(rule->when.source_key, sizeof(rule->when.source_key), "%s.%d", rule_source_name(source), (int)pin);
    }
    const cJSON *actions = cJSON_GetObjectItemCaseSensitive(object, "actions");
    if (actions != NULL) {
        const int count = cJSON_GetArraySize(actions);
        if (!cJSON_IsArray(actions) || count < 1 || count > (int)RULE_MAX_ACTIONS_PER_RULE) return false;
        for (int i = 0; i < count; ++i) if (!cJSON_IsObject(cJSON_GetArrayItem(actions, i)) ||
            !parse_json_action(&rule->actions[i], cJSON_GetArrayItem(actions, i))) return false;
        rule->action_count = (size_t)count;
    } else return false;
    return true;
}

static bool make_json_config(automation_config_t *config, const char *body, const automation_config_t *current)
{
    cJSON *root = json_parse_object(body);
    if (root == NULL) return false;
    bool ok = cJSON_GetObjectItemCaseSensitive(root, "preset") == NULL &&
              json_read_u32(root, "schema_version", &config->schema_version) && config->schema_version == RULE_CONFIG_SCHEMA_VERSION;
    const cJSON *rules = cJSON_GetObjectItemCaseSensitive(root, "rules");
    if (ok && rules != NULL) {
        const int count = cJSON_GetArraySize(rules);
        ok = cJSON_IsArray(rules) && count <= (int)RULE_MAX_RULES;
        if (ok) {
            memset(config->rules, 0, sizeof(config->rules));
            config->rule_count = (size_t)count;
            for (int i = 0; i < count && ok; ++i) {
                const cJSON *object = cJSON_GetArrayItem(rules, i);
                uint32_t id = (uint32_t)i + 1;
                ok = cJSON_IsObject(object) && json_read_u32(object, "id", &id);
                automation_rule_t *rule = &config->rules[i];
                rule->id = id;
                rule->when.source = RULE_SOURCE_KEY1_SHORT;
                rule->when.threshold = rule_value_bool(true);
                rule->when.comparator = RULE_COMPARATOR_EQ;
                rule->cooldown_ms = 1000;
                rule->action_count = 1;
                rule->actions[0].type = RULE_ACTION_LOCAL_UI;
                for (size_t j = 0; current != NULL && j < current->rule_count; ++j) if (current->rules[j].id == id) *rule = current->rules[j];
                ok = ok && parse_json_rule(rule, object);
            }
        }
    } else ok = false;
    cJSON_Delete(root);
    return ok && automation_config_validate(config, NULL, 0);
}

static bool commit_web_config(const automation_config_t *config, void *ctx)
{
    return rule_config_store_save((rule_config_store_t *)ctx, config);
}

static bool rule_web_handle_request_unlocked(rule_web_t *web, rule_web_method_t method, const char *path, const char *body, char *out, size_t out_len)
{
    if (web == NULL || path == NULL || out == NULL || out_len == 0 || !web->started ||
        (method == RULE_WEB_METHOD_POST && body != NULL && strlen(body) >= (strcmp(path, "/api/config") == 0 ? RULE_WEB_MAX_BODY : RULE_WEB_SMALL_BODY))) {
        return false;
    }
    if (method == RULE_WEB_METHOD_GET && strcmp(path, "/") == 0) {
        if (webui_index_html_len >= out_len) {
            return false;
        }
        memcpy(out, webui_index_html, webui_index_html_len);
        out[webui_index_html_len] = '\0';
        return true;
    }
    if (method == RULE_WEB_METHOD_GET && strcmp(path, "/favicon.ico") == 0) {
        out[0] = '\0';
        return true;
    }
    if (method == RULE_WEB_METHOD_GET && strcmp(path, "/api/capabilities") == 0) {
        return capability_build_json(out, out_len) > 0;
    }
    if (method == RULE_WEB_METHOD_GET && strcmp(path, "/api/status") == 0) {
        return rule_web_get_status_json_unlocked(web, out, out_len);
    }
    if (method == RULE_WEB_METHOD_GET && strcmp(path, "/api/time") == 0) {
        return app_time_config_json(out, out_len);
    }
    if (method == RULE_WEB_METHOD_POST && strcmp(path, "/api/time") == 0) {
        char timezone[APP_TIME_TIMEZONE_MAX_LEN + 1u];
        if (!json_get_string(body, "timezone", timezone, sizeof(timezone))) {
            const int written = snprintf(out, out_len, "{\"ok\":false,\"error\":\"missing_timezone\"}");
            return written > 0 && (size_t)written < out_len;
        }
        if (!app_time_set_timezone(timezone, true)) {
            const int written = snprintf(out, out_len, "{\"ok\":false,\"error\":\"invalid_timezone\"}");
            return written > 0 && (size_t)written < out_len;
        }
        return app_time_config_json(out, out_len);
    }
    if (method == RULE_WEB_METHOD_GET && strcmp(path, "/api/wifi/status") == 0) {
        return app_wifi_status_json(out, out_len);
    }
    if (method == RULE_WEB_METHOD_POST && strcmp(path, "/api/wifi/scan") == 0) {
        return app_wifi_scan_json(out, out_len);
    }
    if (method == RULE_WEB_METHOD_POST && strcmp(path, "/api/wifi/connect") == 0) {
        char ssid[33];
        char password[65];
        if (!json_get_string(body, "ssid", ssid, sizeof(ssid))) {
            const int written = snprintf(out, out_len, "{\"ok\":false,\"error\":\"missing_ssid\"}");
            return written > 0 && (size_t)written < out_len;
        }
        if (!json_get_string(body, "password", password, sizeof(password))) {
            password[0] = '\0';
        }
        const bool ok = app_wifi_connect(ssid, password, true);
        char status[512];
        if (!app_wifi_status_json(status, sizeof(status))) {
            (void)snprintf(status, sizeof(status), "{\"enabled\":false}");
        }
        const int written = snprintf(out, out_len, "{\"ok\":%s,\"status\":%s}", ok ? "true" : "false", status);
        return written > 0 && (size_t)written < out_len;
    }
    if (method == RULE_WEB_METHOD_POST && strcmp(path, "/api/wifi/forget") == 0) {
        const bool ok = app_wifi_forget_sta_credentials();
        char status[512];
        if (!app_wifi_status_json(status, sizeof(status))) {
            (void)snprintf(status, sizeof(status), "{\"enabled\":false}");
        }
        const int written = snprintf(out, out_len, "{\"ok\":%s,\"status\":%s}", ok ? "true" : "false", status);
        return written > 0 && (size_t)written < out_len;
    }
    if (method == RULE_WEB_METHOD_POST && strcmp(path, "/api/wifi/ap") == 0) {
        app_wifi_config_t config;
        char ap_ssid[33];
        char ap_password[65];
        int32_t ap_channel = 0;
        if (!app_wifi_get_config(&config)) {
            const int written = snprintf(out, out_len, "{\"ok\":false,\"error\":\"config_unavailable\"}");
            return written > 0 && (size_t)written < out_len;
        }
        (void)snprintf(ap_ssid, sizeof(ap_ssid), "%s", config.ap_ssid);
        (void)snprintf(ap_password, sizeof(ap_password), "%s", config.ap_password);
        ap_channel = config.ap_channel;
        if (body != NULL && body[0] != '\0') {
            cJSON *object = json_parse_object(body);
            const char *error = NULL;
            int64_t channel = ap_channel;
            if (object == NULL) error = "invalid_json";
            else if (!json_read_string(object, "ssid", ap_ssid, sizeof(ap_ssid))) error = "invalid_ap_ssid";
            else if (!json_read_string(object, "password", ap_password, sizeof(ap_password))) error = "invalid_ap_password";
            else if (!json_read_integer(object, "channel", 1, 13, &channel)) error = "invalid_ap_channel";
            if (object != NULL && cJSON_GetObjectItemCaseSensitive(object, "ssid") != NULL &&
                cJSON_GetObjectItemCaseSensitive(object, "password") == NULL) ap_password[0] = '\0';
            cJSON_Delete(object);
            if (error != NULL) {
                const int written = snprintf(out, out_len, "{\"ok\":false,\"error\":\"%s\"}", error);
                return written > 0 && (size_t)written < out_len;
            }
            ap_channel = (int32_t)channel;
        }
        const size_t ap_ssid_len = strlen(ap_ssid);
        const size_t ap_password_len = strlen(ap_password);
        if (ap_ssid_len == 0u || ap_ssid_len > 32u) {
            const int written = snprintf(out, out_len, "{\"ok\":false,\"error\":\"invalid_ap_ssid\"}");
            return written > 0 && (size_t)written < out_len;
        }
        if (ap_password_len < 8u || ap_password_len > 63u) {
            const int written = snprintf(out, out_len, "{\"ok\":false,\"error\":\"invalid_ap_password\"}");
            return written > 0 && (size_t)written < out_len;
        }
        if (ap_channel < 1 || ap_channel > 13) {
            const int written = snprintf(out, out_len, "{\"ok\":false,\"error\":\"invalid_ap_channel\"}");
            return written > 0 && (size_t)written < out_len;
        }
        const bool ok = app_wifi_start_ap_configured(ap_ssid, ap_password, (uint8_t)ap_channel, true);
        char status[512];
        if (!app_wifi_status_json(status, sizeof(status))) {
            (void)snprintf(status, sizeof(status), "{\"enabled\":false}");
        }
        const int written = snprintf(out, out_len, "{\"ok\":%s,\"status\":%s}", ok ? "true" : "false", status);
        return written > 0 && (size_t)written < out_len;
    }
    if (method == RULE_WEB_METHOD_POST && strcmp(path, "/api/wifi/mode") == 0) {
        char mode_name[16];
        app_wifi_mode_t mode = APP_WIFI_MODE_OFF;
        if (!json_get_string(body, "mode", mode_name, sizeof(mode_name))) {
            const int written = snprintf(out, out_len, "{\"ok\":false,\"error\":\"missing_mode\"}");
            return written > 0 && (size_t)written < out_len;
        }
        if (strcmp(mode_name, "wifi") == 0) {
            mode = APP_WIFI_MODE_STA;
        } else if (strcmp(mode_name, "ap") == 0) {
            mode = APP_WIFI_MODE_AP;
        } else if (strcmp(mode_name, "apsta") == 0) {
            mode = APP_WIFI_MODE_APSTA;
        } else if (strcmp(mode_name, "off") == 0) {
            mode = APP_WIFI_MODE_OFF;
        } else {
            const int written = snprintf(out, out_len, "{\"ok\":false,\"error\":\"invalid_mode\"}");
            return written > 0 && (size_t)written < out_len;
        }
        const bool ok = app_wifi_set_mode(mode);
        char status[512];
        if (!app_wifi_status_json(status, sizeof(status))) {
            (void)snprintf(status, sizeof(status), "{\"enabled\":false}");
        }
        const int written = snprintf(out, out_len, "{\"ok\":%s,\"status\":%s}", ok ? "true" : "false", status);
        return written > 0 && (size_t)written < out_len;
    }
    if (method == RULE_WEB_METHOD_GET && strcmp(path, "/api/config") == 0) {
        return write_config_json(&web->runtime->engine.config, out, out_len);
    }
    if (method == RULE_WEB_METHOD_GET && strcmp(path, "/api/config?view=list") == 0) {
        return write_rule_list_json(&web->runtime->engine.config, out, out_len);
    }
    if (method == RULE_WEB_METHOD_POST && strcmp(path, "/api/config") == 0) {
        automation_config_t *config = calloc(1, sizeof(*config));
        if (config == NULL) {
            const int written = snprintf(out, out_len, "{\"error\":\"config allocation failed\"}");
            return written > 0 && (size_t)written < out_len;
        }
        *config = web->runtime->engine.config;
        if (!make_json_config(config, body, &web->runtime->engine.config)) {
            const int written = snprintf(out, out_len, "{\"error\":\"config rejected\"}");
            free(config);
            return written > 0 && (size_t)written < out_len;
        }
        const bool saved = web->store->opened && rule_runtime_replace_config_with_commit(
            web->runtime, config, commit_web_config, web->store);
        if (!saved) {
            const int written = snprintf(out, out_len, "{\"error\":\"config rejected\"}");
            free(config);
            return written > 0 && (size_t)written < out_len;
        }
        if (web->config_changed_cb != NULL) {
            web->config_changed_cb(config, web->config_changed_ctx);
        }
        free(config);
        const int written = snprintf(out, out_len, "{\"ok\":true}");
        return written > 0 && (size_t)written < out_len;
    }
    if (method == RULE_WEB_METHOD_POST && strcmp(path, "/api/rules/test") == 0) {
        const automation_rule_t *rule = web->runtime->engine.config.rule_count > 0 ? &web->runtime->engine.config.rules[0] : NULL;
        if (rule == NULL || !rule->enabled || rule->action_count == 0) {
            const int written = snprintf(out, out_len, "{\"ok\":false,\"reason\":\"no_enabled_rule\"}");
            return written > 0 && (size_t)written < out_len;
        }
        rule_event_t event;
        memset(&event, 0, sizeof(event));
        event.sequence = web->runtime->engine.next_event_sequence;
        event.rule_id = rule->id;
        event.source = rule->when.source;
        event.action = rule->actions[0].type;
        event.action_config = rule->actions[0];
        event.measured_value = rule->when.threshold;
        (void)snprintf(event.rule_name, sizeof(event.rule_name), "%s", rule->name);
        bool queued = action_enqueue(&web->runtime->dispatcher, &event);
        if (queued) web->runtime->engine.next_event_sequence++;
        /* Explicit first-action test; no condition evaluation or fan-out. */
        const int written = snprintf(out, out_len,
            "{\"ok\":%s,\"queued\":%s,\"mode\":\"first_action\",\"evaluates_rule\":false}",
            queued ? "true" : "false", queued ? "true" : "false");
        return written > 0 && (size_t)written < out_len;
    }
    if (method == RULE_WEB_METHOD_POST && strcmp(path, "/api/gpio/test") == 0) {
        rule_gpio_config_t gpio;
        char error[RULE_ERROR_MAX];
        char source_name[48];
        rule_source_t source = RULE_SOURCE_GPIO_DIGITAL;
        memset(error, 0, sizeof(error));
        if (json_get_string(body, "source", source_name, sizeof(source_name)) && !source_from_name(source_name, &source)) {
            source = RULE_SOURCE_INVALID;
        }
        const bool supported = source != RULE_SOURCE_INVALID && capability_source_supported(source);
        bool valid = supported && json_get_gpio_config(body, &gpio) &&
                     capability_gpio_source_profile_validate(source, &gpio, error, sizeof(error));
        const int written = snprintf(out, out_len, "{\"ok\":%s,\"supported\":%s,\"source\":\"%s\",\"reason\":\"%s\"}",
                                     valid ? "true" : "false",
                                     supported ? "true" : "false",
                                     source != RULE_SOURCE_INVALID ? rule_source_name(source) : "invalid",
                                     valid ? "valid" : (error[0] != '\0' ? error : capability_source_reason(source)));
        return written > 0 && (size_t)written < out_len;
    }
    if (method == RULE_WEB_METHOD_POST && strcmp(path, "/api/hat/probe") == 0) {
        char source_name[48];
        rule_source_t source = RULE_SOURCE_HAT_PIR_MOTION;
        if (json_get_string(body, "source", source_name, sizeof(source_name)) && !source_from_name(source_name, &source)) {
            source = RULE_SOURCE_INVALID;
        }
        const bool supported = source != RULE_SOURCE_INVALID && capability_hat_supported(source);
        const int written = snprintf(out, out_len,
                                     "{\"ok\":%s,\"present\":%s,\"supported\":%s,\"source\":\"%s\",\"reason\":\"%s\"}",
                                     supported ? "true" : "false",
                                     supported ? "true" : "false",
                                     supported ? "true" : "false",
                                     source != RULE_SOURCE_INVALID ? rule_source_name(source) : "invalid",
                                     supported ? "present" : capability_source_reason(source));
        return written > 0 && (size_t)written < out_len;
    }
    const int written = snprintf(out, out_len, "{\"error\":\"not found\"}");
    return written > 0 && (size_t)written < out_len;
}

bool rule_web_handle_request(rule_web_t *web, rule_web_method_t method, const char *path,
                             const char *body, char *out, size_t out_len)
{
    if (method == RULE_WEB_METHOD_POST && body != NULL && path != NULL && out != NULL &&
        strlen(body) >= (strcmp(path, "/api/config") == 0 ? RULE_WEB_MAX_BODY : RULE_WEB_SMALL_BODY)) {
        const int n = snprintf(out, out_len, "{\"ok\":false,\"error\":\"body too large\"}");
        return n > 0 && (size_t)n < out_len;
    }
    const bool touches_runtime = path != NULL &&
        (strcmp(path, "/api/status") == 0 || strcmp(path, "/api/config") == 0 || strcmp(path, "/api/config?view=list") == 0 ||
         strcmp(path, "/api/rules/test") == 0);
    if (touches_runtime && (web == NULL || !rule_web_lock_runtime(web))) {
        return false;
    }
    const bool ok = rule_web_handle_request_unlocked(web, method, path, body, out, out_len);
    if (touches_runtime) {
        rule_web_unlock_runtime(web);
    }
    return ok;
}
