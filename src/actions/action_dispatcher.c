#include "action_dispatcher.h"

#include <string.h>

static action_result_t make_result(action_result_code_t code, const rule_event_t *event)
{
    action_result_t result;
    memset(&result, 0, sizeof(result));
    result.code = code;
    if (event != NULL) {
        result.sequence = event->sequence;
        result.rule_id = event->rule_id;
        result.action = event->action;
    }
    return result;
}

#ifdef ESP_PLATFORM
static void action_dispatcher_worker(void *arg);
#endif

static action_result_t execute_event(action_dispatcher_t *dispatcher, const rule_event_t *event)
{
    if (event == NULL) {
        return make_result(ACTION_RESULT_INVALID_ARG, NULL);
    }
    switch (event->action) {
    case RULE_ACTION_BLE_MESSAGE:
        if (dispatcher != NULL && dispatcher->ble_send != NULL) {
            return dispatcher->ble_send(event, dispatcher->ble_ctx);
        }
        return make_result(ACTION_RESULT_UNSUPPORTED, event);
    case RULE_ACTION_HTTP_POST:
        if (dispatcher != NULL && dispatcher->http_send != NULL) {
            return dispatcher->http_send(event, dispatcher->http_ctx);
        }
        return make_result(ACTION_RESULT_UNSUPPORTED, event);
    case RULE_ACTION_LOCAL_UI:
        if (dispatcher != NULL && dispatcher->local_ui_send != NULL) {
            return dispatcher->local_ui_send(event, dispatcher->local_ui_ctx);
        }
        return make_result(ACTION_RESULT_OK, event);
    case RULE_ACTION_IR_SEND:
        if (dispatcher != NULL && dispatcher->ir_send != NULL) {
            return dispatcher->ir_send(event, dispatcher->ir_ctx);
        }
        return make_result(ACTION_RESULT_UNSUPPORTED, event);
    case RULE_ACTION_SPEAKER_TONE:
        if (dispatcher != NULL && dispatcher->speaker_send != NULL) {
            return dispatcher->speaker_send(event, dispatcher->speaker_ctx);
        }
        return make_result(ACTION_RESULT_UNSUPPORTED, event);
    case RULE_ACTION_HAT_OPERATION:
    default:
        return make_result(ACTION_RESULT_UNSUPPORTED, event);
    }
}

void action_dispatcher_init(action_dispatcher_t *dispatcher)
{
    if (dispatcher == NULL) {
        return;
    }
    memset(dispatcher, 0, sizeof(*dispatcher));
    dispatcher->last_result.code = ACTION_RESULT_NOT_STARTED;
}

bool action_dispatcher_start(action_dispatcher_t *dispatcher)
{
    if (dispatcher == NULL) {
        return false;
    }
    action_dispatcher_init(dispatcher);
#ifdef ESP_PLATFORM
    dispatcher->queue_handle = xQueueCreate(ACTION_DISPATCHER_QUEUE_LEN, sizeof(action_job_t));
    if (dispatcher->queue_handle == NULL) {
        dispatcher->last_result.code = ACTION_RESULT_QUEUE_FULL;
        return false;
    }
    dispatcher->stop_requested = false;
    if (xTaskCreate(action_dispatcher_worker, "rule_action_worker", 4096, dispatcher, tskIDLE_PRIORITY + 1, &dispatcher->worker_task) != pdPASS) {
        vQueueDelete(dispatcher->queue_handle);
        dispatcher->queue_handle = NULL;
        dispatcher->last_result.code = ACTION_RESULT_NOT_STARTED;
        return false;
    }
#endif
    dispatcher->started = true;
    return true;
}

void action_dispatcher_set_ble_sender(action_dispatcher_t *dispatcher, action_dispatcher_send_cb_t cb, void *ctx)
{
    if (dispatcher == NULL) {
        return;
    }
    dispatcher->ble_send = cb;
    dispatcher->ble_ctx = ctx;
}

void action_dispatcher_set_http_sender(action_dispatcher_t *dispatcher, action_dispatcher_send_cb_t cb, void *ctx)
{
    if (dispatcher == NULL) {
        return;
    }
    dispatcher->http_send = cb;
    dispatcher->http_ctx = ctx;
}

void action_dispatcher_set_ir_sender(action_dispatcher_t *dispatcher, action_dispatcher_send_cb_t cb, void *ctx)
{
    if (dispatcher == NULL) {
        return;
    }
    dispatcher->ir_send = cb;
    dispatcher->ir_ctx = ctx;
}

void action_dispatcher_set_local_ui_sender(action_dispatcher_t *dispatcher, action_dispatcher_send_cb_t cb, void *ctx)
{
    if (dispatcher == NULL) {
        return;
    }
    dispatcher->local_ui_send = cb;
    dispatcher->local_ui_ctx = ctx;
}

void action_dispatcher_set_speaker_sender(action_dispatcher_t *dispatcher, action_dispatcher_send_cb_t cb, void *ctx)
{
    if (dispatcher == NULL) {
        return;
    }
    dispatcher->speaker_send = cb;
    dispatcher->speaker_ctx = ctx;
}

bool action_enqueue_batch(action_dispatcher_t *dispatcher, const rule_event_batch_t *batch)
{
    if (dispatcher == NULL || batch == NULL || batch->event_count == 0 ||
        batch->event_count > RULE_MAX_ACTIONS_PER_RULE) {
        if (dispatcher != NULL) {
            dispatcher->last_result = make_result(ACTION_RESULT_INVALID_ARG, NULL);
        }
        return false;
    }
    const rule_event_t *event = &batch->events[0];
    if (!dispatcher->started) {
        dispatcher->last_result = make_result(ACTION_RESULT_NOT_STARTED, event);
        return false;
    }
#ifdef ESP_PLATFORM
    if (dispatcher->queue_handle == NULL || xQueueSend(dispatcher->queue_handle, batch, 0) != pdPASS) {
        dispatcher->last_result = make_result(ACTION_RESULT_QUEUE_FULL, event);
        return false;
    }
#else
    if (dispatcher->count >= ACTION_DISPATCHER_QUEUE_LEN) {
        dispatcher->last_result = make_result(ACTION_RESULT_QUEUE_FULL, event);
        return false;
    }
    dispatcher->queue[dispatcher->tail] = *batch;
    dispatcher->tail = (dispatcher->tail + 1u) % ACTION_DISPATCHER_QUEUE_LEN;
    dispatcher->count++;
#endif
    return true;
}

bool action_enqueue(action_dispatcher_t *dispatcher, const rule_event_t *event)
{
    if (event == NULL) {
        if (dispatcher != NULL) {
            dispatcher->last_result = make_result(ACTION_RESULT_INVALID_ARG, NULL);
        }
        return false;
    }
    rule_event_batch_t batch = {.event_count = 1};
    batch.events[0] = *event;
    return action_enqueue_batch(dispatcher, &batch);
}

static size_t execute_job(action_dispatcher_t *dispatcher, const action_job_t *job)
{
    for (size_t i = 0; i < job->event_count; ++i) {
        dispatcher->last_result = execute_event(dispatcher, &job->events[i]);
    }
    return job->event_count;
}

static size_t process_job(action_dispatcher_t *dispatcher)
{
#ifdef ESP_PLATFORM
    action_job_t job;
    if (dispatcher == NULL || !dispatcher->started || dispatcher->queue_handle == NULL ||
        xQueueReceive(dispatcher->queue_handle, &job, 0) != pdPASS) {
        return false;
    }
    return execute_job(dispatcher, &job);
#else
    if (dispatcher == NULL || !dispatcher->started || dispatcher->count == 0) {
        return false;
    }
    action_job_t job = dispatcher->queue[dispatcher->head];
    dispatcher->head = (dispatcher->head + 1u) % ACTION_DISPATCHER_QUEUE_LEN;
    dispatcher->count--;
    return execute_job(dispatcher, &job);
#endif
}

bool action_dispatcher_process_one(action_dispatcher_t *dispatcher)
{
    return process_job(dispatcher) > 0;
}

size_t action_dispatcher_process_all(action_dispatcher_t *dispatcher)
{
    size_t processed = 0;
    size_t count;
    while ((count = process_job(dispatcher)) > 0) {
        processed += count;
    }
    return processed;
}

void action_dispatcher_stop(action_dispatcher_t *dispatcher)
{
    if (dispatcher == NULL) {
        return;
    }
    dispatcher->started = false;
#ifdef ESP_PLATFORM
    dispatcher->stop_requested = true;
    if (dispatcher->worker_task != NULL) {
        vTaskDelete(dispatcher->worker_task);
        dispatcher->worker_task = NULL;
    }
    if (dispatcher->queue_handle != NULL) {
        vQueueDelete(dispatcher->queue_handle);
        dispatcher->queue_handle = NULL;
    }
#else
    dispatcher->head = 0;
    dispatcher->tail = 0;
    dispatcher->count = 0;
#endif
}

action_result_t action_dispatcher_get_last_result(const action_dispatcher_t *dispatcher)
{
    if (dispatcher == NULL) {
        return make_result(ACTION_RESULT_INVALID_ARG, NULL);
    }
    return dispatcher->last_result;
}


#ifdef ESP_PLATFORM
static void action_dispatcher_worker(void *arg)
{
    action_dispatcher_t *dispatcher = (action_dispatcher_t *)arg;
    action_job_t job;
    while (dispatcher != NULL && !dispatcher->stop_requested) {
        if (dispatcher->queue_handle != NULL && xQueueReceive(dispatcher->queue_handle, &job, portMAX_DELAY) == pdPASS) {
            (void)execute_job(dispatcher, &job);
        }
    }
    vTaskDelete(NULL);
}
#endif
