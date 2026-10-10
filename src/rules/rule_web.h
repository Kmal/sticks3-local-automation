#pragma once

#include "rule_config_store.h"
#include "rule_runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define RULE_WEB_AUTH_TOKEN_LEN 32u
#define RULE_WEB_AUTH_SESSIONS 4u
#define RULE_WEB_AUTH_TIMEOUT_MS 60000u

typedef enum {
    RULE_WEB_AUTH_NONE = 0,
    RULE_WEB_AUTH_PENDING,
    RULE_WEB_AUTH_APPROVED,
    RULE_WEB_AUTH_DENIED,
    RULE_WEB_AUTH_EXPIRED,
} rule_web_auth_state_t;

typedef struct {
    char token[RULE_WEB_AUTH_TOKEN_LEN + 1u];
    rule_web_auth_state_t state;
    uint32_t requested_ms;
    uint32_t request_id;
} rule_web_auth_session_t;

typedef bool (*rule_web_sound_status_cb_t)(char *out, size_t out_len, void *ctx);
typedef void (*rule_web_config_changed_cb_t)(const automation_config_t *config, void *ctx);
typedef bool (*rule_web_lock_cb_t)(void *ctx);
typedef void (*rule_web_unlock_cb_t)(void *ctx);

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#endif


typedef enum {
    RULE_WEB_METHOD_GET = 0,
    RULE_WEB_METHOD_POST,
} rule_web_method_t;

typedef struct {
    atomic_bool started;
    rule_web_auth_session_t auth[RULE_WEB_AUTH_SESSIONS];
    rule_runtime_t *runtime;
    rule_config_store_t *store;
    rule_web_config_changed_cb_t config_changed_cb;
    void *config_changed_ctx;
    rule_web_lock_cb_t runtime_lock_cb;
    rule_web_unlock_cb_t runtime_unlock_cb;
    void *runtime_lock_ctx;
#ifdef ESP_PLATFORM
    httpd_handle_t server;
#endif
} rule_web_t;

bool rule_web_start(rule_web_t *web, rule_runtime_t *runtime, rule_config_store_t *store);
bool rule_web_start_locked(rule_web_t *web, rule_runtime_t *runtime, rule_config_store_t *store,
                           rule_web_lock_cb_t lock_cb, rule_web_unlock_cb_t unlock_cb, void *ctx);
/* Browser-generated 128-bit secrets are approved only through the device input
 * callback. HTTP exposes status, never other browsers' secrets or approval. */
unsigned rule_web_handle_auth(rule_web_t *web, rule_web_method_t method, const char *token,
                              const char *body, uint32_t now_ms, char *out, size_t out_len);
uint32_t rule_web_pending_auth(rule_web_t *web, uint32_t now_ms);
bool rule_web_decide_auth(rule_web_t *web, uint32_t request_id, bool approve, uint32_t now_ms);
bool rule_web_authorize(rule_web_t *web, const char *token);
void rule_web_stop(rule_web_t *web);
void rule_web_set_sound_status_builder(rule_web_sound_status_cb_t cb, void *ctx);
void rule_web_set_config_changed_callback(rule_web_t *web, rule_web_config_changed_cb_t cb, void *ctx);
void rule_web_set_runtime_lock(rule_web_t *web, rule_web_lock_cb_t lock_cb, rule_web_unlock_cb_t unlock_cb, void *ctx);
bool rule_web_get_status_json(const rule_web_t *web, char *out, size_t out_len);
bool rule_web_handle_request(rule_web_t *web, rule_web_method_t method, const char *path, const char *body, char *out, size_t out_len);
