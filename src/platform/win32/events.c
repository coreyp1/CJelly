/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2025-2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io CJelly.
 *
 * Ghoti.io CJelly is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io CJelly is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * CJelly — Win32 event processing
 *
 * Lifted from cjelly.c, where it was the #ifdef _WIN32 arm of an 800-line
 * platform conditional. The X11 arm is src/platform/x11/events.c; the
 * Makefile compiles one directory under src/platform and not the other, so
 * neither file needs a platform conditional of its own.
 */

#include <ghoti.io/cjelly/platform_internal.h>

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/application.h>
#include <ghoti.io/cjelly/cj_window.h>
#include <ghoti.io/cjelly/window_internal.h>

CJ_API void processWindowEvents(void) {
  MSG msg;
  while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    // Handle WM_QUIT specially - this is posted when the last window closes
    // We should not dispatch it, but rather let the application handle it
    // Since we used PM_REMOVE, the message is already removed from the queue
    if (msg.message == WM_QUIT) {
      // WM_QUIT indicates the application should exit
      // Don't dispatch it, just break out of the loop
      // The application's main loop should check for this condition
      // Note: The message has already been removed from the queue by PeekMessage
      break;
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}
