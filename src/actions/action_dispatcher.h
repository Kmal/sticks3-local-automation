#pragma once

#include "rule_engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#endif

#define ACTION_DISPATCHER_QUEUE_LEN 8u

typedef enum {
    ACTION_RESULT_OK = 0,
    ACTION_RESULT_NOT_STARTED,
    ACTION_RESULT_QUEUE_FULL,
    ACTION_RESULT_UNSUPPORTED,
    ACTION_RESULT_INVALID_ARG,
} action_result_code_t;

typedef struct {
    action_result_code_t code;
    uint32_t sequence;
    uint32_t rule_id;
    rule_action_type_t action;
} action_result_t;

/* Eight whole-rule jobs hold a maximum of 24 actions without partial fan-out. */
typedef rule_event_batch_t action_job_t;

typedef action_result_t (*action_dispatcher_send_cb_t)(const rule_event_t *event, void *ctx);

typedef struct {
    bool started;
#ifdef ESP_PLATFORM
    portMUX_TYPE result_lock;
    QueueHandle_t queue_handle;
    TaskHandle_t worker_task;
    bool stop_requested;
#else
    action_job_t queue[ACTION_DISPATCHER_QUEUE_LEN];
    size_t head;
    size_t tail;
    size_t count;
#endif
    action_result_t last_result;
    action_dispatcher_send_cb_t ble_send;
    void *ble_ctx;
    action_dispatcher_send_cb_t http_send;
    void *http_ctx;
    action_dispatcher_send_cb_t ir_send;
    void *ir_ctx;
    action_dispatcher_send_cb_t local_ui_send;
    void *local_ui_ctx;
    action_dispatcher_send_cb_t speaker_send;
    void *speaker_ctx;
} action_dispatcher_t;

void action_dispatcher_init(action_dispatcher_t *dispatcher);
bool action_dispatcher_start(action_dispatcher_t *dispatcher);
void action_dispatcher_set_ble_sender(action_dispatcher_t *dispatcher, action_dispatcher_send_cb_t cb, void *ctx);
void action_dispatcher_set_http_sender(action_dispatcher_t *dispatcher, action_dispatcher_send_cb_t cb, void *ctx);
void action_dispatcher_set_ir_sender(action_dispatcher_t *dispatcher, action_dispatcher_send_cb_t cb, void *ctx);
void action_dispatcher_set_local_ui_sender(action_dispatcher_t *dispatcher, action_dispatcher_send_cb_t cb, void *ctx);
void action_dispatcher_set_speaker_sender(action_dispatcher_t *dispatcher, action_dispatcher_send_cb_t cb, void *ctx);
bool action_enqueue(action_dispatcher_t *dispatcher, const rule_event_t *event);
bool action_enqueue_batch(action_dispatcher_t *dispatcher, const rule_event_batch_t *batch);
/* Process one whole-rule job; process_all returns the number of executed actions. */
bool action_dispatcher_process_one(action_dispatcher_t *dispatcher);
size_t action_dispatcher_process_all(action_dispatcher_t *dispatcher);
void action_dispatcher_stop(action_dispatcher_t *dispatcher);
action_result_t action_dispatcher_get_last_result(const action_dispatcher_t *dispatcher);
