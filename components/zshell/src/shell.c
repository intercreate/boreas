/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Core shell: initialization, thread loop, character processing.
 *
 * Zephyr reference: subsys/shell/shell.c
 */

#include "zshell/shell.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "shell";

/* ----------------------------------------------------------------
 * Character Processing
 * ---------------------------------------------------------------- */

static void shell_print_prompt(struct shell *sh)
{
    const char *prompt = sh->default_prompt ? sh->default_prompt
                                            : CONFIG_ZSHELL_PROMPT;
    shell_write(sh, prompt, strlen(prompt));
}

static void shell_echo_char(struct shell *sh, char ch)
{
    if (sh->ctx.echo) {
        shell_write(sh, &ch, 1);
    }
}

static void shell_clear_line(struct shell *sh)
{
    /* Move to start, clear line */
    const char *clear = "\r\033[K";
    shell_write(sh, clear, strlen(clear));
    sh->ctx.cmd_buff_len = 0;
    sh->ctx.cmd_buff_pos = 0;
}

static void shell_process_char(struct shell *sh, char ch)
{
    /* VT100 escape sequence handling */
#ifdef CONFIG_ZSHELL_VT100
    if (sh->ctx.esc_state != 0) {
        shell_vt100_process(sh, ch);
        return;
    }
    if (ch == '\033') {
        sh->ctx.esc_state = 1;
        sh->ctx.esc_len = 0;
        return;
    }
#endif

    switch (ch) {
    case '\r':
    case '\n':
        /* Execute command */
        shell_write(sh, "\r\n", 2);
        if (sh->ctx.cmd_buff_len > 0) {
            sh->ctx.cmd_buff[sh->ctx.cmd_buff_len] = '\0';

#ifdef CONFIG_ZSHELL_HISTORY
            shell_history_add(&sh->history, sh->ctx.cmd_buff,
                              sh->ctx.cmd_buff_len);
            shell_history_reset(&sh->history);
#endif
            shell_execute(sh);
        }
        sh->ctx.cmd_buff_len = 0;
        sh->ctx.cmd_buff_pos = 0;
        shell_print_prompt(sh);
        break;

    case '\b':
    case 0x7F: /* DEL / Backspace */
        if (sh->ctx.cmd_buff_pos > 0) {
            /* Remove char before cursor */
            if (sh->ctx.cmd_buff_pos < sh->ctx.cmd_buff_len) {
                memmove(&sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos - 1],
                        &sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos],
                        sh->ctx.cmd_buff_len - sh->ctx.cmd_buff_pos);
            }
            sh->ctx.cmd_buff_pos--;
            sh->ctx.cmd_buff_len--;

            /* Erase on terminal: backspace, rewrite rest, clear trailing */
            shell_write(sh, "\b", 1);
            if (sh->ctx.cmd_buff_pos < sh->ctx.cmd_buff_len) {
                shell_write(sh, &sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos],
                            sh->ctx.cmd_buff_len - sh->ctx.cmd_buff_pos);
                shell_write(sh, " \b", 2);
                /* Move cursor back */
                uint16_t move = sh->ctx.cmd_buff_len - sh->ctx.cmd_buff_pos;
                for (uint16_t i = 0; i < move; i++) {
                    shell_write(sh, "\b", 1);
                }
            } else {
                shell_write(sh, " \b", 2);
            }
        }
        break;

    case 0x03: /* Ctrl-C */
        shell_write(sh, "^C\r\n", 4);
        sh->ctx.cmd_buff_len = 0;
        sh->ctx.cmd_buff_pos = 0;
        shell_print_prompt(sh);
        break;

    case 0x01: /* Ctrl-A: move cursor to start */
        while (sh->ctx.cmd_buff_pos > 0) {
            sh->ctx.cmd_buff_pos--;
            shell_write(sh, "\b", 1);
        }
        break;

    case 0x05: /* Ctrl-E: move cursor to end */
        if (sh->ctx.cmd_buff_pos < sh->ctx.cmd_buff_len) {
            shell_write(sh, &sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos],
                        sh->ctx.cmd_buff_len - sh->ctx.cmd_buff_pos);
            sh->ctx.cmd_buff_pos = sh->ctx.cmd_buff_len;
        }
        break;

    case 0x0B: /* Ctrl-K: kill from cursor to end of line */
        if (sh->ctx.cmd_buff_pos < sh->ctx.cmd_buff_len) {
            /* Clear chars on terminal from cursor to end */
            uint16_t erased = sh->ctx.cmd_buff_len - sh->ctx.cmd_buff_pos;
            for (uint16_t i = 0; i < erased; i++) {
                shell_write(sh, " ", 1);
            }
            for (uint16_t i = 0; i < erased; i++) {
                shell_write(sh, "\b", 1);
            }
            sh->ctx.cmd_buff_len = sh->ctx.cmd_buff_pos;
        }
        break;

    case 0x17: /* Ctrl-W: delete word backward */
        if (sh->ctx.cmd_buff_pos > 0) {
            uint16_t start = sh->ctx.cmd_buff_pos;
            /* Skip trailing spaces */
            while (sh->ctx.cmd_buff_pos > 0 &&
                   sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos - 1] == ' ') {
                sh->ctx.cmd_buff_pos--;
            }
            /* Skip word chars */
            while (sh->ctx.cmd_buff_pos > 0 &&
                   sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos - 1] != ' ') {
                sh->ctx.cmd_buff_pos--;
            }
            /* Remove the chars */
            uint16_t removed = start - sh->ctx.cmd_buff_pos;
            memmove(&sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos],
                    &sh->ctx.cmd_buff[start],
                    sh->ctx.cmd_buff_len - start);
            sh->ctx.cmd_buff_len -= removed;
            /* Redraw: move cursor back, rewrite rest, clear trailing */
            for (uint16_t i = 0; i < removed; i++) {
                shell_write(sh, "\b", 1);
            }
            shell_write(sh, &sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos],
                        sh->ctx.cmd_buff_len - sh->ctx.cmd_buff_pos);
            for (uint16_t i = 0; i < removed; i++) {
                shell_write(sh, " ", 1);
            }
            uint16_t move_back = sh->ctx.cmd_buff_len - sh->ctx.cmd_buff_pos + removed;
            for (uint16_t i = 0; i < move_back; i++) {
                shell_write(sh, "\b", 1);
            }
        }
        break;

    case 0x0C: /* Ctrl-L: clear screen */
        shell_write(sh, "\033[2J\033[H", 7);
        shell_print_prompt(sh);
        /* Reprint current line */
        if (sh->ctx.cmd_buff_len > 0) {
            shell_write(sh, sh->ctx.cmd_buff, sh->ctx.cmd_buff_len);
        }
        break;

    case '\t': /* Tab */
#ifdef CONFIG_ZSHELL_TAB_COMPLETION
        shell_completion(sh);
#endif
        break;

    default:
        /* Printable character */
        if (ch >= 0x20 && ch < 0x7F &&
            sh->ctx.cmd_buff_len < CONFIG_ZSHELL_CMD_BUFF_SIZE - 1) {

            if (sh->ctx.cmd_buff_pos < sh->ctx.cmd_buff_len) {
                /* Insert mode: shift chars right */
                memmove(&sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos + 1],
                        &sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos],
                        sh->ctx.cmd_buff_len - sh->ctx.cmd_buff_pos);
            }

            sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos] = ch;
            sh->ctx.cmd_buff_pos++;
            sh->ctx.cmd_buff_len++;

            if (sh->ctx.echo) {
                /* Write from cursor to end, then move cursor back */
                shell_write(sh, &sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos - 1],
                            sh->ctx.cmd_buff_len - sh->ctx.cmd_buff_pos + 1);
                uint16_t move = sh->ctx.cmd_buff_len - sh->ctx.cmd_buff_pos;
                for (uint16_t i = 0; i < move; i++) {
                    shell_write(sh, "\b", 1);
                }
            }
        }
        break;
    }
}

/* ----------------------------------------------------------------
 * Shell Thread
 * ---------------------------------------------------------------- */

static void shell_thread_entry(void *p1, void *p2, void *p3)
{
    struct shell *sh = (struct shell *)p1;
    (void)p2;
    (void)p3;

    /* Initialize transport */
    if (sh->iface && sh->iface->api && sh->iface->api->init) {
        sh->iface->api->init((const struct shell_transport *)sh->iface, sh);
    }

    sh->ctx.state = SHELL_STATE_ACTIVE;
    ESP_LOGI(TAG, "Shell '%s' started", sh->name ? sh->name : "default");

    /* Print initial prompt */
    shell_write(sh, "\r\n", 2);
    shell_print_prompt(sh);

    /* Main loop: read chars from transport */
    for (;;) {
        char ch;
        size_t cnt = 0;

        if (sh->iface && sh->iface->api && sh->iface->api->read) {
            int ret = sh->iface->api->read(sh->iface, &ch, 1, &cnt);
            if (ret == 0 && cnt > 0) {
                shell_process_char(sh, ch);
            }
        } else {
            k_msleep(100);
        }
    }
}

/* ----------------------------------------------------------------
 * Shell Init
 * ---------------------------------------------------------------- */

int shell_init(struct shell *sh, const struct shell_transport *transport,
               const char *prompt)
{
    memset(&sh->ctx, 0, sizeof(sh->ctx));

    sh->iface = transport;
    sh->default_prompt = prompt ? prompt : CONFIG_ZSHELL_PROMPT;

    /* Init output mutex */
    k_mutex_init(&sh->ctx.wr_mtx);

#ifdef CONFIG_ZSHELL_ECHO
    sh->ctx.echo = true;
#else
    sh->ctx.echo = false;
#endif

    sh->ctx.state = SHELL_STATE_INITIALIZED;

    /* Register built-in commands, then sort all root commands */
    shell_builtins_register();
    shell_cmd_sort_root();

#ifdef CONFIG_ZSHELL_HISTORY
    shell_history_init(&sh->history);
#endif

    /* Create shell thread */
    K_THREAD_STACK_DEFINE(shell_stack, CONFIG_ZSHELL_THREAD_STACK_SIZE);

    k_thread_create(&sh->thread, shell_stack, CONFIG_ZSHELL_THREAD_STACK_SIZE,
                    shell_thread_entry, sh, NULL, NULL,
                    CONFIG_ZSHELL_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&sh->thread, "shell");

    ESP_LOGI(TAG, "Shell initialized (%zu root commands)", _shell_root_cmd_count);
    return 0;
}
