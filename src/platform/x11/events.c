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
 * CJelly — X11 event processing
 *
 * Lifted from cjelly.c, where it was the #else arm of an 800-line platform
 * conditional. The Win32 arm is src/platform/win32/events.c; the Makefile
 * compiles one directory under src/platform and not the other, so neither
 * file needs a platform conditional of its own.
 *
 * The XInput2 state and the X cj_x11_display live here because nothing outside this
 * file reads them: all eighteen uses of `cj_x11_display` in cjelly.c were inside
 * this arm, and every xinput2_* use was too.
 */

#include <ghoti.io/cjelly/platform_internal.h>

#include <dlfcn.h> /* For dlsym to optionally load XInput2 functions */
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void*)0)
#endif

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/keysym.h>
#include <X11/extensions/XI.h>
#include <X11/extensions/XI2.h>
#include <X11/extensions/XI2proto.h>

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/application.h>
#include <ghoti.io/cjelly/cj_input.h>
#include <ghoti.io/cjelly/cj_window.h>
#include <ghoti.io/cjelly/window_internal.h>

/* The X cj_x11_display. It used to be a global spelled `cj_x11_display`, with external
 * linkage and no library prefix, which the demo ASSIGNED TO through an
 * `extern Display * cj_x11_display;` of its own - so a consumer linking the static
 * archive both collided with the name and drove the library's connection.
 * Renamed, declared once in platform_internal.h, and opened and closed
 * through the two functions below. */
Display * cj_x11_display;

bool cj_x11_open_display(void) {
  if (cj_x11_display) return true;
  cj_x11_display = XOpenDisplay(NULL);
  return cj_x11_display != NULL;
}

void cj_x11_close_display(void) {
  if (!cj_x11_display) return;
  XCloseDisplay(cj_x11_display);
  cj_x11_display = NULL;
}

static int xinput2_available = -1; /* -1 = not checked, 0 = unavailable, 1 = available */
static int xinput2_major = 2;
static int xinput2_minor = 0;
static int xinput2_opcode = -1; /* XInput extension opcode */
static void* xinput2_lib_handle = NULL; /* Handle to libXi.so for dlopen */


/* Initialize XInput2 if available. Returns true if XInput2 is available and initialized.
 * Note: XInput2 is kept available for future touch support, but scroll events
 * are handled via traditional X11 ButtonPress events (Button4/Button5).
 */
static bool init_xinput2(void) {
  if (xinput2_available != -1) {
    return xinput2_available == 1;
  }

  xinput2_available = 0; /* Assume unavailable until proven otherwise */

  if (!cj_x11_display) return false;

  int event, error;
  if (!XQueryExtension(cj_x11_display, "XInputExtension", &xinput2_opcode, &event, &error)) {
    /* XInput extension not available */
    return false;
  }

  /* Query XInput2 version - try to load libXi dynamically */
  typedef int (*XIQueryVersionFunc)(Display*, int*, int*);
  XIQueryVersionFunc xi_query_version = NULL;

  /* Try to open libXi if not already opened */
  if (!xinput2_lib_handle) {
    /* Try common library names */
    const char* lib_names[] = {
      "libXi.so.6",
      "libXi.so",
      "libXi.so.6.1.0",
      NULL
    };

    for (int i = 0; lib_names[i] != NULL; i++) {
      xinput2_lib_handle = dlopen(lib_names[i], RTLD_LAZY | RTLD_LOCAL);
      if (xinput2_lib_handle) {
        break;
      }
    }
  }

  if (xinput2_lib_handle) {
    void* sym = dlsym(xinput2_lib_handle, "XIQueryVersion");
    if (sym) {
      /* Use union to avoid pedantic warning about function pointer conversion */
      union { void* p; XIQueryVersionFunc f; } u = {.p = sym};
      xi_query_version = u.f;
    }
  }

  if (!xi_query_version) {
    /* libXi not available - skip XInput2 */
    return false;
  }

  int major = 2, minor = 0;
  Status result = xi_query_version(cj_x11_display, &major, &minor);
  if (result == BadRequest || result != Success) {
    /* XInput2 not available */
    return false;
  }

  xinput2_major = major;
  xinput2_minor = minor;

  xinput2_available = 1;
  return true;
}

/* Select XInput2 events for a window. Returns true if XInput2 events were selected. */
bool cj_x11_select_xinput2_events(Window window) {
  if (!init_xinput2() || !cj_x11_display) {
    return false;
  }

  /* Define XIEventMask structure locally since header may not expose it properly */
  typedef struct {
    int deviceid;
    int mask_len;
    unsigned char* mask;
  } LocalXIEventMask;

  /* Load XInput2 function dynamically */
  typedef Status (*XISelectEventsFunc)(Display*, Window, void*, int);
  XISelectEventsFunc xi_select_events = NULL;

  if (xinput2_lib_handle) {
    void* sym = dlsym(xinput2_lib_handle, "XISelectEvents");
    if (sym) {
      union { void* p; XISelectEventsFunc f; } u = {.p = sym};
      xi_select_events = u.f;
    }
  }

  if (!xi_select_events) {
    /* Function not available */
    return false;
  }

  LocalXIEventMask event_mask;
  unsigned char mask[32] = {0}; /* Allocate enough space for XI_LASTEVENT */

  event_mask.deviceid = XIAllMasterDevices;
  event_mask.mask_len = sizeof(mask);
  event_mask.mask = mask;

  /* Select touch events for future touch support (XI_TouchBegin, XI_TouchUpdate, XI_TouchEnd) */
  /* Note: Scroll events are handled via traditional X11 ButtonPress (Button4/Button5) */
  /* We don't select XI_ButtonPress/XI_ButtonRelease here to avoid consuming scroll events */

  Status result = xi_select_events(cj_x11_display, window, &event_mask, 1);
  if (result != Success) {
    return false;
  }

  XFlush(cj_x11_display);
  return true;
}

CJ_API void processWindowEvents(void) {
  /* Initialize XInput2 on first call */
  if (xinput2_available == -1) {
    init_xinput2();
  }

  while (XPending(cj_x11_display)) {
    XEvent event;
    XNextEvent(cj_x11_display, &event);

    /* Check for XInput2 events (reserved for future touch support) */
    if (xinput2_available == 1 && event.type == GenericEvent) {
      XGenericEventCookie* cookie = &event.xcookie;
      if (XGetEventData(cj_x11_display, cookie)) {
        if (cookie->extension == xinput2_opcode) {
          /* TODO: Handle XI_TouchBegin, XI_TouchUpdate, XI_TouchEnd for touch support */
          /* For now, just free the event data and let traditional X11 handle everything */
          XFreeEventData(cj_x11_display, cookie);
        } else {
          XFreeEventData(cj_x11_display, cookie);
        }
      }
    }

    if (event.type == ClientMessage) {
      Atom wmDelete = XInternAtom(cj_x11_display, "WM_DELETE_WINDOW", False);
      if ((Atom)event.xclient.data.l[0] == wmDelete) {
        // Look up window from handle via application
        CJellyApplication* app = cjelly_application_get_current();
        if (app) {
          cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xclient.window);
          if (window) {
            bool cancellable = true;  // User-initiated close
            cj_window_close_with_callback(window, cancellable);
          }
        }
      }
    }
    if (event.type == DestroyNotify) {
      // Window already destroyed, nothing to do
    }
    if (event.type == MapNotify) {
      // Window mapped (restored from minimized)
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xmap.window);
        if (window) {
          cj_window__set_minimized(window, false);
          /* Mark window dirty when restored from minimized (EXPOSE reason bypasses FPS limit) */
          cj_window_mark_dirty_with_reason(window, CJ_RENDER_REASON_EXPOSE);
        }
      }
    }
    if (event.type == UnmapNotify) {
      // Window unmapped (minimized)
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xunmap.window);
        if (window) {
          cj_window__set_minimized(window, true);
        }
      }
    }
    if (event.type == ConfigureNotify) {
      // Window resized or moved
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xconfigure.window);
        if (window) {
          // Get CLIENT position for consistency with get_position/set_position.
          Window child;
          int client_x, client_y;
          if (XTranslateCoordinates(cj_x11_display, event.xconfigure.window, RootWindow(cj_x11_display, DefaultScreen(cj_x11_display)),
                                     0, 0, &client_x, &client_y, &child)) {
            // Skip position updates if we're programmatically moving (avoid feedback loops)
            if (cj_window__is_programmatic_move(window)) {
              // Just update cache silently, don't dispatch callback
              cj_window__set_position(window, client_x, client_y);
              // Clear the flag after we've processed this ConfigureNotify
              cj_window__set_programmatic_move(window, false);
            } else {
              int32_t current_x, current_y;
              cj_window__get_position(window, &current_x, &current_y);
              // Only update if position changed significantly (avoid feedback loops during drag)
              int32_t dx = (client_x > current_x) ? (client_x - current_x) : (current_x - client_x);
              int32_t dy = (client_y > current_y) ? (client_y - current_y) : (current_y - client_y);
              if (dx > 1 || dy > 1) {
                cj_window__set_position(window, client_x, client_y);
                cj_window__dispatch_move_callback(window, client_x, client_y);
              } else if (client_x != current_x || client_y != current_y) {
                // Small change (< 2 pixels) - just update cache without dispatching callback
                cj_window__set_position(window, client_x, client_y);
              }

              // Check if DPI changed (window moved to different monitor)
              float old_scale = cj_window__get_dpi_scale(window);
              Window root = RootWindow(cj_x11_display, DefaultScreen(cj_x11_display));
              float new_scale = cj_window__get_dpi_scale_linux(cj_x11_display, root, client_x, client_y);
              if (fabsf(new_scale - old_scale) > 0.01f) {  // DPI changed (with small threshold for floating point)
                cj_window__set_dpi_scale(window, new_scale);
                // Mark swapchain for recreation (physical size may have changed)
                cj_window__mark_swapchain_for_recreation(window);
              }
            }
          }

          uint32_t new_width = (uint32_t)event.xconfigure.width;
          uint32_t new_height = (uint32_t)event.xconfigure.height;

          // Get current size to check if it changed
          uint32_t current_width = 0, current_height = 0;
          cj_window_get_size(window, &current_width, &current_height);

          // Only dispatch if size actually changed
          if (new_width != current_width || new_height != current_height) {
            // Update size and mark swapchain for recreation (deferred until next frame to avoid blocking)
            cj_window__update_size_and_mark_recreate(window, new_width, new_height);

            // Dispatch resize callback (user can do additional work)
            cj_window__dispatch_resize_callback(window, new_width, new_height);
          }
        }
      }
    }
    if (event.type == PropertyNotify) {
      // Window property changed (e.g., _NET_WM_STATE)
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xproperty.window);
        if (window) {
          Atom wm_state = XInternAtom(cj_x11_display, "_NET_WM_STATE", False);
          if (event.xproperty.atom == wm_state) {
            // _NET_WM_STATE changed, query new state
            cj_window_state_t new_state = cj_window_get_state(window);
            cj_window_state_t current_state = cj_window__get_state(window);
            if (new_state != current_state) {
              cj_window__set_state(window, new_state);
              cj_window__dispatch_state_callback(window, new_state);
            }
          }
        }
      }
    }
    if (event.type == KeyPress) {
      // Handle keyboard input
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xkey.window);
        if (window) {
      KeySym sym = XLookupKeysym(&event.xkey, 0);

          // Map X11 keysym to cj_keycode_t
          cj_keycode_t keycode = CJ_KEY_UNKNOWN;
          if (sym >= XK_a && sym <= XK_z) keycode = (cj_keycode_t)(CJ_KEY_A + (sym - XK_a));
          else if (sym >= XK_A && sym <= XK_Z) keycode = (cj_keycode_t)(CJ_KEY_A + (sym - XK_A));
          else if (sym >= XK_0 && sym <= XK_9) keycode = (cj_keycode_t)(CJ_KEY_0 + (sym - XK_0));
          else {
            switch (sym) {
              case XK_F1: keycode = CJ_KEY_F1; break;
              case XK_F2: keycode = CJ_KEY_F2; break;
              case XK_F3: keycode = CJ_KEY_F3; break;
              case XK_F4: keycode = CJ_KEY_F4; break;
              case XK_F5: keycode = CJ_KEY_F5; break;
              case XK_F6: keycode = CJ_KEY_F6; break;
              case XK_F7: keycode = CJ_KEY_F7; break;
              case XK_F8: keycode = CJ_KEY_F8; break;
              case XK_F9: keycode = CJ_KEY_F9; break;
              case XK_F10: keycode = CJ_KEY_F10; break;
              case XK_F11: keycode = CJ_KEY_F11; break;
              case XK_F12: keycode = CJ_KEY_F12; break;
              case XK_Up: keycode = CJ_KEY_UP; break;
              case XK_Down: keycode = CJ_KEY_DOWN; break;
              case XK_Left: keycode = CJ_KEY_LEFT; break;
              case XK_Right: keycode = CJ_KEY_RIGHT; break;
              case XK_Home: keycode = CJ_KEY_HOME; break;
              case XK_End: keycode = CJ_KEY_END; break;
              case XK_Page_Up: keycode = CJ_KEY_PAGE_UP; break;
              case XK_Page_Down: keycode = CJ_KEY_PAGE_DOWN; break;
              case XK_BackSpace: keycode = CJ_KEY_BACKSPACE; break;
              case XK_Delete: keycode = CJ_KEY_DELETE; break;
              case XK_Insert: keycode = CJ_KEY_INSERT; break;
              case XK_Return: keycode = CJ_KEY_ENTER; break;
              case XK_Tab: keycode = CJ_KEY_TAB; break;
              case XK_Escape: keycode = CJ_KEY_ESCAPE; break;
              case XK_Shift_L: keycode = CJ_KEY_LEFT_SHIFT; break;
              case XK_Shift_R: keycode = CJ_KEY_RIGHT_SHIFT; break;
              case XK_Control_L: keycode = CJ_KEY_LEFT_CTRL; break;
              case XK_Control_R: keycode = CJ_KEY_RIGHT_CTRL; break;
              case XK_Alt_L: keycode = CJ_KEY_LEFT_ALT; break;
              case XK_Alt_R: keycode = CJ_KEY_RIGHT_ALT; break;
              case XK_Super_L: keycode = CJ_KEY_LEFT_META; break;
              case XK_Super_R: keycode = CJ_KEY_RIGHT_META; break;
              case XK_space: keycode = CJ_KEY_SPACE; break;
              case XK_minus: keycode = CJ_KEY_MINUS; break;
              case XK_equal: keycode = CJ_KEY_EQUALS; break;
              case XK_bracketleft: keycode = CJ_KEY_BRACKET_LEFT; break;
              case XK_bracketright: keycode = CJ_KEY_BRACKET_RIGHT; break;
              case XK_backslash: keycode = CJ_KEY_BACKSLASH; break;
              case XK_semicolon: keycode = CJ_KEY_SEMICOLON; break;
              case XK_apostrophe: keycode = CJ_KEY_APOSTROPHE; break;
              case XK_grave: keycode = CJ_KEY_GRAVE; break;
              case XK_comma: keycode = CJ_KEY_COMMA; break;
              case XK_period: keycode = CJ_KEY_PERIOD; break;
              case XK_slash: keycode = CJ_KEY_SLASH; break;
              case XK_KP_0: keycode = CJ_KEY_NUMPAD_0; break;
              case XK_KP_1: keycode = CJ_KEY_NUMPAD_1; break;
              case XK_KP_2: keycode = CJ_KEY_NUMPAD_2; break;
              case XK_KP_3: keycode = CJ_KEY_NUMPAD_3; break;
              case XK_KP_4: keycode = CJ_KEY_NUMPAD_4; break;
              case XK_KP_5: keycode = CJ_KEY_NUMPAD_5; break;
              case XK_KP_6: keycode = CJ_KEY_NUMPAD_6; break;
              case XK_KP_7: keycode = CJ_KEY_NUMPAD_7; break;
              case XK_KP_8: keycode = CJ_KEY_NUMPAD_8; break;
              case XK_KP_9: keycode = CJ_KEY_NUMPAD_9; break;
              case XK_KP_Add: keycode = CJ_KEY_NUMPAD_ADD; break;
              case XK_KP_Subtract: keycode = CJ_KEY_NUMPAD_SUBTRACT; break;
              case XK_KP_Multiply: keycode = CJ_KEY_NUMPAD_MULTIPLY; break;
              case XK_KP_Divide: keycode = CJ_KEY_NUMPAD_DIVIDE; break;
              case XK_KP_Decimal: keycode = CJ_KEY_NUMPAD_DECIMAL; break;
              case XK_KP_Enter: keycode = CJ_KEY_NUMPAD_ENTER; break;
              case XK_Caps_Lock: keycode = CJ_KEY_CAPS_LOCK; break;
              case XK_Num_Lock: keycode = CJ_KEY_NUM_LOCK; break;
              case XK_Scroll_Lock: keycode = CJ_KEY_SCROLL_LOCK; break;
              case XK_Print: keycode = CJ_KEY_PRINT_SCREEN; break;
              case XK_Pause: keycode = CJ_KEY_PAUSE; break;
              default: keycode = CJ_KEY_UNKNOWN; break;
            }
          }

          // Get modifiers
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xkey.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xkey.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xkey.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xkey.state & Mod4Mask) modifiers |= CJ_MOD_META;
          // Lock keys: LockMask = Caps Lock, Mod2Mask = Num Lock (typical, may vary)
          if (event.xkey.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xkey.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          // Check for auto-repeat: if key is already marked as pressed, this is a repeat
          bool is_repeat = cj_window__is_key_pressed(window, keycode);
          // Mark key as pressed
          cj_window__set_key_pressed(window, keycode, true);

          // Dispatch keyboard callback
          cj_window__dispatch_key_callback(window, keycode, (cj_scancode_t)event.xkey.keycode,
                                           CJ_KEY_ACTION_DOWN, modifiers, is_repeat);
        }
      }
    }
    if (event.type == KeyRelease) {
      // X11 auto-repeat detection: When a key is held, X11 generates KeyRelease immediately
      // followed by KeyPress. We detect this by peeking at the next event. If it's a KeyPress
      // for the same key with the same or very close timestamp, skip this KeyRelease (it's fake).
      if (XPending(cj_x11_display) > 0) {
        XEvent next_event;
        XPeekEvent(cj_x11_display, &next_event);
        if (next_event.type == KeyPress &&
            next_event.xkey.keycode == event.xkey.keycode &&
            next_event.xkey.time == event.xkey.time) {
          // This is a fake KeyRelease from auto-repeat - skip it
          // The next KeyPress will be handled and marked as repeat
          continue;
        }
      }
      // Handle key release
        CJellyApplication* app = cjelly_application_get_current();
        if (app) {
          cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xkey.window);
          if (window) {
          KeySym sym = XLookupKeysym(&event.xkey, 0);

          // Map X11 keysym to cj_keycode_t (same mapping as KeyPress)
          cj_keycode_t keycode = CJ_KEY_UNKNOWN;
          if (sym >= XK_a && sym <= XK_z) keycode = (cj_keycode_t)(CJ_KEY_A + (sym - XK_a));
          else if (sym >= XK_A && sym <= XK_Z) keycode = (cj_keycode_t)(CJ_KEY_A + (sym - XK_A));
          else if (sym >= XK_0 && sym <= XK_9) keycode = (cj_keycode_t)(CJ_KEY_0 + (sym - XK_0));
          else {
            switch (sym) {
              case XK_F1: keycode = CJ_KEY_F1; break;
              case XK_F2: keycode = CJ_KEY_F2; break;
              case XK_F3: keycode = CJ_KEY_F3; break;
              case XK_F4: keycode = CJ_KEY_F4; break;
              case XK_F5: keycode = CJ_KEY_F5; break;
              case XK_F6: keycode = CJ_KEY_F6; break;
              case XK_F7: keycode = CJ_KEY_F7; break;
              case XK_F8: keycode = CJ_KEY_F8; break;
              case XK_F9: keycode = CJ_KEY_F9; break;
              case XK_F10: keycode = CJ_KEY_F10; break;
              case XK_F11: keycode = CJ_KEY_F11; break;
              case XK_F12: keycode = CJ_KEY_F12; break;
              case XK_Up: keycode = CJ_KEY_UP; break;
              case XK_Down: keycode = CJ_KEY_DOWN; break;
              case XK_Left: keycode = CJ_KEY_LEFT; break;
              case XK_Right: keycode = CJ_KEY_RIGHT; break;
              case XK_Home: keycode = CJ_KEY_HOME; break;
              case XK_End: keycode = CJ_KEY_END; break;
              case XK_Page_Up: keycode = CJ_KEY_PAGE_UP; break;
              case XK_Page_Down: keycode = CJ_KEY_PAGE_DOWN; break;
              case XK_BackSpace: keycode = CJ_KEY_BACKSPACE; break;
              case XK_Delete: keycode = CJ_KEY_DELETE; break;
              case XK_Insert: keycode = CJ_KEY_INSERT; break;
              case XK_Return: keycode = CJ_KEY_ENTER; break;
              case XK_Tab: keycode = CJ_KEY_TAB; break;
              case XK_Escape: keycode = CJ_KEY_ESCAPE; break;
              case XK_Shift_L: keycode = CJ_KEY_LEFT_SHIFT; break;
              case XK_Shift_R: keycode = CJ_KEY_RIGHT_SHIFT; break;
              case XK_Control_L: keycode = CJ_KEY_LEFT_CTRL; break;
              case XK_Control_R: keycode = CJ_KEY_RIGHT_CTRL; break;
              case XK_Alt_L: keycode = CJ_KEY_LEFT_ALT; break;
              case XK_Alt_R: keycode = CJ_KEY_RIGHT_ALT; break;
              case XK_Super_L: keycode = CJ_KEY_LEFT_META; break;
              case XK_Super_R: keycode = CJ_KEY_RIGHT_META; break;
              case XK_space: keycode = CJ_KEY_SPACE; break;
              case XK_minus: keycode = CJ_KEY_MINUS; break;
              case XK_equal: keycode = CJ_KEY_EQUALS; break;
              case XK_bracketleft: keycode = CJ_KEY_BRACKET_LEFT; break;
              case XK_bracketright: keycode = CJ_KEY_BRACKET_RIGHT; break;
              case XK_backslash: keycode = CJ_KEY_BACKSLASH; break;
              case XK_semicolon: keycode = CJ_KEY_SEMICOLON; break;
              case XK_apostrophe: keycode = CJ_KEY_APOSTROPHE; break;
              case XK_grave: keycode = CJ_KEY_GRAVE; break;
              case XK_comma: keycode = CJ_KEY_COMMA; break;
              case XK_period: keycode = CJ_KEY_PERIOD; break;
              case XK_slash: keycode = CJ_KEY_SLASH; break;
              case XK_KP_0: keycode = CJ_KEY_NUMPAD_0; break;
              case XK_KP_1: keycode = CJ_KEY_NUMPAD_1; break;
              case XK_KP_2: keycode = CJ_KEY_NUMPAD_2; break;
              case XK_KP_3: keycode = CJ_KEY_NUMPAD_3; break;
              case XK_KP_4: keycode = CJ_KEY_NUMPAD_4; break;
              case XK_KP_5: keycode = CJ_KEY_NUMPAD_5; break;
              case XK_KP_6: keycode = CJ_KEY_NUMPAD_6; break;
              case XK_KP_7: keycode = CJ_KEY_NUMPAD_7; break;
              case XK_KP_8: keycode = CJ_KEY_NUMPAD_8; break;
              case XK_KP_9: keycode = CJ_KEY_NUMPAD_9; break;
              case XK_KP_Add: keycode = CJ_KEY_NUMPAD_ADD; break;
              case XK_KP_Subtract: keycode = CJ_KEY_NUMPAD_SUBTRACT; break;
              case XK_KP_Multiply: keycode = CJ_KEY_NUMPAD_MULTIPLY; break;
              case XK_KP_Divide: keycode = CJ_KEY_NUMPAD_DIVIDE; break;
              case XK_KP_Decimal: keycode = CJ_KEY_NUMPAD_DECIMAL; break;
              case XK_KP_Enter: keycode = CJ_KEY_NUMPAD_ENTER; break;
              case XK_Caps_Lock: keycode = CJ_KEY_CAPS_LOCK; break;
              case XK_Num_Lock: keycode = CJ_KEY_NUM_LOCK; break;
              case XK_Scroll_Lock: keycode = CJ_KEY_SCROLL_LOCK; break;
              case XK_Print: keycode = CJ_KEY_PRINT_SCREEN; break;
              case XK_Pause: keycode = CJ_KEY_PAUSE; break;
              default: keycode = CJ_KEY_UNKNOWN; break;
            }
          }

          // Get modifiers
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xkey.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xkey.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xkey.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xkey.state & Mod4Mask) modifiers |= CJ_MOD_META;
          // Lock keys: LockMask = Caps Lock, Mod2Mask = Num Lock (typical, may vary)
          if (event.xkey.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xkey.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          // Clear key state (mark as not pressed)
          cj_window__set_key_pressed(window, keycode, false);

          // Dispatch keyboard callback
          cj_window__dispatch_key_callback(window, keycode, (cj_scancode_t)event.xkey.keycode,
                                           CJ_KEY_ACTION_UP, modifiers, false);
        }
      }
    }
    if (event.type == FocusIn) {
      // Window gained focus
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xfocus.window);
        if (window) {
          cj_window__dispatch_focus_callback(window, CJ_FOCUS_GAINED);
        }
      }
    }
    if (event.type == FocusOut) {
      // Window lost focus
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xfocus.window);
        if (window) {
          cj_window__dispatch_focus_callback(window, CJ_FOCUS_LOST);
        }
      }
    }
    if (event.type == MotionNotify) {
      // Mouse moved
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xmotion.window);
        if (window) {
          int32_t x = (int32_t)event.xmotion.x;
          int32_t y = (int32_t)event.xmotion.y;
          int32_t old_x = 0, old_y = 0;
          cj_window__get_mouse_position(window, &old_x, &old_y);
          int32_t dx = x - old_x;
          int32_t dy = y - old_y;

          // For dragging, we need screen-space deltas, not window-relative
          // Use root coordinates (x_root, y_root) to calculate screen-space movement
          int32_t last_root_x, last_root_y;
          bool has_seen_move;
          cj_window__get_mouse_root(window, &last_root_x, &last_root_y, &has_seen_move);

          int32_t screen_dx = 0, screen_dy = 0;
          if (has_seen_move) {
            // Calculate screen-space delta from root coordinates
            screen_dx = (int32_t)event.xmotion.x_root - last_root_x;
            screen_dy = (int32_t)event.xmotion.y_root - last_root_y;
          } else {
            // First move after button press - root coordinates should be initialized from button press
            // If they are, calculate delta normally; otherwise use window-relative as fallback
            if (last_root_x != 0 || last_root_y != 0) {
              // Root coordinates were initialized - calculate delta normally
              screen_dx = (int32_t)event.xmotion.x_root - last_root_x;
              screen_dy = (int32_t)event.xmotion.y_root - last_root_y;
            } else {
              // Root coordinates not initialized - use window-relative delta as fallback
              screen_dx = dx;
              screen_dy = dy;
            }
          }
          // Update root coordinates AFTER calculating delta (for next event)
          cj_window__update_mouse_root(window, (int32_t)event.xmotion.x_root, (int32_t)event.xmotion.y_root);

          // Extract modifiers from event state
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xmotion.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xmotion.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xmotion.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xmotion.state & Mod4Mask) modifiers |= CJ_MOD_META;
          if (event.xmotion.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xmotion.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          cj_mouse_event_t mouse_event = {0};
          mouse_event.type = CJ_MOUSE_MOVE;
          mouse_event.x = x;  // Physical pixels (Linux/X11)
          mouse_event.y = y;  // Physical pixels (Linux/X11)
          mouse_event.screen_x = (int32_t)event.xmotion.x_root;  // Physical pixels (Linux/X11)
          mouse_event.screen_y = (int32_t)event.xmotion.y_root;  // Physical pixels (Linux/X11)
          // On Linux, coordinates are already physical, so copy to _physical fields
          mouse_event.x_physical = x;
          mouse_event.y_physical = y;
          mouse_event.screen_x_physical = (int32_t)event.xmotion.x_root;
          mouse_event.screen_y_physical = (int32_t)event.xmotion.y_root;
          // Use screen-space deltas (calculated above)
          mouse_event.dx = screen_dx;
          mouse_event.dy = screen_dy;
          mouse_event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &mouse_event);
        }
      }
    }
    if (event.type == ButtonPress) {
      // Check for scroll buttons first (Button4 = scroll up, Button5 = scroll down)
      if (event.xbutton.button == Button4 || event.xbutton.button == Button5) {
        // Mouse wheel scroll via traditional X11 button events
        CJellyApplication* app = cjelly_application_get_current();
        if (app) {
          cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xbutton.window);
          if (window) {
            int32_t x = (int32_t)event.xbutton.x;
            int32_t y = (int32_t)event.xbutton.y;
            float scroll_delta = (event.xbutton.button == Button4) ? 1.0f : -1.0f;

            // Extract modifiers
            cj_modifiers_t modifiers = CJ_MOD_NONE;
            if (event.xbutton.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
            if (event.xbutton.state & ControlMask) modifiers |= CJ_MOD_CTRL;
            if (event.xbutton.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
            if (event.xbutton.state & Mod4Mask) modifiers |= CJ_MOD_META;
            if (event.xbutton.state & LockMask) modifiers |= CJ_MOD_CAPS;
            if (event.xbutton.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

            cj_mouse_event_t mouse_event = {0};
            mouse_event.type = CJ_MOUSE_SCROLL;
            mouse_event.x = x;  // Physical pixels (Linux/X11)
            mouse_event.y = y;  // Physical pixels (Linux/X11)
            mouse_event.screen_x = (int32_t)event.xbutton.x_root;  // Physical pixels (Linux/X11)
            mouse_event.screen_y = (int32_t)event.xbutton.y_root;  // Physical pixels (Linux/X11)
            // On Linux, coordinates are already physical, so copy to _physical fields
            mouse_event.x_physical = x;
            mouse_event.y_physical = y;
            mouse_event.screen_x_physical = (int32_t)event.xbutton.x_root;
            mouse_event.screen_y_physical = (int32_t)event.xbutton.y_root;
            mouse_event.scroll_y = scroll_delta;
            mouse_event.modifiers = modifiers;
            cj_window__dispatch_mouse_callback(window, &mouse_event);
          }
        }
        // Important: continue to next event after handling scroll, don't process as regular button
        continue;
      } else {
        // Regular mouse button pressed
        CJellyApplication* app = cjelly_application_get_current();
        if (app) {
          cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xbutton.window);
          if (window) {
            cj_mouse_button_t button = CJ_MOUSE_BUTTON_LEFT;
            if (event.xbutton.button == Button2) button = CJ_MOUSE_BUTTON_MIDDLE;
            else if (event.xbutton.button == Button3) button = CJ_MOUSE_BUTTON_RIGHT;
            else if (event.xbutton.button == 8) button = CJ_MOUSE_BUTTON_4;
            else if (event.xbutton.button == 9) button = CJ_MOUSE_BUTTON_5;

            int32_t x = (int32_t)event.xbutton.x;
            int32_t y = (int32_t)event.xbutton.y;

            // Extract modifiers
            cj_modifiers_t modifiers = CJ_MOD_NONE;
            if (event.xbutton.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
            if (event.xbutton.state & ControlMask) modifiers |= CJ_MOD_CTRL;
            if (event.xbutton.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
            if (event.xbutton.state & Mod4Mask) modifiers |= CJ_MOD_META;
            if (event.xbutton.state & LockMask) modifiers |= CJ_MOD_CAPS;
            if (event.xbutton.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

            // Initialize mouse root coordinates from button press event
            // This ensures we have correct root coordinates when drag starts
            cj_window__update_mouse_root(window, (int32_t)event.xbutton.x_root, (int32_t)event.xbutton.y_root);

            cj_mouse_event_t mouse_event = {0};
            mouse_event.type = CJ_MOUSE_BUTTON_DOWN;
            mouse_event.x = x;  // Physical pixels (Linux/X11)
            mouse_event.y = y;  // Physical pixels (Linux/X11)
            mouse_event.screen_x = (int32_t)event.xbutton.x_root;  // Physical pixels (Linux/X11)
            mouse_event.screen_y = (int32_t)event.xbutton.y_root;  // Physical pixels (Linux/X11)
            // On Linux, coordinates are already physical, so copy to _physical fields
            mouse_event.x_physical = x;
            mouse_event.y_physical = y;
            mouse_event.screen_x_physical = (int32_t)event.xbutton.x_root;
            mouse_event.screen_y_physical = (int32_t)event.xbutton.y_root;
            mouse_event.button = button;
            mouse_event.modifiers = modifiers;
            cj_window__dispatch_mouse_callback(window, &mouse_event);
          }
        }
      }
    }
    if (event.type == ButtonRelease) {
      // Skip ButtonRelease for scroll buttons (Button4/Button5) - scroll events only use ButtonPress
      if (event.xbutton.button == Button4 || event.xbutton.button == Button5) {
        // Scroll buttons don't generate BUTTON_UP events, only SCROLL events via ButtonPress
        continue;
      }
      // Mouse button released
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xbutton.window);
        if (window) {
          cj_mouse_button_t button = CJ_MOUSE_BUTTON_LEFT;
          if (event.xbutton.button == Button2) button = CJ_MOUSE_BUTTON_MIDDLE;
          else if (event.xbutton.button == Button3) button = CJ_MOUSE_BUTTON_RIGHT;
          else if (event.xbutton.button == 8) button = CJ_MOUSE_BUTTON_4;
          else if (event.xbutton.button == 9) button = CJ_MOUSE_BUTTON_5;

          int32_t x = (int32_t)event.xbutton.x;
          int32_t y = (int32_t)event.xbutton.y;

          // Extract modifiers
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xbutton.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xbutton.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xbutton.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xbutton.state & Mod4Mask) modifiers |= CJ_MOD_META;
          if (event.xbutton.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xbutton.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          cj_mouse_event_t mouse_event = {0};
          mouse_event.type = CJ_MOUSE_BUTTON_UP;
          mouse_event.x = x;  // Physical pixels (Linux/X11)
          mouse_event.y = y;  // Physical pixels (Linux/X11)
          mouse_event.screen_x = (int32_t)event.xbutton.x_root;  // Physical pixels (Linux/X11)
          mouse_event.screen_y = (int32_t)event.xbutton.y_root;  // Physical pixels (Linux/X11)
          // On Linux, coordinates are already physical, so copy to _physical fields
          mouse_event.x_physical = x;
          mouse_event.y_physical = y;
          mouse_event.screen_x_physical = (int32_t)event.xbutton.x_root;
          mouse_event.screen_y_physical = (int32_t)event.xbutton.y_root;
          mouse_event.button = button;
          mouse_event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &mouse_event);
        }
      }
    }
    if (event.type == EnterNotify) {
      // Mouse entered window
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xcrossing.window);
        if (window) {
          int32_t x = (int32_t)event.xcrossing.x;
          int32_t y = (int32_t)event.xcrossing.y;

          // Extract modifiers
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xcrossing.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xcrossing.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xcrossing.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xcrossing.state & Mod4Mask) modifiers |= CJ_MOD_META;
          if (event.xcrossing.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xcrossing.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          cj_mouse_event_t mouse_event = {0};
          mouse_event.type = CJ_MOUSE_ENTER;
          mouse_event.x = x;  // Physical pixels (Linux/X11)
          mouse_event.y = y;  // Physical pixels (Linux/X11)
          mouse_event.screen_x = (int32_t)event.xcrossing.x_root;  // Physical pixels (Linux/X11)
          mouse_event.screen_y = (int32_t)event.xcrossing.y_root;  // Physical pixels (Linux/X11)
          // On Linux, coordinates are already physical, so copy to _physical fields
          mouse_event.x_physical = x;
          mouse_event.y_physical = y;
          mouse_event.screen_x_physical = (int32_t)event.xcrossing.x_root;
          mouse_event.screen_y_physical = (int32_t)event.xcrossing.y_root;
          mouse_event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &mouse_event);
        }
      }
    }
    if (event.type == LeaveNotify) {
      // Mouse left window
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)event.xcrossing.window);
        if (window) {
          int32_t x = (int32_t)event.xcrossing.x;
          int32_t y = (int32_t)event.xcrossing.y;

          // Extract modifiers
          cj_modifiers_t modifiers = CJ_MOD_NONE;
          if (event.xcrossing.state & ShiftMask) modifiers |= CJ_MOD_SHIFT;
          if (event.xcrossing.state & ControlMask) modifiers |= CJ_MOD_CTRL;
          if (event.xcrossing.state & Mod1Mask) modifiers |= CJ_MOD_ALT;
          if (event.xcrossing.state & Mod4Mask) modifiers |= CJ_MOD_META;
          if (event.xcrossing.state & LockMask) modifiers |= CJ_MOD_CAPS;
          if (event.xcrossing.state & Mod2Mask) modifiers |= CJ_MOD_NUM;

          cj_mouse_event_t mouse_event = {0};
          mouse_event.type = CJ_MOUSE_LEAVE;
          mouse_event.x = x;  // Physical pixels (Linux/X11)
          mouse_event.y = y;  // Physical pixels (Linux/X11)
          mouse_event.screen_x = (int32_t)event.xcrossing.x_root;  // Physical pixels (Linux/X11)
          mouse_event.screen_y = (int32_t)event.xcrossing.y_root;  // Physical pixels (Linux/X11)
          // On Linux, coordinates are already physical, so copy to _physical fields
          mouse_event.x_physical = x;
          mouse_event.y_physical = y;
          mouse_event.screen_x_physical = (int32_t)event.xcrossing.x_root;
          mouse_event.screen_y_physical = (int32_t)event.xcrossing.y_root;
          mouse_event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &mouse_event);
        }
      }
    }
  }
}
