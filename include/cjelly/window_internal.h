/*
 * CJelly — Internal window API
 * Copyright (c) 2025
 *
 * Internal window functions used between modules.
 * Not part of the public API.
 */
#pragma once

#include "cj_window.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Internal helper to invoke close callback and destroy window if allowed.
 *  @param window The window to close.
 *  @param cancellable True if the close can be prevented, false if close is mandatory.
 */
void cj_window_close_with_callback(cj_window_t* window, bool cancellable);

/** Internal helper used by the framework event loop to run a window's per-frame callback. */
cj_frame_result_t cj_window__dispatch_frame_callback(cj_window_t* window,
                                                    const cj_frame_info_t* frame_info);

#ifdef __cplusplus
}
#endif
