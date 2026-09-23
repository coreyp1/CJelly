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
 * CJelly — Win32 keycodes, DPI and the window procedure
 *
 * Lifted from window.c, where it was two `#ifdef _WIN32` blocks totalling
 * 723 lines that no build on this machine has ever compiled. Virtual-key
 * and modifier mapping, per-monitor DPI, and the window procedure itself.
 */

#include <ghoti.io/cjelly/platform_internal.h>

#include <shellscalingapi.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/application.h>
#include <ghoti.io/cjelly/cj_input.h>
#include <ghoti.io/cjelly/cj_window.h>
#include <ghoti.io/cjelly/window_internal.h>

/* Map Windows virtual key code (VK_*) to cj_keycode_t */
static cj_keycode_t map_windows_keycode(WPARAM vk) {
  /* Letters */
  if (vk >= 'A' && vk <= 'Z') return (cj_keycode_t)(CJ_KEY_A + (vk - 'A'));
  /* Numbers */
  if (vk >= '0' && vk <= '9') return (cj_keycode_t)(CJ_KEY_0 + (vk - '0'));

  switch (vk) {
    case VK_F1: return CJ_KEY_F1;
    case VK_F2: return CJ_KEY_F2;
    case VK_F3: return CJ_KEY_F3;
    case VK_F4: return CJ_KEY_F4;
    case VK_F5: return CJ_KEY_F5;
    case VK_F6: return CJ_KEY_F6;
    case VK_F7: return CJ_KEY_F7;
    case VK_F8: return CJ_KEY_F8;
    case VK_F9: return CJ_KEY_F9;
    case VK_F10: return CJ_KEY_F10;
    case VK_F11: return CJ_KEY_F11;
    case VK_F12: return CJ_KEY_F12;

    case VK_UP: return CJ_KEY_UP;
    case VK_DOWN: return CJ_KEY_DOWN;
    case VK_LEFT: return CJ_KEY_LEFT;
    case VK_RIGHT: return CJ_KEY_RIGHT;
    case VK_HOME: return CJ_KEY_HOME;
    case VK_END: return CJ_KEY_END;
    case VK_PRIOR: return CJ_KEY_PAGE_UP;  /* Page Up */
    case VK_NEXT: return CJ_KEY_PAGE_DOWN; /* Page Down */

    case VK_BACK: return CJ_KEY_BACKSPACE;
    case VK_DELETE: return CJ_KEY_DELETE;
    case VK_INSERT: return CJ_KEY_INSERT;
    case VK_RETURN: return CJ_KEY_ENTER;
    case VK_TAB: return CJ_KEY_TAB;
    case VK_ESCAPE: return CJ_KEY_ESCAPE;

    case VK_LSHIFT: return CJ_KEY_LEFT_SHIFT;
    case VK_RSHIFT: return CJ_KEY_RIGHT_SHIFT;
    case VK_LCONTROL: return CJ_KEY_LEFT_CTRL;
    case VK_RCONTROL: return CJ_KEY_RIGHT_CTRL;
    case VK_LMENU: return CJ_KEY_LEFT_ALT;  /* Left Alt */
    case VK_RMENU: return CJ_KEY_RIGHT_ALT; /* Right Alt */
    case VK_LWIN: return CJ_KEY_LEFT_META;
    case VK_RWIN: return CJ_KEY_RIGHT_META;

    case VK_SPACE: return CJ_KEY_SPACE;
    case VK_OEM_MINUS: return CJ_KEY_MINUS;  /* - on main keyboard */
    case VK_OEM_PLUS: return CJ_KEY_EQUALS;  /* = on main keyboard */
    case VK_OEM_4: return CJ_KEY_BRACKET_LEFT;  /* [ */
    case VK_OEM_6: return CJ_KEY_BRACKET_RIGHT; /* ] */
    case VK_OEM_5: return CJ_KEY_BACKSLASH;  /* \ */
    case VK_OEM_1: return CJ_KEY_SEMICOLON; /* ; */
    case VK_OEM_7: return CJ_KEY_APOSTROPHE; /* ' */
    case VK_OEM_3: return CJ_KEY_GRAVE;  /* ` */
    case VK_OEM_COMMA: return CJ_KEY_COMMA;
    case VK_OEM_PERIOD: return CJ_KEY_PERIOD;
    case VK_OEM_2: return CJ_KEY_SLASH;  /* / */

    case VK_NUMPAD0: return CJ_KEY_NUMPAD_0;
    case VK_NUMPAD1: return CJ_KEY_NUMPAD_1;
    case VK_NUMPAD2: return CJ_KEY_NUMPAD_2;
    case VK_NUMPAD3: return CJ_KEY_NUMPAD_3;
    case VK_NUMPAD4: return CJ_KEY_NUMPAD_4;
    case VK_NUMPAD5: return CJ_KEY_NUMPAD_5;
    case VK_NUMPAD6: return CJ_KEY_NUMPAD_6;
    case VK_NUMPAD7: return CJ_KEY_NUMPAD_7;
    case VK_NUMPAD8: return CJ_KEY_NUMPAD_8;
    case VK_NUMPAD9: return CJ_KEY_NUMPAD_9;
    case VK_ADD: return CJ_KEY_NUMPAD_ADD;
    case VK_SUBTRACT: return CJ_KEY_NUMPAD_SUBTRACT;
    case VK_MULTIPLY: return CJ_KEY_NUMPAD_MULTIPLY;
    case VK_DIVIDE: return CJ_KEY_NUMPAD_DIVIDE;
    case VK_DECIMAL: return CJ_KEY_NUMPAD_DECIMAL;
    case VK_SEPARATOR: return CJ_KEY_NUMPAD_ENTER;  /* Numpad Enter */

    case VK_CAPITAL: return CJ_KEY_CAPS_LOCK;
    case VK_NUMLOCK: return CJ_KEY_NUM_LOCK;
    case VK_SCROLL: return CJ_KEY_SCROLL_LOCK;
    case VK_SNAPSHOT: return CJ_KEY_PRINT_SCREEN;
    case VK_PAUSE: return CJ_KEY_PAUSE;

    default: return CJ_KEY_UNKNOWN;
  }
}

/* Get modifier flags from Windows keyboard state */
static cj_modifiers_t get_windows_modifiers(void) {
  cj_modifiers_t mods = CJ_MOD_NONE;
  if (GetKeyState(VK_SHIFT) & 0x8000) mods |= CJ_MOD_SHIFT;
  if (GetKeyState(VK_CONTROL) & 0x8000) mods |= CJ_MOD_CTRL;
  if (GetKeyState(VK_MENU) & 0x8000) mods |= CJ_MOD_ALT;  /* Alt */
  if (GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000) mods |= CJ_MOD_META;
  if (GetKeyState(VK_CAPITAL) & 0x0001) mods |= CJ_MOD_CAPS;  /* Caps Lock (toggle state) */
  if (GetKeyState(VK_NUMLOCK) & 0x0001) mods |= CJ_MOD_NUM;  /* Num Lock (toggle state) */
  return mods;
}

// MDT_EFFECTIVE_DPI constant (if not defined in headers)
#ifndef MDT_EFFECTIVE_DPI
#define MDT_EFFECTIVE_DPI 0
#endif

/* Timer ID for resize rendering */
#define CJ_RESIZE_TIMER_ID 1
#define CJ_RESIZE_TIMER_MS 16  /* ~60 FPS during resize */

/* Forward declaration for rendering during resize */

/**
 * @brief Get DPI for a window
 * @param hwnd Window handle
 * @return DPI value (96 = 100% scaling)
 */
UINT cj_win32_window_dpi(HWND hwnd) {
  // Try GetDpiForWindow first (Windows 10 1607+)
  typedef UINT (WINAPI *GetDpiForWindowFunc)(HWND);
  static GetDpiForWindowFunc get_dpi_for_window = NULL;
  static bool tried_load = false;

  if (!tried_load) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32) {
      FARPROC proc = GetProcAddress(user32, "GetDpiForWindow");
      if (proc) {
        // Use union to convert between function pointer types (ISO C compliant)
        union {
          FARPROC farproc;
          GetDpiForWindowFunc func;
        } u;
        u.farproc = proc;
        get_dpi_for_window = u.func;
      }
    }
    tried_load = true;
  }

  if (get_dpi_for_window) {
    return get_dpi_for_window(hwnd);
  }

  // Fallback: GetDpiForMonitor (Windows 8.1+)
  {
    // MONITOR_DPI_TYPE is an enum, use int if not defined
    typedef HRESULT (WINAPI *GetDpiForMonitorFunc)(HMONITOR, int, UINT*, UINT*);
    static GetDpiForMonitorFunc get_dpi_for_monitor = NULL;
    static bool tried_load_monitor = false;

    if (!tried_load_monitor) {
      HMODULE shcore = LoadLibraryA("shcore.dll");
      if (shcore) {
        FARPROC proc = GetProcAddress(shcore, "GetDpiForMonitor");
        if (proc) {
          // Use union to convert between function pointer types (ISO C compliant)
          union {
            FARPROC farproc;
            GetDpiForMonitorFunc func;
          } u;
          u.farproc = proc;
          get_dpi_for_monitor = u.func;
        }
        FreeLibrary(shcore);
      }
      tried_load_monitor = true;
    }

    if (get_dpi_for_monitor) {
      HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
      UINT dpi_x, dpi_y;
      if (SUCCEEDED(get_dpi_for_monitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
        return dpi_x;  // Usually same as dpi_y
      }
    }
  }

  // Final fallback: System DPI
  HDC hdc = GetDC(hwnd);
  int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
  ReleaseDC(hwnd, hdc);
  return dpi;
}

/**
 * @brief Convert DPI to scale factor
 * @param dpi DPI value
 * @return Scale factor (1.0 = 96 DPI)
 */
float cj_win32_dpi_to_scale(UINT dpi) {
  return (float)dpi / 96.0f;  // 96 DPI = 1.0 scale
}

/* Forward declarations for helper functions */
static int32_t logical_to_physical(int32_t logical, float dpi_scale);

/*
 * Windows window procedure.
 *
 * Window closing flow:
 * 1. User clicks X -> WM_CLOSE sent
 * 2. WM_CLOSE calls close callback, then cj_window_destroy() if allowed
 * 3. cj_window_destroy() does all cleanup and calls DestroyWindow()
 * 4. WM_DESTROY is sent but is a no-op (cleanup already done)
 *
 * Resize handling:
 * Windows enters a modal loop during resize/move operations (WM_ENTERSIZEMOVE).
 * During this modal loop, our main event loop doesn't run. To keep rendering,
 * we start a timer that fires periodically to render frames.
 */
LRESULT CALLBACK cj_win32_wnd_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    case WM_CLOSE: {
      // User requested window close (clicked X button)
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          // Invoke close callback if present
          cj_window_close_response_t response = CJ_WINDOW_CLOSE_ALLOW;
          if (window->close_callback) {
            response = window->close_callback(window, true, window->close_callback_user_data);
          }

          if (response == CJ_WINDOW_CLOSE_ALLOW) {
            // Destroy window - this handles all cleanup
            cj_window_destroy(window);
          }
          // Return 0 to indicate we handled the message (whether closed or not)
          return 0;
        }
      }
      // Window not found or invalid - use default behavior
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    case WM_ENTERSIZEMOVE: {
      // Windows is entering the modal resize/move loop.
      // Start a timer to keep rendering during this modal loop.
      SetTimer(hwnd, CJ_RESIZE_TIMER_ID, CJ_RESIZE_TIMER_MS, NULL);
      return 0;
    }

    case WM_EXITSIZEMOVE: {
      // Windows is exiting the modal resize/move loop.
      // Stop the timer.
      KillTimer(hwnd, CJ_RESIZE_TIMER_ID);
      return 0;
    }

    case WM_TIMER: {
      if (wParam == CJ_RESIZE_TIMER_ID) {
        // Timer fired during modal resize loop - render a frame
        CJellyApplication* app = cjelly_application_get_current();
        if (app) {
          cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
          if (window && !window->is_destroyed) {
            cj_window__render_frame_immediate(window);
          }
        }
        return 0;
      }
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    case WM_DPICHANGED: {
      // DPI changed (e.g., window moved to different monitor)
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && window->plat) {
          // wParam contains new DPI (low word = X, high word = Y)
          UINT new_dpi = LOWORD(wParam);
          float new_scale = cj_win32_dpi_to_scale(new_dpi);
          window->plat->dpi_scale = new_scale;

          // lParam contains suggested new window rect in logical pixels
          // This rect accounts for the DPI change and maintains the same physical size
          RECT* suggested_rect = (RECT*)lParam;

          // Update window size/position to suggested rect
          // This ensures the window maintains appropriate size on the new monitor
          SetWindowPos(hwnd, NULL,
                      suggested_rect->left, suggested_rect->top,
                      suggested_rect->right - suggested_rect->left,
                      suggested_rect->bottom - suggested_rect->top,
                      SWP_NOZORDER | SWP_NOACTIVATE);

          // Update cached position and size
          cj_window__set_position(window, suggested_rect->left, suggested_rect->top);
          window->plat->width = suggested_rect->right - suggested_rect->left;
          window->plat->height = suggested_rect->bottom - suggested_rect->top;

          // Mark swapchain for recreation (physical size may have changed)
          window->plat->needs_swapchain_recreate = true;

          // Dispatch resize callback (size in logical pixels may have changed)
          cj_window__dispatch_resize_callback(window,
                                              (uint32_t)(suggested_rect->right - suggested_rect->left),
                                              (uint32_t)(suggested_rect->bottom - suggested_rect->top));
        }
      }
      return 0;
    }

    case WM_MOVE: {
      // Window position changed
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && window->plat) {
          // WM_MOVE provides client area position, use GetWindowRect for frame position
          RECT rect;
          if (GetWindowRect(hwnd, &rect)) {
            int32_t new_x = rect.left;
            int32_t new_y = rect.top;
            // GetWindowRect returns logical pixels for DPI-aware apps
            if (new_x != window->plat->x || new_y != window->plat->y) {
              cj_window__set_position(window, new_x, new_y);
              cj_window__dispatch_move_callback(window, new_x, new_y);
            }
          }
        }
      }
      return 0;
    }

    case WM_SIZE: {
      // Track minimized/restored state and handle resize
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && window->plat) {
          // wParam: SIZE_MINIMIZED, SIZE_MAXIMIZED, SIZE_RESTORED, SIZE_MAXSHOW, SIZE_MAXHIDE
          cj_window_state_t new_state = window->plat->state;
          if (wParam == SIZE_MINIMIZED) {
            cj_window__set_minimized(window, true);
            new_state = CJ_WINDOW_STATE_MINIMIZED;
          } else if (wParam == SIZE_MAXIMIZED) {
            cj_window__set_minimized(window, false);
            new_state = CJ_WINDOW_STATE_MAXIMIZED;
          } else if (wParam == SIZE_RESTORED) {
            cj_window__set_minimized(window, false);
            new_state = CJ_WINDOW_STATE_NORMAL;
            /* Mark window dirty when restored from minimized */
            window->plat->needsRedraw = 1;
            window->pending_render_reason = CJ_RENDER_REASON_EXPOSE;
          }

          // Dispatch state change callback if state changed
          if (new_state != window->plat->state) {
            window->plat->state = new_state;
            cj_window__dispatch_state_callback(window, new_state);
          }

          // Extract new width and height from lParam
          uint32_t new_width = (uint32_t)(LOWORD(lParam));
          uint32_t new_height = (uint32_t)(HIWORD(lParam));

          // Only update if size actually changed
          if ((int)new_width != window->plat->width || (int)new_height != window->plat->height) {
            // Update size and mark swapchain for recreation
            cj_window__update_size_and_mark_recreate(window, new_width, new_height);

            // Dispatch resize callback (user can do additional work)
            cj_window__dispatch_resize_callback(window, new_width, new_height);
          }
        }
      }
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      // Key pressed (including system keys like Alt combinations)
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          cj_keycode_t keycode = map_windows_keycode(wParam);
          cj_scancode_t scancode = (cj_scancode_t)((lParam >> 16) & 0xFF);  /* Extract scancode from lParam */
          cj_modifiers_t modifiers = get_windows_modifiers();
          bool is_repeat = (lParam & (1 << 30)) != 0;  /* Previous key state bit indicates repeat */

          cj_window__dispatch_key_callback(window, keycode, scancode, CJ_KEY_ACTION_DOWN, modifiers, is_repeat);
        }
      }
      // For system keys, we might want to call DefWindowProc to allow default handling
      // For regular keys, we can return 0 to indicate we handled it
      if (uMsg == WM_SYSKEYDOWN) {
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
      }
      return 0;  /* Handled */
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
      // Key released
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          cj_keycode_t keycode = map_windows_keycode(wParam);
          cj_scancode_t scancode = (cj_scancode_t)((lParam >> 16) & 0xFF);  /* Extract scancode from lParam */
          cj_modifiers_t modifiers = get_windows_modifiers();

          cj_window__dispatch_key_callback(window, keycode, scancode, CJ_KEY_ACTION_UP, modifiers, false);
        }
      }
      // For system keys, we might want to call DefWindowProc to allow default handling
      if (uMsg == WM_SYSKEYUP) {
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
      }
      return 0;  /* Handled */
    }

    case WM_SETFOCUS: {
      // Window gained focus
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          cj_window__dispatch_focus_callback(window, CJ_FOCUS_GAINED);
        }
      }
      return 0;
    }

    case WM_KILLFOCUS: {
      // Window lost focus
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          cj_window__dispatch_focus_callback(window, CJ_FOCUS_LOST);
        }
      }
      return 0;
    }

    case WM_MOUSEMOVE: {
      // Mouse moved
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          int32_t x = (int32_t)(short)LOWORD(lParam);
          int32_t y = (int32_t)(short)HIWORD(lParam);
          int32_t dx = x - window->mouse_x;
          int32_t dy = y - window->mouse_y;
          cj_modifiers_t modifiers = get_windows_modifiers();

          // Convert to screen coordinates (logical pixels)
          POINT screen_pt = {x, y};
          ClientToScreen(hwnd, &screen_pt);

          // Get DPI scale for physical pixel conversion
          float dpi_scale = window->plat->dpi_scale;

          cj_mouse_event_t event = {0};
          event.type = CJ_MOUSE_MOVE;
          event.x = x;  // Logical pixels (Windows DPI-aware)
          event.y = y;  // Logical pixels (Windows DPI-aware)
          event.screen_x = screen_pt.x;  // Logical pixels (Windows DPI-aware)
          event.screen_y = screen_pt.y;  // Logical pixels (Windows DPI-aware)
          event.x_physical = logical_to_physical(x, dpi_scale);
          event.y_physical = logical_to_physical(y, dpi_scale);
          event.screen_x_physical = logical_to_physical(screen_pt.x, dpi_scale);
          event.screen_y_physical = logical_to_physical(screen_pt.y, dpi_scale);
          event.dx = dx;
          event.dy = dy;
          event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &event);
        }
      }
      return 0;
    }

    case WM_LBUTTONDBLCLK: {
      // Left button double-click
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          int32_t x = (int32_t)(short)LOWORD(lParam);
          int32_t y = (int32_t)(short)HIWORD(lParam);
          cj_modifiers_t modifiers = get_windows_modifiers();

          // Convert to screen coordinates (logical pixels)
          POINT screen_pt = {x, y};
          ClientToScreen(hwnd, &screen_pt);

          // Get DPI scale for physical pixel conversion
          float dpi_scale = window->plat->dpi_scale;

          cj_mouse_event_t event = {0};
          event.type = CJ_MOUSE_BUTTON_DOWN;
          event.x = x;  // Logical pixels (Windows DPI-aware)
          event.y = y;  // Logical pixels (Windows DPI-aware)
          event.screen_x = screen_pt.x;  // Logical pixels (Windows DPI-aware)
          event.screen_y = screen_pt.y;  // Logical pixels (Windows DPI-aware)
          event.x_physical = logical_to_physical(x, dpi_scale);
          event.y_physical = logical_to_physical(y, dpi_scale);
          event.screen_x_physical = logical_to_physical(screen_pt.x, dpi_scale);
          event.screen_y_physical = logical_to_physical(screen_pt.y, dpi_scale);
          event.button = CJ_MOUSE_BUTTON_LEFT;
          event.modifiers = modifiers;
          // Dispatch as BUTTON_DOWN - the callback can detect double-click via timing
          // or we could add a double-click event type in the future
          cj_window__dispatch_mouse_callback(window, &event);
        }
      }
      return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_XBUTTONDOWN: {
      // Mouse button pressed
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          cj_mouse_button_t button = CJ_MOUSE_BUTTON_LEFT;
          if (uMsg == WM_MBUTTONDOWN) button = CJ_MOUSE_BUTTON_MIDDLE;
          else if (uMsg == WM_RBUTTONDOWN) button = CJ_MOUSE_BUTTON_RIGHT;
          else if (uMsg == WM_XBUTTONDOWN) {
            if (HIWORD(wParam) == XBUTTON1) button = CJ_MOUSE_BUTTON_4;
            else if (HIWORD(wParam) == XBUTTON2) button = CJ_MOUSE_BUTTON_5;
          }

          int32_t x = (int32_t)(short)LOWORD(lParam);
          int32_t y = (int32_t)(short)HIWORD(lParam);
          cj_modifiers_t modifiers = get_windows_modifiers();

          // Convert to screen coordinates (logical pixels)
          POINT screen_pt = {x, y};
          ClientToScreen(hwnd, &screen_pt);

          // Get DPI scale for physical pixel conversion
          float dpi_scale = window->plat->dpi_scale;

          cj_mouse_event_t event = {0};
          event.type = CJ_MOUSE_BUTTON_DOWN;
          event.x = x;  // Logical pixels (Windows DPI-aware)
          event.y = y;  // Logical pixels (Windows DPI-aware)
          event.screen_x = screen_pt.x;  // Logical pixels (Windows DPI-aware)
          event.screen_y = screen_pt.y;  // Logical pixels (Windows DPI-aware)
          event.x_physical = logical_to_physical(x, dpi_scale);
          event.y_physical = logical_to_physical(y, dpi_scale);
          event.screen_x_physical = logical_to_physical(screen_pt.x, dpi_scale);
          event.screen_y_physical = logical_to_physical(screen_pt.y, dpi_scale);
          event.button = button;
          event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &event);
        }
      }
      return 0;
    }

    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    case WM_XBUTTONUP: {
      // Mouse button released
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          cj_mouse_button_t button = CJ_MOUSE_BUTTON_LEFT;
          if (uMsg == WM_MBUTTONUP) button = CJ_MOUSE_BUTTON_MIDDLE;
          else if (uMsg == WM_RBUTTONUP) button = CJ_MOUSE_BUTTON_RIGHT;
          else if (uMsg == WM_XBUTTONUP) {
            if (HIWORD(wParam) == XBUTTON1) button = CJ_MOUSE_BUTTON_4;
            else if (HIWORD(wParam) == XBUTTON2) button = CJ_MOUSE_BUTTON_5;
          }

          int32_t x = (int32_t)(short)LOWORD(lParam);
          int32_t y = (int32_t)(short)HIWORD(lParam);
          cj_modifiers_t modifiers = get_windows_modifiers();

          // Convert to screen coordinates (logical pixels)
          POINT screen_pt = {x, y};
          ClientToScreen(hwnd, &screen_pt);

          // Get DPI scale for physical pixel conversion
          float dpi_scale = window->plat->dpi_scale;

          cj_mouse_event_t event = {0};
          event.type = CJ_MOUSE_BUTTON_UP;
          event.x = x;  // Logical pixels (Windows DPI-aware)
          event.y = y;  // Logical pixels (Windows DPI-aware)
          event.screen_x = screen_pt.x;  // Logical pixels (Windows DPI-aware)
          event.screen_y = screen_pt.y;  // Logical pixels (Windows DPI-aware)
          event.x_physical = logical_to_physical(x, dpi_scale);
          event.y_physical = logical_to_physical(y, dpi_scale);
          event.screen_x_physical = logical_to_physical(screen_pt.x, dpi_scale);
          event.screen_y_physical = logical_to_physical(screen_pt.y, dpi_scale);
          event.button = button;
          event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &event);
        }
      }
      return 0;
    }

    case WM_MOUSEWHEEL: {
      // Mouse wheel scroll (vertical)
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          POINT pt = {LOWORD(lParam), HIWORD(lParam)};
          ScreenToClient(hwnd, &pt);
          float delta = (float)(short)HIWORD(wParam) / (float)WHEEL_DELTA;
          cj_modifiers_t modifiers = get_windows_modifiers();

          // Get DPI scale for physical pixel conversion
          float dpi_scale = window->plat->dpi_scale;

          // Convert to screen coordinates for screen_x/screen_y
          POINT screen_pt = {pt.x, pt.y};
          ClientToScreen(hwnd, &screen_pt);

          cj_mouse_event_t event = {0};
          event.type = CJ_MOUSE_SCROLL;
          event.x = pt.x;  // Logical pixels (Windows DPI-aware)
          event.y = pt.y;  // Logical pixels (Windows DPI-aware)
          event.screen_x = screen_pt.x;  // Logical pixels (Windows DPI-aware)
          event.screen_y = screen_pt.y;  // Logical pixels (Windows DPI-aware)
          event.x_physical = logical_to_physical(pt.x, dpi_scale);
          event.y_physical = logical_to_physical(pt.y, dpi_scale);
          event.screen_x_physical = logical_to_physical(screen_pt.x, dpi_scale);
          event.screen_y_physical = logical_to_physical(screen_pt.y, dpi_scale);
          event.scroll_y = delta;
          event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &event);
        }
      }
      return 0;
    }

    case WM_MOUSEHWHEEL: {
      // Mouse wheel scroll (horizontal)
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          POINT pt = {LOWORD(lParam), HIWORD(lParam)};
          ScreenToClient(hwnd, &pt);
          float delta = (float)(short)HIWORD(wParam) / (float)WHEEL_DELTA;
          cj_modifiers_t modifiers = get_windows_modifiers();

          // Get DPI scale for physical pixel conversion
          float dpi_scale = window->plat->dpi_scale;

          // Convert to screen coordinates for screen_x/screen_y
          POINT screen_pt = {pt.x, pt.y};
          ClientToScreen(hwnd, &screen_pt);

          cj_mouse_event_t event = {0};
          event.type = CJ_MOUSE_SCROLL;
          event.x = pt.x;  // Logical pixels (Windows DPI-aware)
          event.y = pt.y;  // Logical pixels (Windows DPI-aware)
          event.screen_x = screen_pt.x;  // Logical pixels (Windows DPI-aware)
          event.screen_y = screen_pt.y;  // Logical pixels (Windows DPI-aware)
          event.x_physical = logical_to_physical(pt.x, dpi_scale);
          event.y_physical = logical_to_physical(pt.y, dpi_scale);
          event.screen_x_physical = logical_to_physical(screen_pt.x, dpi_scale);
          event.screen_y_physical = logical_to_physical(screen_pt.y, dpi_scale);
          event.scroll_x = delta;
          event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &event);
        }
      }
      return 0;
    }

    case WM_MOUSELEAVE: {
      // Mouse left window
      CJellyApplication* app = cjelly_application_get_current();
      if (app) {
        cj_window_t* window = (cj_window_t*)cjelly_application_find_window_by_handle(app, (void*)hwnd);
        if (window && !window->is_destroyed) {
          cj_modifiers_t modifiers = get_windows_modifiers();

          // Get DPI scale for physical pixel conversion
          float dpi_scale = window->plat->dpi_scale;

          // Convert cached mouse position to screen coordinates
          POINT screen_pt = {window->mouse_x, window->mouse_y};
          ClientToScreen(hwnd, &screen_pt);

          cj_mouse_event_t event = {0};
          event.type = CJ_MOUSE_LEAVE;
          event.x = window->mouse_x;  // Logical pixels (Windows DPI-aware)
          event.y = window->mouse_y;  // Logical pixels (Windows DPI-aware)
          event.screen_x = screen_pt.x;  // Logical pixels (Windows DPI-aware)
          event.screen_y = screen_pt.y;  // Logical pixels (Windows DPI-aware)
          event.x_physical = logical_to_physical(window->mouse_x, dpi_scale);
          event.y_physical = logical_to_physical(window->mouse_y, dpi_scale);
          event.screen_x_physical = logical_to_physical(screen_pt.x, dpi_scale);
          event.screen_y_physical = logical_to_physical(screen_pt.y, dpi_scale);
          event.modifiers = modifiers;
          cj_window__dispatch_mouse_callback(window, &event);
        }
      }
      return 0;
    }

    case WM_DESTROY:
      // Window is being destroyed. Cleanup is already done by cj_window_destroy(),
      // so this is just a no-op. The user data should already be cleared.
      return DefWindowProc(hwnd, uMsg, wParam, lParam);

    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}


/* === The portable seam, Win32 side === */

uint64_t cj_plat_now_ms(void) {
  LARGE_INTEGER frequency;
  LARGE_INTEGER counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return (uint64_t)((counter.QuadPart * 1000LL) / frequency.QuadPart);
}

bool cj_plat_capture_mouse(uintptr_t handle) {
  HWND hwnd = (HWND)handle;
  if (!hwnd || !IsWindow(hwnd)) return false;
  SetCapture(hwnd);
  return true;
}

bool cj_plat_release_mouse(void) {
  ReleaseCapture();
  return true;
}

bool cj_plat_query_position(uintptr_t handle, int32_t* out_x, int32_t* out_y) {
  /* Windows knows where the window is, including after the user moved it. */
  RECT rect;
  if (!handle || !GetWindowRect((HWND)handle, &rect)) return false;
  /* TODO: Apply DPI scaling conversion (for now, assume 1.0 scale) */
  if (out_x) *out_x = rect.left;
  if (out_y) *out_y = rect.top;
  return true;
}

bool cj_plat_query_state(uintptr_t handle, cj_window_state_t* out_state) {
  HWND hwnd = (HWND)handle;
  if (!hwnd || !out_state) return false;
  if (IsZoomed(hwnd))      *out_state = CJ_WINDOW_STATE_MAXIMIZED;
  else if (IsIconic(hwnd)) *out_state = CJ_WINDOW_STATE_MINIMIZED;
  else                     *out_state = CJ_WINDOW_STATE_NORMAL;
  return true;
}

bool cj_plat_move_window(uintptr_t handle, int32_t x, int32_t y,
    int32_t cur_x, int32_t cur_y) {
  (void)cur_x; (void)cur_y;
  if (!handle) return false;
  /* TODO: Apply DPI scaling conversion (for now, assume 1.0 scale) */
  SetWindowPos((HWND)handle, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  /* Windows does not feed programmatic moves back as user moves, so there is
   * nothing for the caller to suppress. */
  return false;
}

cj_result_t cj_plat_set_window_state(uintptr_t handle, cj_window_state_t state) {
  HWND hwnd = (HWND)handle;
  if (!hwnd) return CJ_E_INVALID_ARGUMENT;
  int show_cmd;
  switch (state) {
    case CJ_WINDOW_STATE_NORMAL:     show_cmd = SW_RESTORE;  break;
    case CJ_WINDOW_STATE_MAXIMIZED:  show_cmd = SW_MAXIMIZE; break;
    case CJ_WINDOW_STATE_MINIMIZED:  show_cmd = SW_MINIMIZE; break;
    case CJ_WINDOW_STATE_FULLSCREEN: return CJ_E_UNSUPPORTED; /* Not implemented yet */
    default:                         return CJ_E_INVALID_ARGUMENT;
  }
  ShowWindow(hwnd, show_cmd);
  return CJ_SUCCESS;
}

void cj_plat_bind_window_user_data(uintptr_t handle, void* user) {
  /* Retrieved again in WM_DESTROY, after the window has been unregistered
   * from the application. */
  if (handle) SetWindowLongPtr((HWND)handle, GWLP_USERDATA, (LONG_PTR)user);
}

void cj_plat_unbind_window_user_data(uintptr_t handle) {
  HWND hwnd = (HWND)handle;
  if (hwnd && IsWindow(hwnd)) SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
}

void cj_plat_destroy_native_window(uintptr_t handle) {
  HWND hwnd = (HWND)handle;
  if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
}

bool cj_plat_window_is_alive(uintptr_t handle) {
  HWND hwnd = (HWND)handle;
  return hwnd != NULL && IsWindow(hwnd);
}

VkResult cj_plat_create_surface(uintptr_t handle, VkInstance instance,
    VkSurfaceKHR* out_surface) {
  VkWin32SurfaceCreateInfoKHR ci = {0};
  ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  ci.hinstance = GetModuleHandle(NULL);
  ci.hwnd = (HWND)handle;
  return vkCreateWin32SurfaceKHR(instance, &ci, NULL, out_surface);
}

void cj_plat_create_window(const char* title, int width, int height,
    int32_t x, int32_t y, cj_window_state_t initial_state,
    cj_plat_window_t* out) {
  if (!out) return;
  HINSTANCE hInstance = GetModuleHandle(NULL);
  WNDCLASS wc = {0};
  wc.lpfnWndProc = cj_win32_wnd_proc;
  wc.hInstance = hInstance;
  wc.lpszClassName = "CJellyWindow";
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS; /* CS_DBLCLKS for double-click support */
  RegisterClass(&wc);

  int win_x = (x == CJ_WINDOW_POSITION_DEFAULT) ? CW_USEDEFAULT : x;
  int win_y = (y == CJ_WINDOW_POSITION_DEFAULT) ? CW_USEDEFAULT : y;

  /* TODO(windows): width/height here size the whole frame and the client area
   * is fitted inside it, where X11 sizes the client area and the window
   * manager hangs decoration outside. The same cj_window_desc_t therefore
   * gives a smaller drawable on Windows. AdjustWindowRectEx() is the fix, but
   * it changes the size of every window in every application using CJelly, so
   * it should land on a machine that can verify it. Same split applies to
   * win_x/win_y. See WINDOWS-TODO.md items 1 and 2. */
  HWND hwnd = CreateWindowEx(0, "CJellyWindow", title, WS_OVERLAPPEDWINDOW,
      win_x, win_y, width, height, NULL, NULL, hInstance, NULL);
  if (!hwnd) return;
  out->handle = (uintptr_t)hwnd;

  int show_cmd = SW_SHOWNORMAL;
  if (initial_state == CJ_WINDOW_STATE_MAXIMIZED)      show_cmd = SW_SHOWMAXIMIZED;
  else if (initial_state == CJ_WINDOW_STATE_MINIMIZED) show_cmd = SW_SHOWMINIMIZED;
  ShowWindow(hwnd, show_cmd);

  RECT rect;
  if (GetWindowRect(hwnd, &rect)) {
    out->x = rect.left;
    out->y = rect.top;
  }

  out->dpi_scale = cj_win32_dpi_to_scale(cj_win32_window_dpi(hwnd));
}

const char* cj_plat_surface_extension_name(void) {
  return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
}

bool cj_plat_open_display(void) {
  /* Win32 has no connection to open; a process talks to the window manager
   * through its own module handle. */
  return true;
}

void cj_plat_close_display(void) {
}

uint64_t cj_plat_now_us(void) {
  LARGE_INTEGER frequency, counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  /* TODO(windows): counter.QuadPart * 1000000 overflows a signed 64-bit
   * value once the machine has been up long enough - about 106 days at a
   * 10 MHz performance counter. Pre-existing; it wants the divide split
   * into whole seconds plus remainder. */
  return (uint64_t)((counter.QuadPart * 1000000ULL) / frequency.QuadPart);
}

void cj_plat_sleep_ms(uint32_t ms) {
  if (ms == 0) return;
  Sleep((DWORD)ms);
}
