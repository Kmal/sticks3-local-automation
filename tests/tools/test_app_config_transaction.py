#!/usr/bin/env python3
"""Exercise production LCD transaction callbacks against the real host runtime."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
main = (ROOT / "src/app/main.c").read_text()
callbacks = main[main.index("static bool app_ui_load_automation("):main.index("static void app_rule_web_config_changed_cb(")]
# Reuse the host integration translation unit's dependency stubs and helpers.
integration = (ROOT / "tests/host/test_external_triggers_and_web.c").read_text().replace("int main(void)", "int original_main(void)")
prefix = r'''
#include "freertos/semphr.h"
#include <stdatomic.h>
#define ESP_LOGW(...) ((void)0)
#define portMAX_DELAY UINT32_MAX
int g_fake_semaphore_take_result;
static rule_runtime_t s_rule_runtime;
static rule_config_store_t s_rule_store;
static automation_config_t s_rule_config;
static atomic_bool s_rule_runtime_ready;
static SemaphoreHandle_t s_rule_mutex;
'''
tests = r'''
int main(void)
{
    automation_config_t config = preservation_config();
    ASSERT_TRUE(rule_runtime_init(&s_rule_runtime, &config));
    ASSERT_TRUE(rule_config_store_open(&s_rule_store));
    ASSERT_TRUE(rule_config_store_save(&s_rule_store, &config));
    s_rule_config = config;
    s_rule_runtime_ready = true;
    s_rule_mutex = xSemaphoreCreateMutex();
    g_fake_semaphore_take_result = pdTRUE;
    ui_model_set_automation_backend(app_ui_load_automation, app_ui_save_automation, NULL);
    ui_runtime_t ui;
    ui_runtime_init(&ui);
    ASSERT_TRUE(ui_runtime_load_automation(&ui, 0));
    ui.automations[0].enabled = true;
    ASSERT_TRUE(ui_runtime_save_automation(&ui, 0, UI_AUTOMATION_EDIT_ENABLED));
    config.rules[0].enabled = true;
    automation_config_t saved;
    ASSERT_TRUE(rule_config_store_load(&s_rule_store, &saved));
    ASSERT_TRUE(memcmp(&config, &saved, sizeof(config)) == 0);
    ASSERT_TRUE(memcmp(&saved, &s_rule_runtime.engine.config, sizeof(saved)) == 0);
    ASSERT_TRUE(memcmp(&saved, &s_rule_config, sizeof(saved)) == 0);
    /* A browser edit immediately after the LCD transaction sees its result. */
    rule_web_t web;
    ASSERT_TRUE(rule_web_start(&web, &s_rule_runtime, &s_rule_store));
    char response[32768];
    ASSERT_TRUE(edit_first_rule(&web, "{\"source\":\"sound.rms_dbfs\",\"name\":\"browser rename\",\"actions\":[{\"action\":\"http_post\"}]}", response, sizeof(response)));
    ASSERT_TRUE(strstr(response, "error") == NULL);
    ASSERT_TRUE(s_rule_runtime.engine.config.rules[0].enabled);
    /* A stale LCD slot changes only its chosen field in the latest config. */
    ui.automations[0].enabled = false;
    ASSERT_TRUE(ui_runtime_save_automation(&ui, 0, UI_AUTOMATION_EDIT_ENABLED));
    ASSERT_TRUE(!s_rule_runtime.engine.config.rules[0].enabled);
    ASSERT_TRUE(strcmp(s_rule_runtime.engine.config.rules[0].name, "browser rename") == 0);
    ASSERT_TRUE(memcmp(&config.rules[1], &s_rule_runtime.engine.config.rules[1], sizeof(config.rules[1])) == 0);
    const rule_engine_t before = s_rule_runtime.engine;
    config = s_rule_config;
    /* Inject persistence failure: no engine, mirror or stored changes. */
    s_rule_store.opened = false;
    ui.automations[0].enabled = true;
    ASSERT_FALSE(ui_runtime_save_automation(&ui, 0, UI_AUTOMATION_EDIT_ENABLED));
    ASSERT_TRUE(memcmp(&before, &s_rule_runtime.engine, sizeof(before)) == 0);
    ASSERT_TRUE(memcmp(&config, &s_rule_config, sizeof(config)) == 0);
    s_rule_store.opened = true;
    ASSERT_TRUE(rule_config_store_load(&s_rule_store, &saved));
    ASSERT_TRUE(memcmp(&config, &saved, sizeof(config)) == 0);
    /* Failed lock/startup cannot fall back to direct NVS writes. */
    g_fake_semaphore_take_result = pdFALSE;
    ASSERT_FALSE(ui_runtime_save_automation(&ui, 0, UI_AUTOMATION_EDIT_ENABLED));
    g_fake_semaphore_take_result = pdTRUE;
    s_rule_runtime_ready = false;
    ASSERT_FALSE(ui_runtime_save_automation(&ui, 0, UI_AUTOMATION_EDIT_ENABLED));
    ASSERT_TRUE(rule_config_store_load(&s_rule_store, &saved));
    ASSERT_TRUE(memcmp(&config, &saved, sizeof(config)) == 0);
    ui_model_set_automation_backend(NULL, NULL, NULL);
    puts("app LCD/Web configuration transaction tests passed");
    return 0;
}
'''
runner = (ROOT / "tests/host/run_host_tests.sh").read_text().splitlines()
index = next(i for i, line in enumerate(runner) if line.startswith("compile test_external_triggers_and_web "))
with tempfile.TemporaryDirectory() as temporary:
    source = Path(temporary) / "transaction.c"
    source.write_text(integration + prefix + callbacks + tests)
    command = "\n".join(runner[index:index + 2])
    command = command.replace('"${ROOT}/tests/host/test_external_triggers_and_web.c"', '"${TRANSACTION_SOURCE}"')
    shell_prefix = 'compile() { shift; "$CC" -std=c11 -Wall -Wextra -Werror "$@" -o "$TRANSACTION_EXE"; }\n'
    env = dict(os.environ, ROOT=str(ROOT), FAKE_INC=str(ROOT / "tests/host/fakes/esp_idf_stubs"),
               CC=os.environ.get("CC", "cc"), TRANSACTION_SOURCE=str(source), TRANSACTION_EXE=str(Path(temporary) / "transaction"))
    subprocess.run(["bash", "-c", shell_prefix + command], env=env, check=True)
    subprocess.run([env["TRANSACTION_EXE"]], check=True)
