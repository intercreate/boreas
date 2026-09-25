/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Unit tests for shell parsing, command registration, history,
 * line editing and dispatch (via a capture transport, no shell thread).
 */

#include <errno.h>
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
	(void)sh;
	(void)argc;
	(void)argv;
	return 42;
}

static void test_shell_cmd_register_and_sort(void)
{
	/* Save and reset state */
	size_t saved_count = _shell_root_cmd_count;
	struct shell_static_entry *saved[CONFIG_ZSHELL_MAX_ROOT_CMDS];
	memcpy(saved, _shell_root_cmds, saved_count * sizeof(struct shell_static_entry *));

	_shell_root_cmd_count = 0;

	/* Register in reverse alphabetical order */
	static struct shell_static_entry cmd_z = {
		.syntax = "zebra",
		.help = "z cmd",
		.handler = dummy_handler,
	};
	static struct shell_static_entry cmd_a = {
		.syntax = "apple",
		.help = "a cmd",
		.handler = dummy_handler,
	};
	static struct shell_static_entry cmd_m = {
		.syntax = "mango",
		.help = "m cmd",
		.handler = dummy_handler,
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
	memcpy(_shell_root_cmds, saved, saved_count * sizeof(struct shell_static_entry *));
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
 * Line-editing harness: capture transport, no shell thread
 * ---------------------------------------------------------------- */

static char cap_buf[1024];
static size_t cap_len;

static int cap_write(const struct shell_transport *transport, const void *data, size_t length,
		     size_t *cnt)
{
	(void)transport;
	size_t room = sizeof(cap_buf) - 1 - cap_len;
	size_t n = length < room ? length : room;

	memcpy(&cap_buf[cap_len], data, n);
	cap_len += n;
	cap_buf[cap_len] = '\0';
	*cnt = length;
	return 0;
}

static const struct shell_transport_api cap_api = {.write = cap_write};
static const struct shell_transport cap_transport = {.api = &cap_api};

static struct shell tsh;

#ifdef CONFIG_ZSHELL_PROMPT_CHANGE
/* The harness prompt "t> " and the "login: " test both have to fit */
BUILD_ASSERT(CONFIG_ZSHELL_PROMPT_BUFF_SIZE >= sizeof("login: "),
	     "shell tests assume CONFIG_ZSHELL_PROMPT_BUFF_SIZE >= 8");
#endif

/* Last dispatch seen by tcmd_handler */
static size_t seen_argc;
static char seen_argv[CONFIG_ZSHELL_MAX_ARGC][32];

static int tcmd_handler(const struct shell *sh, size_t argc, char **argv)
{
	(void)sh;
	seen_argc = argc;
	for (size_t i = 0; i < argc && i < CONFIG_ZSHELL_MAX_ARGC; i++) {
		strncpy(seen_argv[i], argv[i], sizeof(seen_argv[i]) - 1);
		seen_argv[i][sizeof(seen_argv[i]) - 1] = '\0';
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(tcmd_subcmds, SHELL_CMD(get, NULL, "get", tcmd_handler)
						     SHELL_CMD(set, NULL, "set", tcmd_handler));

static struct shell_static_entry tcmd = {
	.syntax = "tcmd",
	.help = "test command",
	.subcmd = &tcmd_subcmds,
	.handler = tcmd_handler,
};

static int other_hits;

static int other_handler(const struct shell *sh, size_t argc, char **argv)
{
	(void)sh;
	(void)argc;
	(void)argv;
	other_hits++;
	return 0;
}

static struct shell_static_entry other = {
	.syntax = "other",
	.help = "other command",
	.handler = other_handler,
};

static size_t saved_root_count;
static struct shell_static_entry *saved_root[CONFIG_ZSHELL_MAX_ROOT_CMDS];

static void cap_clear(void)
{
	cap_len = 0;
	cap_buf[0] = '\0';
}

/* Fresh shell with a capture transport and a root tree of {other, tcmd} + builtins */
static void harness_setup(void)
{
	saved_root_count = _shell_root_cmd_count;
	memcpy(saved_root, _shell_root_cmds, saved_root_count * sizeof(saved_root[0]));
	_shell_root_cmd_count = 0;
	shell_cmd_register(&other);
	shell_cmd_register(&tcmd);
	shell_builtins_register(); /* help must be present for the gating tests */

	memset(&tsh, 0, sizeof(tsh));
	tsh.iface = &cap_transport;
	tsh.default_prompt = "t> ";
#ifdef CONFIG_ZSHELL_PROMPT_CHANGE
	strcpy(tsh.ctx.prompt, "t> ");
#endif
	k_mutex_init(&tsh.ctx.wr_mtx);
	tsh.ctx.echo = true;
	tsh.ctx.state = SHELL_STATE_ACTIVE;
#ifdef CONFIG_ZSHELL_HISTORY
	shell_history_init(&tsh.history);
#endif
	seen_argc = 0;
	other_hits = 0;
	cap_clear();
}

static void harness_teardown(void)
{
	shell_set_root_cmd(NULL);
	_shell_root_cmd_count = saved_root_count;
	memcpy(_shell_root_cmds, saved_root, saved_root_count * sizeof(saved_root[0]));
}

static void feed(const char *s)
{
	while (*s != '\0') {
		shell_process_char(&tsh, *s++);
	}
}

static void test_shell_insert_echo(void)
{
	harness_setup();
	feed("ab");
	TEST_ASSERT_EQUAL_STRING("ab", cap_buf);
	TEST_ASSERT_EQUAL(2, tsh.ctx.cmd_buff_len);
	TEST_ASSERT_EQUAL(2, tsh.ctx.cmd_buff_pos);
	TEST_ASSERT_EQUAL_MEMORY("ab", tsh.ctx.cmd_buff, 2);
	harness_teardown();
}

static void test_shell_backspace(void)
{
	harness_setup();
	feed("abc\b");
	TEST_ASSERT_EQUAL_STRING("abc\b \b", cap_buf);
	TEST_ASSERT_EQUAL(2, tsh.ctx.cmd_buff_len);
	TEST_ASSERT_EQUAL_MEMORY("ab", tsh.ctx.cmd_buff, 2);
	harness_teardown();
}

static void test_shell_dispatch_subcmd(void)
{
	harness_setup();
	feed("tcmd set 5\r");
	TEST_ASSERT_EQUAL(2, seen_argc);
	TEST_ASSERT_EQUAL_STRING("set", seen_argv[0]);
	TEST_ASSERT_EQUAL_STRING("5", seen_argv[1]);
	/* Prompt reprinted after execution */
	TEST_ASSERT_NOT_NULL(strstr(cap_buf, "\r\nt> "));
	harness_teardown();
}

static void test_shell_dispatch_unknown(void)
{
	harness_setup();
	feed("nope\r");
	TEST_ASSERT_EQUAL(0, seen_argc);
	TEST_ASSERT_NOT_NULL(strstr(cap_buf, "Unknown command: nope"));
	harness_teardown();
}

/* ----------------------------------------------------------------
 * Obscured input
 * ---------------------------------------------------------------- */

static void test_shell_obscure_set_returns_previous(void)
{
	harness_setup();
	TEST_ASSERT_EQUAL(0, shell_obscure_set(&tsh, true));
	TEST_ASSERT_EQUAL(1, shell_obscure_set(&tsh, true));
	TEST_ASSERT_EQUAL(1, shell_obscure_set(&tsh, false));
	TEST_ASSERT_EQUAL(0, shell_obscure_set(&tsh, false));
	TEST_ASSERT_EQUAL(-EINVAL, shell_obscure_set(NULL, true));
	harness_teardown();
}

static void test_shell_obscure_insert_delete(void)
{
	harness_setup();
	shell_obscure_set(&tsh, true);

	feed("ab");
	TEST_ASSERT_EQUAL_STRING("**", cap_buf);
	TEST_ASSERT_EQUAL_MEMORY("ab", tsh.ctx.cmd_buff, 2);

	cap_clear();
	feed("\b");
	TEST_ASSERT_EQUAL_STRING("\b \b", cap_buf);
	TEST_ASSERT_EQUAL(1, tsh.ctx.cmd_buff_len);

#ifdef CONFIG_ZSHELL_VT100
	/* Insert in the middle: the rewritten tail must stay obscured */
	cap_clear();
	feed("\033[D"
	     "z");
	TEST_ASSERT_EQUAL_MEMORY("za", tsh.ctx.cmd_buff, 2);
	TEST_ASSERT_NULL(strchr(cap_buf, 'a'));
	TEST_ASSERT_NULL(strchr(cap_buf, 'z'));
#endif

	/* Dispatch still sees the real text */
	feed("\r");
#ifdef CONFIG_ZSHELL_VT100
	TEST_ASSERT_NOT_NULL(strstr(cap_buf, "Unknown command: za"));
#else
	TEST_ASSERT_NOT_NULL(strstr(cap_buf, "Unknown command: a"));
#endif
	harness_teardown();
}

/* Not obscured unless CONFIG_ZSHELL_START_OBSCURED (off in the test config) */
static void test_shell_obscure_default_off(void)
{
	harness_setup();
	feed("a");
	TEST_ASSERT_EQUAL_STRING("a", cap_buf);
	harness_teardown();
}

/* ----------------------------------------------------------------
 * Root command selection
 * ---------------------------------------------------------------- */

static void test_shell_set_root_cmd_valid_invalid(void)
{
	harness_setup();
	TEST_ASSERT_EQUAL(-EINVAL, shell_set_root_cmd("bogus"));
	TEST_ASSERT_NULL(shell_root_cmd_get());
	TEST_ASSERT_EQUAL(0, shell_set_root_cmd("tcmd"));
	TEST_ASSERT_EQUAL_PTR(&tcmd, shell_root_cmd_get());
	/* A bad name leaves the current selection alone */
	TEST_ASSERT_EQUAL(-EINVAL, shell_set_root_cmd("bogus"));
	TEST_ASSERT_EQUAL_PTR(&tcmd, shell_root_cmd_get());
	TEST_ASSERT_EQUAL(0, shell_set_root_cmd(NULL));
	TEST_ASSERT_NULL(shell_root_cmd_get());
	harness_teardown();
}

static void test_shell_root_dispatch_beneath(void)
{
	harness_setup();
	shell_set_root_cmd("tcmd");

	/* Subcommand of the root */
	feed("set 7\r");
	TEST_ASSERT_EQUAL(2, seen_argc);
	TEST_ASSERT_EQUAL_STRING("set", seen_argv[0]);
	TEST_ASSERT_EQUAL_STRING("7", seen_argv[1]);

	/* Free text goes to the root's own handler as arguments */
	feed("secret\r");
	TEST_ASSERT_EQUAL(2, seen_argc);
	TEST_ASSERT_EQUAL_STRING("tcmd", seen_argv[0]);
	TEST_ASSERT_EQUAL_STRING("secret", seen_argv[1]);

	/* Blank line does nothing */
	seen_argc = 0;
	feed("   \r");
	TEST_ASSERT_EQUAL(0, seen_argc);
	harness_teardown();
}

static void test_shell_root_rejects_other_roots(void)
{
	harness_setup();
	shell_set_root_cmd("tcmd");

	feed("other\r");
	TEST_ASSERT_EQUAL(0, other_hits);
	TEST_ASSERT_EQUAL_STRING("other", seen_argv[1]);

	/* help is itself a root command, so it is unreachable too: it lands on
	 * the root handler as an argument instead of listing the tree */
	feed("help\r");
	TEST_ASSERT_EQUAL_STRING("help", seen_argv[1]);
	TEST_ASSERT_NULL(strstr(cap_buf, "Available commands"));
	TEST_ASSERT_NULL(strstr(cap_buf, "other command"));

	/* Positive control: with the tree restored, help does list it */
	shell_set_root_cmd(NULL);
	cap_clear();
	feed("help\r");
	TEST_ASSERT_NOT_NULL(strstr(cap_buf, "other command"));
	harness_teardown();
}

static void test_shell_root_clear_restores_tree(void)
{
	harness_setup();
	shell_set_root_cmd("tcmd");
	shell_set_root_cmd(NULL);

	feed("other\r");
	TEST_ASSERT_EQUAL(1, other_hits);
	harness_teardown();
}

#ifdef CONFIG_ZSHELL_TAB_COMPLETION
static void test_shell_root_completion(void)
{
	harness_setup();
	shell_set_root_cmd("tcmd");

	/* Completes beneath the root */
	feed("g\t");
	TEST_ASSERT_EQUAL(4, tsh.ctx.cmd_buff_len);
	TEST_ASSERT_EQUAL_MEMORY("get ", tsh.ctx.cmd_buff, 4);

	/* Does not see other root commands */
	feed("\x03");
	cap_clear();
	feed("o\t");
	TEST_ASSERT_EQUAL(1, tsh.ctx.cmd_buff_len);
	TEST_ASSERT_EQUAL_STRING("o\a", cap_buf);

	/* Empty line lists only the root's subcommands */
	feed("\x03");
	cap_clear();
	feed("\t");
	TEST_ASSERT_NOT_NULL(strstr(cap_buf, "get"));
	TEST_ASSERT_NOT_NULL(strstr(cap_buf, "set"));
	TEST_ASSERT_NULL(strstr(cap_buf, "other"));

	/* Completed text stays obscured */
	feed("\x03");
	shell_obscure_set(&tsh, true);
	cap_clear();
	feed("g\t");
	TEST_ASSERT_EQUAL_MEMORY("get ", tsh.ctx.cmd_buff, 4);
	TEST_ASSERT_EQUAL_STRING("*** ", cap_buf);
	harness_teardown();
}
#endif

/* ----------------------------------------------------------------
 * Prompt change
 * ---------------------------------------------------------------- */

#ifdef CONFIG_ZSHELL_PROMPT_CHANGE
static void test_shell_prompt_change(void)
{
	char too_long[CONFIG_ZSHELL_PROMPT_BUFF_SIZE + 1];

	memset(too_long, 'x', sizeof(too_long) - 1);
	too_long[sizeof(too_long) - 1] = '\0';

	harness_setup();
	TEST_ASSERT_EQUAL(-EINVAL, shell_prompt_change(&tsh, NULL));
	TEST_ASSERT_EQUAL(-EINVAL, shell_prompt_change(&tsh, too_long));
	/* Exactly BUFF_SIZE - 1 chars fits */
	too_long[CONFIG_ZSHELL_PROMPT_BUFF_SIZE - 1] = '\0';
	TEST_ASSERT_EQUAL(0, shell_prompt_change(&tsh, too_long));

	TEST_ASSERT_EQUAL(0, shell_prompt_change(&tsh, "login: "));
	feed("\r");
	TEST_ASSERT_EQUAL_STRING("\r\nlogin: ", cap_buf);
	harness_teardown();
}
#endif

/* ----------------------------------------------------------------
 * History purge
 * ---------------------------------------------------------------- */

#ifdef CONFIG_ZSHELL_HISTORY
static void test_shell_history_purge(void)
{
	char buf[CONFIG_ZSHELL_CMD_BUFF_SIZE];
	uint16_t len;

	harness_setup();
	feed("hunter2\r");
	TEST_ASSERT_TRUE(shell_history_get(&tsh.history, true, buf, &len));
	shell_history_reset(&tsh.history);

	shell_history_purge(&tsh);
	TEST_ASSERT_FALSE(shell_history_get(&tsh.history, true, buf, &len));

	/* Still usable afterwards */
	feed("later\r");
	TEST_ASSERT_TRUE(shell_history_get(&tsh.history, true, buf, &len));
	TEST_ASSERT_EQUAL_STRING("later", buf);
	harness_teardown();
}
#endif

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

	/* Line editing + dispatch */
	RUN_TEST(test_shell_insert_echo);
	RUN_TEST(test_shell_backspace);
	RUN_TEST(test_shell_dispatch_subcmd);
	RUN_TEST(test_shell_dispatch_unknown);

	/* Login primitives (#59) */
	RUN_TEST(test_shell_obscure_set_returns_previous);
	RUN_TEST(test_shell_obscure_insert_delete);
	RUN_TEST(test_shell_obscure_default_off);
	RUN_TEST(test_shell_set_root_cmd_valid_invalid);
	RUN_TEST(test_shell_root_dispatch_beneath);
	RUN_TEST(test_shell_root_rejects_other_roots);
	RUN_TEST(test_shell_root_clear_restores_tree);
#ifdef CONFIG_ZSHELL_TAB_COMPLETION
	RUN_TEST(test_shell_root_completion);
#endif
#ifdef CONFIG_ZSHELL_PROMPT_CHANGE
	RUN_TEST(test_shell_prompt_change);
#endif
#ifdef CONFIG_ZSHELL_HISTORY
	RUN_TEST(test_shell_history_purge);
#endif
}
