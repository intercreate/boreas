/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Unit tests for shell parsing, command registration, history,
 * and tab completion logic.
 */

#include <string.h>

#include "unity.h"
#include "zshell/shell.h"

/* ----------------------------------------------------------------
 * Argv tokenizer tests
 * ---------------------------------------------------------------- */

static void test_shell_make_argv_simple(void)
{
    char line[] = "hello world foo";
    char *argv[8];
    size_t argc = shell_make_argv(line, argv, 8);

    TEST_ASSERT_EQUAL(3, argc);
    TEST_ASSERT_EQUAL_STRING("hello", argv[0]);
    TEST_ASSERT_EQUAL_STRING("world", argv[1]);
    TEST_ASSERT_EQUAL_STRING("foo", argv[2]);
}

static void test_shell_make_argv_extra_spaces(void)
{
    char line[] = "  a   b  c  ";
    char *argv[8];
    size_t argc = shell_make_argv(line, argv, 8);

    TEST_ASSERT_EQUAL(3, argc);
    TEST_ASSERT_EQUAL_STRING("a", argv[0]);
    TEST_ASSERT_EQUAL_STRING("b", argv[1]);
    TEST_ASSERT_EQUAL_STRING("c", argv[2]);
}

static void test_shell_make_argv_quoted(void)
{
    char line[] = "set name \"hello world\"";
    char *argv[8];
    size_t argc = shell_make_argv(line, argv, 8);

    TEST_ASSERT_EQUAL(3, argc);
    TEST_ASSERT_EQUAL_STRING("set", argv[0]);
    TEST_ASSERT_EQUAL_STRING("name", argv[1]);
    TEST_ASSERT_EQUAL_STRING("hello world", argv[2]);
}

static void test_shell_make_argv_empty(void)
{
    char line[] = "   ";
    char *argv[8];
    size_t argc = shell_make_argv(line, argv, 8);

    TEST_ASSERT_EQUAL(0, argc);
}

static void test_shell_make_argv_max_argc(void)
{
    char line[] = "a b c d e f";
    char *argv[3];
    size_t argc = shell_make_argv(line, argv, 3);

    TEST_ASSERT_EQUAL(3, argc);
    TEST_ASSERT_EQUAL_STRING("a", argv[0]);
    TEST_ASSERT_EQUAL_STRING("b", argv[1]);
    TEST_ASSERT_EQUAL_STRING("c", argv[2]);
}

/* ----------------------------------------------------------------
 * Command registration tests
 * ---------------------------------------------------------------- */

static int dummy_handler(const struct shell *sh, size_t argc, char **argv)
{
    (void)sh; (void)argc; (void)argv;
    return 42;
}

static void test_shell_cmd_register_and_sort(void)
{
    /* Save and reset state */
    size_t saved_count = _shell_root_cmd_count;
    struct shell_static_entry *saved[CONFIG_ZSHELL_MAX_ROOT_CMDS];
    memcpy(saved, _shell_root_cmds,
           saved_count * sizeof(struct shell_static_entry *));

    _shell_root_cmd_count = 0;

    /* Register in reverse alphabetical order */
    static struct shell_static_entry cmd_z = {
        .syntax = "zebra", .help = "z cmd", .handler = dummy_handler,
    };
    static struct shell_static_entry cmd_a = {
        .syntax = "apple", .help = "a cmd", .handler = dummy_handler,
    };
    static struct shell_static_entry cmd_m = {
        .syntax = "mango", .help = "m cmd", .handler = dummy_handler,
    };

    _shell_root_cmds[_shell_root_cmd_count++] = &cmd_z;
    _shell_root_cmds[_shell_root_cmd_count++] = &cmd_a;
    _shell_root_cmds[_shell_root_cmd_count++] = &cmd_m;

    shell_cmd_sort_root();

    TEST_ASSERT_EQUAL(3, _shell_root_cmd_count);
    TEST_ASSERT_EQUAL_STRING("apple", _shell_root_cmds[0]->syntax);
    TEST_ASSERT_EQUAL_STRING("mango", _shell_root_cmds[1]->syntax);
    TEST_ASSERT_EQUAL_STRING("zebra", _shell_root_cmds[2]->syntax);

    /* Restore state */
    _shell_root_cmd_count = saved_count;
    memcpy(_shell_root_cmds, saved,
           saved_count * sizeof(struct shell_static_entry *));
}

/* ----------------------------------------------------------------
 * History tests
 * ---------------------------------------------------------------- */

#ifdef CONFIG_ZSHELL_HISTORY

static void test_shell_history_add_and_navigate(void)
{
    struct shell_history hist;
    shell_history_init(&hist);

    shell_history_add(&hist, "first", 5);
    shell_history_add(&hist, "second", 6);
    shell_history_add(&hist, "third", 5);

    char buf[CONFIG_ZSHELL_CMD_BUFF_SIZE];
    uint16_t len;

    /* Up: should get "third" (most recent) */
    TEST_ASSERT_TRUE(shell_history_get(&hist, true, buf, &len));
    TEST_ASSERT_EQUAL_STRING("third", buf);

    /* Up: should get "second" */
    TEST_ASSERT_TRUE(shell_history_get(&hist, true, buf, &len));
    TEST_ASSERT_EQUAL_STRING("second", buf);

    /* Up: should get "first" */
    TEST_ASSERT_TRUE(shell_history_get(&hist, true, buf, &len));
    TEST_ASSERT_EQUAL_STRING("first", buf);

    /* Down: should get "second" */
    TEST_ASSERT_TRUE(shell_history_get(&hist, false, buf, &len));
    TEST_ASSERT_EQUAL_STRING("second", buf);

    /* Down: should get "third" */
    TEST_ASSERT_TRUE(shell_history_get(&hist, false, buf, &len));
    TEST_ASSERT_EQUAL_STRING("third", buf);

    /* Down: past newest, should return false */
    TEST_ASSERT_FALSE(shell_history_get(&hist, false, buf, &len));
}

static void test_shell_history_skip_duplicate(void)
{
    struct shell_history hist;
    shell_history_init(&hist);

    shell_history_add(&hist, "same", 4);
    shell_history_add(&hist, "same", 4);

    /* Should only have one entry */
    char buf[CONFIG_ZSHELL_CMD_BUFF_SIZE];
    uint16_t len;

    TEST_ASSERT_TRUE(shell_history_get(&hist, true, buf, &len));
    TEST_ASSERT_EQUAL_STRING("same", buf);

    /* Up again: should stay on "same" (only entry) */
    TEST_ASSERT_TRUE(shell_history_get(&hist, true, buf, &len));
    TEST_ASSERT_EQUAL_STRING("same", buf);
}

static void test_shell_history_empty(void)
{
    struct shell_history hist;
    shell_history_init(&hist);

    char buf[CONFIG_ZSHELL_CMD_BUFF_SIZE];
    uint16_t len;

    TEST_ASSERT_FALSE(shell_history_get(&hist, true, buf, &len));
    TEST_ASSERT_FALSE(shell_history_get(&hist, false, buf, &len));
}

#endif /* CONFIG_ZSHELL_HISTORY */

/* ----------------------------------------------------------------
 * Test group runner
 * ---------------------------------------------------------------- */

void test_shell_group(void)
{
    /* Parsing */
    RUN_TEST(test_shell_make_argv_simple);
    RUN_TEST(test_shell_make_argv_extra_spaces);
    RUN_TEST(test_shell_make_argv_quoted);
    RUN_TEST(test_shell_make_argv_empty);
    RUN_TEST(test_shell_make_argv_max_argc);

    /* Registration */
    RUN_TEST(test_shell_cmd_register_and_sort);

#ifdef CONFIG_ZSHELL_HISTORY
    /* History */
    RUN_TEST(test_shell_history_add_and_navigate);
    RUN_TEST(test_shell_history_skip_duplicate);
    RUN_TEST(test_shell_history_empty);
#endif
}
