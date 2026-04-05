/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Tests for the zsys logging subsystem.
 */

#include "unity.h"
#include "zsys/log.h"
#include "zsys/log_backend.h"

#include <string.h>

/* -----------------------------------------------------------------------
 * Test capture backend
 * ----------------------------------------------------------------------- */

#define TEST_CAPTURE_MAX 16

static struct log_msg _captured[TEST_CAPTURE_MAX];
static volatile int _capture_count = 0;
static volatile bool _capture_active = false;

static void test_backend_init(const struct log_backend *backend)
{
    (void)backend;
}

static void test_backend_put(const struct log_backend *backend,
                             const struct log_msg *msg)
{
    (void)backend;
    if (_capture_active && _capture_count < TEST_CAPTURE_MAX) {
        memcpy(&_captured[_capture_count], msg, sizeof(struct log_msg));
        _capture_count++;
    }
}

static void test_backend_panic(const struct log_backend *backend)
{
    (void)backend;
}

static const struct log_backend_api _test_backend_api = {
    .init  = test_backend_init,
    .put   = test_backend_put,
    .panic = test_backend_panic,
};

/* Registered via constructor -- will be active for all tests */
LOG_BACKEND_DEFINE(test_capture, &_test_backend_api, NULL);

static void capture_reset(void)
{
    _capture_count = 0;
    memset(_captured, 0, sizeof(_captured));
    _capture_active = true;
}

static void capture_stop(void)
{
    _capture_active = false;
}

/* -----------------------------------------------------------------------
 * Module registration for this test file
 * ----------------------------------------------------------------------- */

LOG_MODULE_REGISTER(test_log, LOG_LEVEL_DBG);

/* -----------------------------------------------------------------------
 * Tests
 * ----------------------------------------------------------------------- */

static void test_log_module_register(void)
{
    /* This file's module should be registered by the constructor */
    int count = zsys_log_get_module_count();
    TEST_ASSERT_GREATER_THAN(0, count);

    /* Find our module */
    bool found = false;
    for (int i = 0; i < count; i++) {
        const char *name;
        int level;
        TEST_ASSERT_EQUAL(0, zsys_log_get_module_info(i, &name, &level));
        if (strcmp(name, "test_log") == 0) {
            found = true;
            TEST_ASSERT_EQUAL(LOG_LEVEL_DBG, level);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

static void test_log_level_constants(void)
{
    /* Verify level ordering: NONE < ERR < WRN < INF < DBG */
    TEST_ASSERT_LESS_THAN(LOG_LEVEL_ERR, LOG_LEVEL_NONE);
    TEST_ASSERT_LESS_THAN(LOG_LEVEL_WRN, LOG_LEVEL_ERR);
    TEST_ASSERT_LESS_THAN(LOG_LEVEL_INF, LOG_LEVEL_WRN);
    TEST_ASSERT_LESS_THAN(LOG_LEVEL_DBG, LOG_LEVEL_INF);
}

static void test_log_sync_output(void)
{
    capture_reset();

    LOG_INF("sync test message %d", 42);

    /* In sync mode, message should arrive immediately */
    TEST_ASSERT_GREATER_THAN(0, _capture_count);

    struct log_msg *msg = &_captured[_capture_count - 1];
    TEST_ASSERT_EQUAL(LOG_LEVEL_INF, msg->level);
    TEST_ASSERT_EQUAL_STRING("test_log", msg->module);
    TEST_ASSERT_NOT_NULL(strstr(msg->text, "sync test message 42"));
    TEST_ASSERT_TRUE(msg->timestamp_ms > 0);

    capture_stop();
}

static void test_log_runtime_level_filter(void)
{
    capture_reset();

    /* Set level to WRN -- INF and DBG should be filtered */
    zsys_log_set_level("test_log", (esp_log_level_t)LOG_LEVEL_WRN);

    LOG_INF("should be filtered");
    LOG_DBG("should also be filtered");
    LOG_WRN("should pass");
    LOG_ERR("should also pass");

    /* Only WRN and ERR should have been captured */
    TEST_ASSERT_EQUAL(2, _capture_count);
    TEST_ASSERT_EQUAL(LOG_LEVEL_WRN, _captured[0].level);
    TEST_ASSERT_EQUAL(LOG_LEVEL_ERR, _captured[1].level);

    /* Restore level */
    zsys_log_set_level("test_log", (esp_log_level_t)LOG_LEVEL_DBG);

    capture_stop();
}

static void test_log_all_levels(void)
{
    capture_reset();

    LOG_ERR("error msg");
    LOG_WRN("warn msg");
    LOG_INF("info msg");
    LOG_DBG("debug msg");

    TEST_ASSERT_EQUAL(4, _capture_count);
    TEST_ASSERT_EQUAL(LOG_LEVEL_ERR, _captured[0].level);
    TEST_ASSERT_EQUAL(LOG_LEVEL_WRN, _captured[1].level);
    TEST_ASSERT_EQUAL(LOG_LEVEL_INF, _captured[2].level);
    TEST_ASSERT_EQUAL(LOG_LEVEL_DBG, _captured[3].level);

    capture_stop();
}

static void test_log_format_msg(void)
{
    struct log_msg msg = {
        .timestamp_ms = 12345,  /* 12.345 seconds */
        .level = LOG_LEVEL_INF,
        .module = "mymod",
        .thread = "main",
        .text = "hello world",
    };

    char buf[128];
    int len = zsys_log_format_msg(&msg, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);

    /* Check format: [12.345] <INF> mymod: hello world */
    TEST_ASSERT_NOT_NULL(strstr(buf, "[12.345]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<INF>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "mymod:"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "hello world"));
}

static void test_log_thread_name(void)
{
    capture_reset();

    LOG_INF("thread name test");

    TEST_ASSERT_GREATER_THAN(0, _capture_count);
    /* Thread name should not be empty */
    TEST_ASSERT_GREATER_THAN((size_t)0, strlen(_captured[0].thread));

    capture_stop();
}

static void test_log_message_truncation(void)
{
    capture_reset();

    /* Build a message longer than CONFIG_ZSYS_LOG_MSG_MAX_LEN */
    char long_msg[256];
    memset(long_msg, 'A', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';

    LOG_INF("%s", long_msg);

    TEST_ASSERT_GREATER_THAN(0, _capture_count);
    /* Text should be truncated: strlen + null must fit in MSG_MAX_LEN */
    TEST_ASSERT_LESS_OR_EQUAL((size_t)CONFIG_ZSYS_LOG_MSG_MAX_LEN,
                              strlen(_captured[0].text) + 1);

    capture_stop();
}

static void test_log_backend_count(void)
{
    /* At least 2 backends should be registered:
     * the default ESP backend and our test capture backend */
    /* We can verify by checking that our test backend receives messages */
    capture_reset();
    LOG_INF("backend count test");
    TEST_ASSERT_GREATER_THAN(0, _capture_count);
    capture_stop();
}

static void test_log_dropped_count(void)
{
    /* In sync mode, no messages should be dropped */
    uint32_t dropped = zsys_log_get_dropped_count();
    TEST_ASSERT_EQUAL(0, dropped);
}

static void test_log_hexdump(void)
{
    capture_reset();

    uint8_t data[] = { 0x00, 0x01, 0x02, 0x03, 0xAA, 0xBB, 0xCC, 0xFF };
    LOG_HEXDUMP_INF(data, sizeof(data), "test data");

    /* Should get 2 messages: label line + 1 hex line (8 bytes < 16) */
    TEST_ASSERT_GREATER_OR_EQUAL(2, _capture_count);

    /* First message: label */
    TEST_ASSERT_NOT_NULL(strstr(_captured[0].text, "test data"));
    TEST_ASSERT_NOT_NULL(strstr(_captured[0].text, "8 bytes"));

    /* Second message: hex values */
    TEST_ASSERT_NOT_NULL(strstr(_captured[1].text, "00"));
    TEST_ASSERT_NOT_NULL(strstr(_captured[1].text, "FF"));

    capture_stop();
}

static void test_log_hexdump_multiline(void)
{
    capture_reset();

    uint8_t data[32];
    for (int i = 0; i < 32; i++) {
        data[i] = (uint8_t)i;
    }
    LOG_HEXDUMP_DBG(data, sizeof(data), "multi");

    /* 1 label + 2 hex lines (32 bytes / 16 per line) */
    TEST_ASSERT_GREATER_OR_EQUAL(3, _capture_count);

    /* First hex line should have 00..0F */
    TEST_ASSERT_NOT_NULL(strstr(_captured[1].text, "00"));
    TEST_ASSERT_NOT_NULL(strstr(_captured[1].text, "0F"));

    /* Second hex line should have 10..1F */
    TEST_ASSERT_NOT_NULL(strstr(_captured[2].text, "10"));
    TEST_ASSERT_NOT_NULL(strstr(_captured[2].text, "1F"));

    capture_stop();
}

/* -----------------------------------------------------------------------
 * Test group
 * ----------------------------------------------------------------------- */

void test_log_group(void)
{
    /* Initialize the log subsystem for testing.
     * This inits backends (including our test capture backend). */
    zsys_log_init();

    RUN_TEST(test_log_module_register);
    RUN_TEST(test_log_level_constants);
    RUN_TEST(test_log_sync_output);
    RUN_TEST(test_log_runtime_level_filter);
    RUN_TEST(test_log_all_levels);
    RUN_TEST(test_log_format_msg);
    RUN_TEST(test_log_thread_name);
    RUN_TEST(test_log_message_truncation);
    RUN_TEST(test_log_backend_count);
    RUN_TEST(test_log_dropped_count);
    RUN_TEST(test_log_hexdump);
    RUN_TEST(test_log_hexdump_multiline);
}
