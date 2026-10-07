#include "action_dispatcher.h"

#include <stdio.h>
#include <stdlib.h>

#define ASSERT_TRUE(value) do { if (!(value)) { fprintf(stderr, "%s:%d assertion failed: %s\n", __FILE__, __LINE__, #value); exit(1); } } while (0)
#define ASSERT_FALSE(value) ASSERT_TRUE(!(value))
#define ASSERT_EQ(expected, actual) do { if ((expected) != (actual)) { fprintf(stderr, "%s:%d expected %d got %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); exit(1); } } while (0)

static rule_event_t event_with_action(rule_action_type_t action, uint32_t sequence)
{
    rule_event_t event = {0};
    event.sequence = sequence;
    event.rule_id = 10 + sequence;
    event.action = action;
    return event;
}

static action_result_t fake_ble_sender(const rule_event_t *event, void *ctx)
{
    (void)ctx;
    action_result_t result = {
        .code = ACTION_RESULT_OK,
        .sequence = event->sequence,
        .rule_id = event->rule_id,
        .action = event->action,
    };
    return result;
}

static action_result_t fake_http_sender(const rule_event_t *event, void *ctx)
{
    (void)ctx;
    action_result_t result = {
        .code = ACTION_RESULT_OK,
        .sequence = event->sequence,
        .rule_id = event->rule_id,
        .action = event->action,
    };
    return result;
}

static action_result_t fake_ir_sender(const rule_event_t *event, void *ctx)
{
    (void)ctx;
    action_result_t result = {
        .code = ACTION_RESULT_OK,
        .sequence = event->sequence,
        .rule_id = event->rule_id,
        .action = event->action,
    };
    return result;
}


static action_result_t fake_speaker_sender(const rule_event_t *event, void *ctx)
{
    int *calls = (int *)ctx;
    if (calls != NULL) {
        (*calls)++;
    }
    action_result_t result = {
        .code = ACTION_RESULT_OK,
        .sequence = event->sequence,
        .rule_id = event->rule_id,
        .action = event->action,
    };
    return result;
}

static action_result_t fake_local_ui_sender(const rule_event_t *event, void *ctx)
{
    int *calls = (int *)ctx;
    if (calls != NULL) {
        (*calls)++;
    }
    action_result_t result = {
        .code = ACTION_RESULT_OK,
        .sequence = event->sequence,
        .rule_id = event->rule_id,
        .action = event->action,
    };
    return result;
}

static void test_not_started_and_queue_full(void)
{
    action_dispatcher_t dispatcher;
    action_dispatcher_init(&dispatcher);
    rule_event_t event = event_with_action(RULE_ACTION_LOCAL_UI, 1);
    ASSERT_FALSE(action_enqueue(&dispatcher, &event));
    ASSERT_EQ(ACTION_RESULT_NOT_STARTED, action_dispatcher_get_last_result(&dispatcher).code);

    ASSERT_TRUE(action_dispatcher_start(&dispatcher));
    for (size_t i = 0; i < ACTION_DISPATCHER_QUEUE_LEN; ++i) {
        event = event_with_action(RULE_ACTION_LOCAL_UI, (uint32_t)i + 1u);
        ASSERT_TRUE(action_enqueue(&dispatcher, &event));
    }
    event = event_with_action(RULE_ACTION_LOCAL_UI, 99);
    ASSERT_FALSE(action_enqueue(&dispatcher, &event));
    ASSERT_EQ(ACTION_RESULT_QUEUE_FULL, action_dispatcher_get_last_result(&dispatcher).code);
}

static void test_process_local_ui_and_unsupported(void)
{
    action_dispatcher_t dispatcher;
    ASSERT_TRUE(action_dispatcher_start(&dispatcher));
    rule_event_t event = event_with_action(RULE_ACTION_LOCAL_UI, 1);
    ASSERT_TRUE(action_enqueue(&dispatcher, &event));
    ASSERT_EQ(1, action_dispatcher_process_all(&dispatcher));
    ASSERT_EQ(ACTION_RESULT_OK, action_dispatcher_get_last_result(&dispatcher).code);

    event = event_with_action(RULE_ACTION_HTTP_POST, 2);
    ASSERT_TRUE(action_enqueue(&dispatcher, &event));
    ASSERT_TRUE(action_dispatcher_process_one(&dispatcher));
    ASSERT_EQ(ACTION_RESULT_UNSUPPORTED, action_dispatcher_get_last_result(&dispatcher).code);
}

static void test_ble_callback_action(void)
{
    action_dispatcher_t dispatcher;
    ASSERT_TRUE(action_dispatcher_start(&dispatcher));
    action_dispatcher_set_ble_sender(&dispatcher, fake_ble_sender, NULL);
    rule_event_t event = event_with_action(RULE_ACTION_BLE_MESSAGE, 3);
    ASSERT_TRUE(action_enqueue(&dispatcher, &event));
    ASSERT_TRUE(action_dispatcher_process_one(&dispatcher));
    ASSERT_EQ(ACTION_RESULT_OK, action_dispatcher_get_last_result(&dispatcher).code);
    ASSERT_EQ(RULE_ACTION_BLE_MESSAGE, action_dispatcher_get_last_result(&dispatcher).action);
}

static void test_http_callback_action(void)
{
    action_dispatcher_t dispatcher;
    ASSERT_TRUE(action_dispatcher_start(&dispatcher));
    action_dispatcher_set_http_sender(&dispatcher, fake_http_sender, NULL);
    rule_event_t event = event_with_action(RULE_ACTION_HTTP_POST, 4);
    ASSERT_TRUE(action_enqueue(&dispatcher, &event));
    ASSERT_TRUE(action_dispatcher_process_one(&dispatcher));
    ASSERT_EQ(ACTION_RESULT_OK, action_dispatcher_get_last_result(&dispatcher).code);
    ASSERT_EQ(RULE_ACTION_HTTP_POST, action_dispatcher_get_last_result(&dispatcher).action);
}

static void test_ir_callback_action(void)
{
    action_dispatcher_t dispatcher;
    ASSERT_TRUE(action_dispatcher_start(&dispatcher));
    action_dispatcher_set_ir_sender(&dispatcher, fake_ir_sender, NULL);
    rule_event_t event = event_with_action(RULE_ACTION_IR_SEND, 5);
    ASSERT_TRUE(action_enqueue(&dispatcher, &event));
    ASSERT_TRUE(action_dispatcher_process_one(&dispatcher));
    ASSERT_EQ(ACTION_RESULT_OK, action_dispatcher_get_last_result(&dispatcher).code);
    ASSERT_EQ(RULE_ACTION_IR_SEND, action_dispatcher_get_last_result(&dispatcher).action);
}


static void test_speaker_callback_action(void)
{
    action_dispatcher_t dispatcher;
    int calls = 0;
    ASSERT_TRUE(action_dispatcher_start(&dispatcher));
    action_dispatcher_set_speaker_sender(&dispatcher, fake_speaker_sender, &calls);
    rule_event_t event = event_with_action(RULE_ACTION_SPEAKER_TONE, 7);
    ASSERT_TRUE(action_enqueue(&dispatcher, &event));
    ASSERT_TRUE(action_dispatcher_process_one(&dispatcher));
    ASSERT_EQ(1, calls);
    ASSERT_EQ(ACTION_RESULT_OK, action_dispatcher_get_last_result(&dispatcher).code);
    ASSERT_EQ(RULE_ACTION_SPEAKER_TONE, action_dispatcher_get_last_result(&dispatcher).action);
}

static void test_local_ui_callback_action(void)
{
    action_dispatcher_t dispatcher;
    int calls = 0;
    ASSERT_TRUE(action_dispatcher_start(&dispatcher));
    action_dispatcher_set_local_ui_sender(&dispatcher, fake_local_ui_sender, &calls);
    rule_event_t event = event_with_action(RULE_ACTION_LOCAL_UI, 6);
    ASSERT_TRUE(action_enqueue(&dispatcher, &event));
    ASSERT_TRUE(action_dispatcher_process_one(&dispatcher));
    ASSERT_EQ(1, calls);
    ASSERT_EQ(ACTION_RESULT_OK, action_dispatcher_get_last_result(&dispatcher).code);
    ASSERT_EQ(RULE_ACTION_LOCAL_UI, action_dispatcher_get_last_result(&dispatcher).action);
}

static void test_network_backlog_does_not_block_local_jobs(void)
{
    action_dispatcher_t dispatcher; ASSERT_TRUE(action_dispatcher_start(&dispatcher));
    action_dispatcher_set_http_sender(&dispatcher, fake_http_sender, NULL);
    for (unsigned i = 0; i < ACTION_DISPATCHER_QUEUE_LEN; ++i) {
        rule_event_t http = event_with_action(RULE_ACTION_HTTP_POST, i+1);
        ASSERT_TRUE(action_enqueue(&dispatcher, &http));
    }
    rule_event_t local = event_with_action(RULE_ACTION_LOCAL_UI, 99);
    ASSERT_TRUE(action_enqueue(&dispatcher, &local));
    ASSERT_TRUE(action_dispatcher_process_one(&dispatcher));
    ASSERT_EQ(99, action_dispatcher_get_last_result(&dispatcher).sequence);
    ASSERT_EQ(ACTION_DISPATCHER_QUEUE_LEN, dispatcher.network_count);
    ASSERT_EQ(ACTION_DISPATCHER_QUEUE_LEN, action_dispatcher_process_all(&dispatcher));
    action_dispatcher_stop(&dispatcher);
}

static uint32_t ordered_sequences[3];
static size_t ordered_count;
static action_result_t capture_order(const rule_event_t *event, void *ctx)
{
    (void)ctx;
    ASSERT_TRUE(ordered_count < 3);
    ordered_sequences[ordered_count++] = event->sequence;
    return fake_http_sender(event, NULL);
}
static void test_mixed_job_preserves_order_and_whole_admission(void)
{
    action_dispatcher_t dispatcher;
    ASSERT_TRUE(action_dispatcher_start(&dispatcher));
    action_dispatcher_set_http_sender(&dispatcher, capture_order, NULL);
    action_dispatcher_set_local_ui_sender(&dispatcher, capture_order, NULL);
    rule_event_batch_t job = {.event_count = 3};
    job.events[0] = event_with_action(RULE_ACTION_LOCAL_UI, 1);
    job.events[1] = event_with_action(RULE_ACTION_HTTP_POST, 2);
    job.events[2] = event_with_action(RULE_ACTION_LOCAL_UI, 3);
    ASSERT_TRUE(action_enqueue_batch(&dispatcher, &job));
    ASSERT_EQ(0, dispatcher.count);
    ASSERT_EQ(1, dispatcher.network_count);
    ordered_count = 0;
    ASSERT_TRUE(action_dispatcher_process_one(&dispatcher));
    ASSERT_EQ(3, ordered_count);
    for (unsigned i = 0; i < 3; ++i) ASSERT_EQ(i+1, ordered_sequences[i]);
    for (unsigned i = 0; i < ACTION_DISPATCHER_QUEUE_LEN; ++i)
        ASSERT_TRUE(action_enqueue_batch(&dispatcher, &job));
    ASSERT_FALSE(action_enqueue_batch(&dispatcher, &job));
    ASSERT_EQ(ACTION_DISPATCHER_QUEUE_LEN, dispatcher.network_count);
    action_dispatcher_stop(&dispatcher);
}

int main(void)
{
    test_network_backlog_does_not_block_local_jobs();
    test_mixed_job_preserves_order_and_whole_admission();
    test_not_started_and_queue_full();
    test_process_local_ui_and_unsupported();
    test_ble_callback_action();
    test_http_callback_action();
    test_ir_callback_action();
    test_local_ui_callback_action();
    test_speaker_callback_action();
    puts("action_dispatcher tests passed");
    return 0;
}
