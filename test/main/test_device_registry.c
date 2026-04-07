/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Tests for device registry and device lifecycle.
 */

#include "unity.h"
#include "device_model.h"

/* ----------------------------------------------------------------
 * Mock devices for testing
 * ---------------------------------------------------------------- */

static int mock_init_ok(const struct device *dev)
{
    (void)dev;
    return ESP_OK;
}

static int mock_init_fail(const struct device *dev)
{
    (void)dev;
    return ESP_FAIL;
}

/* These register into the global device registry via constructors */
DEVICE_DEFINE(test_dev_a, mock_init_ok, NULL, NULL, NULL, NULL);
DEVICE_DEFINE(test_dev_b, mock_init_ok, NULL, NULL, NULL, &test_dev_a);
DEVICE_DEFINE(test_dev_fail, mock_init_fail, NULL, NULL, NULL, NULL);

/* ----------------------------------------------------------------
 * Registry tests
 * ---------------------------------------------------------------- */

static void test_device_get_binding_found(void)
{
    const struct device *dev = device_get_binding("test_dev_a");
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_EQUAL_STRING("test_dev_a", dev->name);
}

static void test_device_get_binding_found_b(void)
{
    const struct device *dev = device_get_binding("test_dev_b");
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_EQUAL_STRING("test_dev_b", dev->name);
    /* Verify bus pointer */
    TEST_ASSERT_NOT_NULL(dev->bus);
    TEST_ASSERT_EQUAL_STRING("test_dev_a", dev->bus->name);
}

static void test_device_get_binding_not_found(void)
{
    const struct device *dev = device_get_binding("nonexistent");
    TEST_ASSERT_NULL(dev);
}

static void test_device_get_binding_null(void)
{
    const struct device *dev = device_get_binding(NULL);
    TEST_ASSERT_NULL(dev);
}

static void test_device_count(void)
{
    /* At least 3 test devices registered */
    TEST_ASSERT_TRUE(device_get_count() >= 3);
}

static void test_device_get_by_index(void)
{
    size_t count = device_get_count();
    for (size_t i = 0; i < count; i++) {
        TEST_ASSERT_NOT_NULL(device_get_by_index(i));
    }
    /* Out of range returns NULL */
    TEST_ASSERT_NULL(device_get_by_index(count));
}

/* ----------------------------------------------------------------
 * Lifecycle tests
 * ---------------------------------------------------------------- */

static void test_device_not_ready_before_init(void)
{
    /* test_dev_fail hasn't been initialized yet (or failed) */
    const struct device *dev = device_get_binding("test_dev_fail");
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_FALSE(device_is_ready(dev));
}

static void test_device_init_sets_ready(void)
{
    const struct device *dev = device_get_binding("test_dev_a");
    TEST_ASSERT_NOT_NULL(dev);

    /* Reset ready flag for this test */
    *dev->ready = false;
    TEST_ASSERT_FALSE(device_is_ready(dev));

    esp_err_t ret = device_init(dev);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(device_is_ready(dev));
}

static void test_device_init_fail_not_ready(void)
{
    const struct device *dev = device_get_binding("test_dev_fail");
    TEST_ASSERT_NOT_NULL(dev);

    *dev->ready = false;
    esp_err_t ret = device_init(dev);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(device_is_ready(dev));
}

static void test_device_is_ready_null(void)
{
    TEST_ASSERT_FALSE(device_is_ready(NULL));
}

/* ----------------------------------------------------------------
 * Test group
 * ---------------------------------------------------------------- */

void test_device_registry_group(void)
{
    RUN_TEST(test_device_get_binding_found);
    RUN_TEST(test_device_get_binding_found_b);
    RUN_TEST(test_device_get_binding_not_found);
    RUN_TEST(test_device_get_binding_null);
    RUN_TEST(test_device_count);
    RUN_TEST(test_device_get_by_index);
    RUN_TEST(test_device_not_ready_before_init);
    RUN_TEST(test_device_init_sets_ready);
    RUN_TEST(test_device_init_fail_not_ready);
    RUN_TEST(test_device_is_ready_null);
}
