/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Intercreate
 *
 * Thread analyzer -- runtime stack and CPU usage reporting.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Print thread statistics to the log.
 * Shows: task name, stack size, high-water mark, free stack, state.
 */
void zsys_thread_analyzer_print(void);

#ifdef __cplusplus
}
#endif
