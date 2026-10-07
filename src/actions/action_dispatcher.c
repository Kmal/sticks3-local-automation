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
static void action_dispatcher_network_worker(void *arg);
#endif

static void set_last_result(action_dispatcher_t *dispatcher, action_result_t result)
{
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&dispatcher->result_lock);
#endif
    dispatcher->last_result = result;
#ifdef ESP_PLATFORM
    portEXIT_CRITICAL(&dispatcher->result_lock);
#endif
}

static action_result_t execute_event_unlocked(action_dispatcher_t *dispatcher, const rule_event_t *event)
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

/* Mixed network jobs may include hardware actions. Serialize those actions
 * across workers, but never hold this lock during an HTTP request. */
static action_result_t execute_event(action_dispatcher_t *dispatcher, const rule_event_t *event)
{
#ifdef ESP_PLATFORM
    bool local = event != NULL && event->action != RULE_ACTION_HTTP_POST;
    if (local && xSemaphoreTake(dispatcher->local_execution_lock, portMAX_DELAY) != pdTRUE)
        return make_result(ACTION_RESULT_NOT_STARTED, event);
#endif
    action_result_t result = execute_event_unlocked(dispatcher, event);
#ifdef ESP_PLATFORM
    if (local) xSemaphoreGive(dispatcher->local_execution_lock);
#endif
    return result;
}

void action_dispatcher_init(action_dispatcher_t *dispatcher)
{
    if (dispatcher == NULL) {
        return;
    }
    memset(dispatcher, 0, sizeof(*dispatcher));
#ifdef ESP_PLATFORM
    dispatcher->result_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    atomic_init(&dispatcher->stop_requested, false);
#endif
    set_last_result(dispatcher, make_result(ACTION_RESULT_NOT_STARTED, NULL));
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
        set_last_result(dispatcher, make_result(ACTION_RESULT_QUEUE_FULL, NULL));
        return false;
    }
    dispatcher->network_queue_handle = xQueueCreate(ACTION_DISPATCHER_QUEUE_LEN, sizeof(action_job_t));
    if (dispatcher->network_queue_handle == NULL) {
        vQueueDelete(dispatcher->queue_handle); dispatcher->queue_handle = NULL;
        return false;
    }
    dispatcher->local_execution_lock = xSemaphoreCreateMutex();
    if (dispatcher->local_execution_lock == NULL) {
        action_dispatcher_stop(dispatcher);
        return false;
    }
    dispatcher->stop_requested = false;
    if (xTaskCreate(action_dispatcher_worker, "rule_action_worker", 4096, dispatcher, tskIDLE_PRIORITY + 1, &dispatcher->worker_task) != pdPASS) {
        vQueueDelete(dispatcher->queue_handle);
        vQueueDelete(dispatcher->network_queue_handle);
        dispatcher->network_queue_handle = NULL;
        dispatcher->queue_handle = NULL;
        vSemaphoreDelete(dispatcher->local_execution_lock); dispatcher->local_execution_lock = NULL;
        set_last_result(dispatcher, make_result(ACTION_RESULT_NOT_STARTED, NULL));
        return false;
    }
#endif
#ifdef ESP_PLATFORM
    if (xTaskCreate(action_dispatcher_network_worker, "rule_network_worker", 4096, dispatcher,
                    tskIDLE_PRIORITY + 1, &dispatcher->network_worker_task) != pdPASS) {
        action_dispatcher_stop(dispatcher);
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
            set_last_result(dispatcher, make_result(ACTION_RESULT_INVALID_ARG, NULL));
        }
        return false;
    }
    bool network = false;
    for (size_t i = 0; i < batch->event_count; ++i) network |= batch->events[i].action == RULE_ACTION_HTTP_POST;
    const rule_event_t *event = &batch->events[0];
    if (!dispatcher->started) {
        set_last_result(dispatcher, make_result(ACTION_RESULT_NOT_STARTED, event));
        return false;
    }
#ifdef ESP_PLATFORM
    QueueHandle_t queue = network ? dispatcher->network_queue_handle : dispatcher->queue_handle;
    if (queue == NULL || xQueueSend(queue, batch, 0) != pdPASS) {
        set_last_result(dispatcher, make_result(ACTION_RESULT_QUEUE_FULL, event));
        return false;
    }
#else
    size_t *count = network ? &dispatcher->network_count : &dispatcher->count;
    size_t *tail = network ? &dispatcher->network_tail : &dispatcher->tail;
    action_job_t *queue = network ? dispatcher->network_queue : dispatcher->queue;
    if (*count >= ACTION_DISPATCHER_QUEUE_LEN) {
        set_last_result(dispatcher, make_result(ACTION_RESULT_QUEUE_FULL, event));
        return false;
    }
    queue[*tail] = *batch;
    *tail = (*tail + 1u) % ACTION_DISPATCHER_QUEUE_LEN;
    ++*count;
#endif
    return true;
}

bool action_enqueue(action_dispatcher_t *dispatcher, const rule_event_t *event)
{
    if (event == NULL) {
        if (dispatcher != NULL) {
            set_last_result(dispatcher, make_result(ACTION_RESULT_INVALID_ARG, NULL));
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
        set_last_result(dispatcher, execute_event(dispatcher, &job->events[i]));
    }
    return job->event_count;
}

static size_t process_job(action_dispatcher_t *dispatcher)
{
    if (dispatcher == NULL || !dispatcher->started) return 0;
    action_job_t job;
#ifdef ESP_PLATFORM
    if (xQueueReceive(dispatcher->queue_handle, &job, 0) != pdPASS &&
        xQueueReceive(dispatcher->network_queue_handle, &job, 0) != pdPASS) return 0;
#else
    const bool network = dispatcher->count == 0;
    size_t *count = network ? &dispatcher->network_count : &dispatcher->count;
    size_t *head = network ? &dispatcher->network_head : &dispatcher->head;
    action_job_t *queue = network ? dispatcher->network_queue : dispatcher->queue;
    if (*count == 0) return 0;
    job = queue[*head];
    *head = (*head + 1u) % ACTION_DISPATCHER_QUEUE_LEN;
    --*count;
#endif
    return execute_job(dispatcher, &job);
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
    if (dispatcher->network_worker_task != NULL) {
        vTaskDelete(dispatcher->network_worker_task); dispatcher->network_worker_task = NULL;
    }
    if (dispatcher->network_queue_handle != NULL) {
        vQueueDelete(dispatcher->network_queue_handle); dispatcher->network_queue_handle = NULL;
    }
    if (dispatcher->worker_task != NULL) {
        vTaskDelete(dispatcher->worker_task);
        dispatcher->worker_task = NULL;
    }
    if (dispatcher->queue_handle != NULL) {
        vQueueDelete(dispatcher->queue_handle);
        dispatcher->queue_handle = NULL;
    }
    if (dispatcher->local_execution_lock != NULL) {
        vSemaphoreDelete(dispatcher->local_execution_lock); dispatcher->local_execution_lock = NULL;
    }
#else
    dispatcher->head = 0;
    dispatcher->tail = 0;
    dispatcher->count = 0;
    dispatcher->network_count = dispatcher->network_head = dispatcher->network_tail = 0;
#endif
}

action_result_t action_dispatcher_get_last_result(const action_dispatcher_t *dispatcher)
{
    if (dispatcher == NULL) {
        return make_result(ACTION_RESULT_INVALID_ARG, NULL);
    }
#ifdef ESP_PLATFORM
    portMUX_TYPE *lock = (portMUX_TYPE *)&dispatcher->result_lock;
    portENTER_CRITICAL(lock);
#endif
    const action_result_t result = dispatcher->last_result;
#ifdef ESP_PLATFORM
    portEXIT_CRITICAL(lock);
#endif
    return result;
}


#ifdef ESP_PLATFORM
static void action_dispatcher_worker_loop(void *arg, bool network)
{
    action_dispatcher_t *dispatcher = (action_dispatcher_t *)arg;
    action_job_t job;
    QueueHandle_t queue = network ? dispatcher->network_queue_handle : dispatcher->queue_handle;
    while (dispatcher != NULL && !dispatcher->stop_requested) {
        if (queue != NULL && xQueueReceive(queue, &job, portMAX_DELAY) == pdPASS) {
            (void)execute_job(dispatcher, &job);
        }
    }
    vTaskDelete(NULL);
}
static void action_dispatcher_worker(void *arg) { action_dispatcher_worker_loop(arg, false); }
static void action_dispatcher_network_worker(void *arg) { action_dispatcher_worker_loop(arg, true); }
#endif
