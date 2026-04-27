/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * VT100 escape sequence decoder and cursor operations.
 * Phase 6b: will add arrow keys, Home/End, Delete, Ctrl-A/E/K.
 *
 * Zephyr reference: subsys/shell/shell_ops.c
 */

#include "zshell/shell.h"

#include <string.h>

void shell_vt100_process(struct shell *sh, char ch)
{
	switch (sh->ctx.esc_state) {
	case 1: /* Got ESC */
		if (ch == '[') {
			sh->ctx.esc_state = 2; /* CSI sequence */
			sh->ctx.esc_len = 0;
		} else {
			sh->ctx.esc_state = 0; /* Unknown, discard */
		}
		break;

	case 2: /* Got ESC[ (CSI) */
		if (ch >= 0x40 && ch <= 0x7E) {
			/* Final byte -- decode the sequence */
			switch (ch) {
			case 'A': /* Up arrow */
#ifdef CONFIG_ZSHELL_HISTORY
			{
				uint16_t len = 0;
				char tmp[CONFIG_ZSHELL_CMD_BUFF_SIZE];
				if (shell_history_get(&sh->history, true, tmp, &len)) {
					/* Clear current line on terminal */
					shell_write(sh, "\r\033[K", 4);
					/* Reprint prompt */
					const char *prompt = sh->default_prompt
								     ? sh->default_prompt
								     : CONFIG_ZSHELL_PROMPT;
					shell_write(sh, prompt, strlen(prompt));
					/* Load history entry */
					memcpy(sh->ctx.cmd_buff, tmp, len);
					sh->ctx.cmd_buff_len = len;
					sh->ctx.cmd_buff_pos = len;
					shell_write(sh, sh->ctx.cmd_buff, len);
				}
			}
#endif
			break;

			case 'B': /* Down arrow */
#ifdef CONFIG_ZSHELL_HISTORY
			{
				uint16_t len = 0;
				char tmp[CONFIG_ZSHELL_CMD_BUFF_SIZE];
				if (shell_history_get(&sh->history, false, tmp, &len)) {
					shell_write(sh, "\r\033[K", 4);
					const char *prompt = sh->default_prompt
								     ? sh->default_prompt
								     : CONFIG_ZSHELL_PROMPT;
					shell_write(sh, prompt, strlen(prompt));
					memcpy(sh->ctx.cmd_buff, tmp, len);
					sh->ctx.cmd_buff_len = len;
					sh->ctx.cmd_buff_pos = len;
					shell_write(sh, sh->ctx.cmd_buff, len);
				} else {
					/* Past newest -- clear line */
					shell_write(sh, "\r\033[K", 4);
					const char *prompt = sh->default_prompt
								     ? sh->default_prompt
								     : CONFIG_ZSHELL_PROMPT;
					shell_write(sh, prompt, strlen(prompt));
					sh->ctx.cmd_buff_len = 0;
					sh->ctx.cmd_buff_pos = 0;
				}
			}
#endif
			break;

			case 'C': /* Right arrow */
				if (sh->ctx.cmd_buff_pos < sh->ctx.cmd_buff_len) {
					sh->ctx.cmd_buff_pos++;
					shell_write(sh, "\033[C", 3);
				}
				break;

			case 'D': /* Left arrow */
				if (sh->ctx.cmd_buff_pos > 0) {
					sh->ctx.cmd_buff_pos--;
					shell_write(sh, "\033[D", 3);
				}
				break;

			case 'H': /* Home */
				while (sh->ctx.cmd_buff_pos > 0) {
					sh->ctx.cmd_buff_pos--;
					shell_write(sh, "\033[D", 3);
				}
				break;

			case 'F': /* End */
				while (sh->ctx.cmd_buff_pos < sh->ctx.cmd_buff_len) {
					sh->ctx.cmd_buff_pos++;
					shell_write(sh, "\033[C", 3);
				}
				break;

			case '~': /* Extended keys: ESC[3~ = Delete */
				if (sh->ctx.esc_len > 0 && sh->ctx.esc_buff[0] == '3') {
					/* Delete char at cursor */
					if (sh->ctx.cmd_buff_pos < sh->ctx.cmd_buff_len) {
						memmove(&sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos],
							&sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos + 1],
							sh->ctx.cmd_buff_len -
								sh->ctx.cmd_buff_pos - 1);
						sh->ctx.cmd_buff_len--;
						/* Rewrite from cursor, clear trailing char */
						shell_write(sh,
							    &sh->ctx.cmd_buff[sh->ctx.cmd_buff_pos],
							    sh->ctx.cmd_buff_len -
								    sh->ctx.cmd_buff_pos);
						shell_write(sh, " ", 1);
						uint16_t move = sh->ctx.cmd_buff_len -
								sh->ctx.cmd_buff_pos + 1;
						for (uint16_t i = 0; i < move; i++) {
							shell_write(sh, "\b", 1);
						}
					}
				}
				break;

			default:
				break;
			}
			sh->ctx.esc_state = 0;
		} else {
			/* Parameter byte -- accumulate */
			if (sh->ctx.esc_len < sizeof(sh->ctx.esc_buff) - 1) {
				sh->ctx.esc_buff[sh->ctx.esc_len++] = ch;
			}
		}
		break;

	default:
		sh->ctx.esc_state = 0;
		break;
	}
}
