#include "action_http.h"

#include <stdio.h>
#include <string.h>

static bool s_action_http_network_ready;

void action_http_set_network_ready(bool ready)
{
    s_action_http_network_ready = ready;
}

bool action_http_network_ready(void)
{
    return s_action_http_network_ready;
}

#ifdef ESP_PLATFORM
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#endif

static bool url_has_supported_scheme_and_host(const char *url)
{
    if (url == NULL) {
        return false;
    }
    size_t start = 0;
    if (strncmp(url, "http://", 7) == 0) {
        start = 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        start = 8;
    } else {
        return false;
    }
    return url[start] != '\0' && url[start] != '/' && url[start] != ' ';
}

bool rule_event_to_json(const rule_event_t *event, char *out, size_t out_len)
{
    if (event == NULL || out == NULL || out_len == 0) {
        return false;
    }
    const int written = snprintf(out, out_len,
                                 "{\"sequence\":%lu,\"uptime_ms\":%lu,\"rule_id\":%lu,"
                                 "\"source\":\"%s\",\"action\":\"%s\",\"fire_count\":%lu}",
                                 (unsigned long)event->sequence,
                                 (unsigned long)event->uptime_ms,
                                 (unsigned long)event->rule_id,
                                 rule_source_name(event->source),
                                 rule_action_name(event->action),
                                 (unsigned long)event->fire_count);
    return written > 0 && (size_t)written < out_len;
}

action_http_result_t action_http_post_event(const rule_action_t *action, const rule_event_t *event)
{
    if (action == NULL || event == NULL) {
        return ACTION_HTTP_RESULT_INVALID_ARG;
    }
    if (action->type != RULE_ACTION_HTTP_POST || !url_has_supported_scheme_and_host(action->http_url) ||
        action->timeout_ms == 0 || action->timeout_ms > RULE_MAX_HTTP_TIMEOUT_MS) {
        return ACTION_HTTP_RESULT_INVALID_CONFIG;
    }
    if (!action_http_network_ready()) {
        return ACTION_HTTP_RESULT_NOT_READY;
    }
#ifdef ESP_PLATFORM
    char json[256];
    if (!rule_event_to_json(event, json, sizeof(json))) {
        return ACTION_HTTP_RESULT_INVALID_ARG;
    }
    esp_http_client_config_t config = {
        .url = action->http_url,
        .timeout_ms = (int)action->timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ACTION_HTTP_RESULT_NOT_READY;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (action->http_bearer_token[0] != '\0') {
        char auth[RULE_HTTP_AUTH_MAX + 8u];
        (void)snprintf(auth, sizeof(auth), "Bearer %s", action->http_bearer_token);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    esp_http_client_set_post_field(client, json, (int)strlen(json));
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        return ACTION_HTTP_RESULT_NOT_READY;
    }
    return (status >= 200 && status < 300) ? ACTION_HTTP_RESULT_OK : ACTION_HTTP_RESULT_HTTP_ERROR;
#else
    return ACTION_HTTP_RESULT_NOT_READY;
#endif
}
