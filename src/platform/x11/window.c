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
 * CJelly — X11 window geometry and DPI
 *
 * Lifted from window.c, where it was a 203-line `#ifndef _WIN32` block that
 * no Windows build ever compiled. Frame extents from the window manager,
 * per-monitor DPI from XRandR, and the scale lookup window.c asks for when
 * it places a window.
 */

/* clock_gettime() is POSIX, and platform_internal.h pulls in system headers
 * before cjelly/macros.h gets a chance to ask for it. Declaring the level
 * here, above every include, is the only place it takes effect. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <ghoti.io/cjelly/platform_internal.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xutil.h>

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/cj_window.h>
#include <ghoti.io/cjelly/window_internal.h>

// Linux window handling is done in processWindowEvents() in cjelly.c
// No window procedure needed for X11

#include <math.h>

/**
 * @brief Query window frame extents (decoration sizes) from window manager.
 * @param dpy X11 display.
 * @param window Window to query.
 * @param out_left Pointer to receive left border width. Can be NULL.
 * @param out_right Pointer to receive right border width. Can be NULL.
 * @param out_top Pointer to receive top border (title bar) height. Can be NULL.
 * @param out_bottom Pointer to receive bottom border height. Can be NULL.
 * @return true if frame extents were successfully queried, false otherwise.
 *
 * Uses _NET_FRAME_EXTENTS property (EWMH specification).
 * If the property is not available (e.g., window not yet mapped or WM doesn't support it),
 * returns false and sets all outputs to 0.
 */
bool cj_x11_get_frame_extents(Display* dpy, Window window,
                              int32_t* out_left, int32_t* out_right,
                              int32_t* out_top, int32_t* out_bottom) {
  if (out_left) *out_left = 0;
  if (out_right) *out_right = 0;
  if (out_top) *out_top = 0;
  if (out_bottom) *out_bottom = 0;

  Atom frame_extents = XInternAtom(dpy, "_NET_FRAME_EXTENTS", True);
  if (frame_extents == None) {
    return false;  /* Property not supported by WM */
  }

  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char* data = NULL;

  int result = XGetWindowProperty(dpy, window, frame_extents,
                                  0, 4, False, XA_CARDINAL,
                                  &actual_type, &actual_format,
                                  &nitems, &bytes_after, &data);

  if (result == Success && actual_type == XA_CARDINAL && actual_format == 32 && nitems == 4) {
    long* extents = (long*)data;
    if (out_left) *out_left = (int32_t)extents[0];
    if (out_right) *out_right = (int32_t)extents[1];
    if (out_top) *out_top = (int32_t)extents[2];
    if (out_bottom) *out_bottom = (int32_t)extents[3];
    XFree(data);
    return true;
  }

  if (data) XFree(data);
  return false;
}

/* XRandR support - try to include, but handle gracefully if not available */
#if __has_include(<X11/extensions/Xrandr.h>)
#include <X11/extensions/Xrandr.h>
#define HAVE_XRANDR_HEADERS 1
#else
#define HAVE_XRANDR_HEADERS 0
/* Minimal type definitions if headers not available */
typedef unsigned long XID;
typedef XID RROutput;
typedef XID RRCrtc;
typedef XID RRMode;
#define None 0L
#define RR_Connected 0
#endif

/**
 * @brief Structure to cache monitor DPI information
 */
typedef struct MonitorDPI {
  int32_t x, y;           /* Monitor position in screen coordinates */
  uint32_t width, height; /* Monitor resolution */
  float dpi_scale;        /* DPI scale factor (1.0 = 96 DPI) */
  bool valid;             /* True if DPI was successfully calculated */
} MonitorDPI;

static MonitorDPI* monitor_dpis = NULL;
static int monitor_dpi_count = 0;

/**
 * @brief Query all monitors and cache their DPI
 * @param cj_x11_display X11 cj_x11_display
 * @param root Root window
 */
static void refresh_monitor_dpis(Display* dpy, Window root) {
  (void)dpy;  /* May be unused if XRandR not available */
  (void)root;     /* May be unused if XRandR not available */

  /* Free old cache */
  if (monitor_dpis) {
    free(monitor_dpis);
    monitor_dpis = NULL;
    monitor_dpi_count = 0;
  }

#if HAVE_XRANDR_HEADERS
  /* Check if XRandR is available */
  int event_base, error_base;
  if (!XRRQueryExtension(dpy, &event_base, &error_base)) {
    return;  /* XRandR not available */
  }
#else
  return;  /* XRandR headers not available */
#endif

#if HAVE_XRANDR_HEADERS
  /* Get screen resources */
  XRRScreenResources* res = XRRGetScreenResources(dpy, root);
  if (!res) return;
#else
  return;
#endif

#if HAVE_XRANDR_HEADERS
  /* Allocate cache */
  monitor_dpis = calloc(res->noutput, sizeof(MonitorDPI));
  if (!monitor_dpis) {
    XRRFreeScreenResources(res);
    return;
  }

  /* Query each output */
  for (int i = 0; i < res->noutput; i++) {
    XRROutputInfo* output = XRRGetOutputInfo(dpy, res, res->outputs[i]);
    if (!output || output->connection != RR_Connected) {
      if (output) XRRFreeOutputInfo(output);
      continue;
    }

    /* Get CRTC (monitor) info */
    if (output->crtc != None) {
      XRRCrtcInfo* crtc = XRRGetCrtcInfo(dpy, res, output->crtc);
      if (crtc) {
        MonitorDPI* m = &monitor_dpis[monitor_dpi_count];
        m->x = crtc->x;
        m->y = crtc->y;
        m->width = crtc->width;
        m->height = crtc->height;

        /* Calculate DPI if physical size is available */
        if (output->mm_width > 0 && output->mm_height > 0) {
          /* DPI = (pixels / physical_size_inches) * 25.4mm_per_inch */
          float dpi_x = ((float)crtc->width / (output->mm_width / 25.4f));
          float dpi_y = ((float)crtc->height / (output->mm_height / 25.4f));
          /* Use average of X and Y DPI */
          float dpi = (dpi_x + dpi_y) / 2.0f;
          m->dpi_scale = dpi / 96.0f;  /* Convert to scale factor */
          m->valid = true;
        } else {
          /* No physical size - assume 96 DPI (1.0 scale) */
          m->dpi_scale = 1.0f;
          m->valid = false;
        }

        monitor_dpi_count++;
        XRRFreeCrtcInfo(crtc);
      }
    }

    XRRFreeOutputInfo(output);
  }

  XRRFreeScreenResources(res);
#endif
}

/**
 * @brief Get DPI scale for a window based on its position
 * @param cj_x11_display X11 cj_x11_display
 * @param root Root window
 * @param win_x Window X position
 * @param win_y Window Y position
 * @return DPI scale factor (1.0 = 96 DPI)
 */
float cj_window__get_dpi_scale_linux(Display* dpy, Window root, int32_t win_x, int32_t win_y) {
  /* Refresh monitor cache if needed */
  if (!monitor_dpis) {
    refresh_monitor_dpis(dpy, root);
  }

  /* Find which monitor contains the window center */
  int32_t win_center_x = win_x;  /* Window position is top-left, use center for better matching */
  int32_t win_center_y = win_y;

  /* Find matching monitor */
  for (int i = 0; i < monitor_dpi_count; i++) {
    MonitorDPI* m = &monitor_dpis[i];
    if (win_center_x >= m->x && win_center_x < m->x + (int32_t)m->width &&
        win_center_y >= m->y && win_center_y < m->y + (int32_t)m->height) {
      return m->dpi_scale;
    }
  }

  /* No monitor found - default to 1.0 (96 DPI) */
  return 1.0f;
}


/* === The portable seam, X11 side === */

uint64_t cj_plat_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

bool cj_plat_capture_mouse(uintptr_t handle) {
  if (!cj_x11_display || !handle) return false;
  XGrabPointer(cj_x11_display, (Window)handle, False,
      ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
      GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
  return true;
}

bool cj_plat_release_mouse(void) {
  if (!cj_x11_display) return false;
  XUngrabPointer(cj_x11_display, CurrentTime);
  return true;
}

bool cj_plat_query_position(uintptr_t handle, int32_t* out_x, int32_t* out_y) {
  /* X11: the cached CLIENT position is authoritative. XMoveWindow takes
   * client coordinates, and what XGetGeometry reports depends on whether the
   * window manager reparented us, so asking would answer a different
   * question than the one the caller is asking. */
  (void)handle; (void)out_x; (void)out_y;
  return false;
}

bool cj_plat_query_state(uintptr_t handle, cj_window_state_t* out_state) {
  if (!cj_x11_display || !handle || !out_state) return false;
  Window w = (Window)handle;

  Atom wm_state = XInternAtom(cj_x11_display, "_NET_WM_STATE", False);
  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char* prop = NULL;

  if (XGetWindowProperty(cj_x11_display, w, wm_state, 0, 1024, False,
          XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &prop)
      != Success) {
    return false;
  }

  bool maximized = false;
  if (prop && nitems > 0) {
    Atom* atoms = (Atom*)prop;
    Atom max_horz = XInternAtom(cj_x11_display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atom max_vert = XInternAtom(cj_x11_display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    bool has_max_horz = false, has_max_vert = false;
    for (unsigned long i = 0; i < nitems; i++) {
      if (atoms[i] == max_horz) has_max_horz = true;
      if (atoms[i] == max_vert) has_max_vert = true;
    }
    maximized = has_max_horz && has_max_vert;
  }
  if (prop) XFree(prop);

  if (maximized) {
    *out_state = CJ_WINDOW_STATE_MAXIMIZED;
    return true;
  }

  XWindowAttributes attrs;
  if (!XGetWindowAttributes(cj_x11_display, w, &attrs)) return false;
  *out_state = (attrs.map_state == IsUnmapped)
      ? CJ_WINDOW_STATE_MINIMIZED
      : CJ_WINDOW_STATE_NORMAL;
  return true;
}

bool cj_plat_move_window(uintptr_t handle, int32_t x, int32_t y,
    int32_t cur_x, int32_t cur_y) {
  if (!cj_x11_display || !handle) return false;
  /* Moving to where we already are costs a round trip and can draw a
   * spurious ConfigureNotify back. */
  if (x == cur_x && y == cur_y) return false;

  int32_t decor_left = 0, decor_top = 0;
  cj_x11_get_frame_extents(cj_x11_display, (Window)handle,
      &decor_left, NULL, &decor_top, NULL);

  /* Empirically determined: the WM adds (decor - 32) to our coordinates, so
   * subtract that to compensate. Seen under WSLg/XWayland. */
  int32_t move_x = x - (decor_left - 32);
  int32_t move_y = y - (decor_top - 32);

  XMoveWindow(cj_x11_display, (Window)handle, move_x, move_y);
  XFlush(cj_x11_display);
  /* X11 reports the move back as a ConfigureNotify; the caller has to know
   * to ignore it. */
  return true;
}

cj_result_t cj_plat_set_window_state(uintptr_t handle, cj_window_state_t state) {
  if (!cj_x11_display || !handle) return CJ_E_INVALID_ARGUMENT;
  Window w = (Window)handle;
  int screen = DefaultScreen(cj_x11_display);

  Atom wm_state = XInternAtom(cj_x11_display, "_NET_WM_STATE", False);
  Atom max_horz = XInternAtom(cj_x11_display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
  Atom max_vert = XInternAtom(cj_x11_display, "_NET_WM_STATE_MAXIMIZED_VERT", False);

  long action;
  switch (state) {
    case CJ_WINDOW_STATE_NORMAL:    action = 0; break; /* _NET_WM_STATE_REMOVE */
    case CJ_WINDOW_STATE_MAXIMIZED: action = 1; break; /* _NET_WM_STATE_ADD */
    case CJ_WINDOW_STATE_MINIMIZED:
      XIconifyWindow(cj_x11_display, w, screen);
      XFlush(cj_x11_display);
      return CJ_SUCCESS;
    case CJ_WINDOW_STATE_FULLSCREEN:
      return CJ_E_UNSUPPORTED; /* Not implemented yet */
    default:
      return CJ_E_INVALID_ARGUMENT;
  }

  XEvent ev = {0};
  ev.type = ClientMessage;
  ev.xclient.window = w;
  ev.xclient.message_type = wm_state;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = action;
  ev.xclient.data.l[1] = (long)max_horz;
  ev.xclient.data.l[2] = (long)max_vert;
  ev.xclient.data.l[3] = 1; /* Source indication: application */
  ev.xclient.data.l[4] = 0;
  XSendEvent(cj_x11_display, RootWindow(cj_x11_display, screen), False,
      SubstructureNotifyMask | SubstructureRedirectMask, &ev);
  XFlush(cj_x11_display);
  return CJ_SUCCESS;
}

void cj_plat_bind_window_user_data(uintptr_t handle, void* user) {
  /* X11 has no per-window user-data slot of its own. Events carry the XID
   * and the application looks the window up by it. */
  (void)handle; (void)user;
}

void cj_plat_unbind_window_user_data(uintptr_t handle) {
  (void)handle;
}

void cj_plat_destroy_native_window(uintptr_t handle) {
  if (!cj_x11_display || !handle) return;
  XDestroyWindow(cj_x11_display, (Window)handle);
}

bool cj_plat_window_is_alive(uintptr_t handle) {
  /* X11 cannot answer this without a round trip that would raise a BadWindow
   * error on the way, so a non-zero handle is taken as alive. */
  return handle != 0;
}

VkResult cj_plat_create_surface(uintptr_t handle, VkInstance instance,
    VkSurfaceKHR* out_surface) {
  VkXlibSurfaceCreateInfoKHR ci = {0};
  ci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
  ci.dpy = cj_x11_display;
  ci.window = (Window)handle;
  return vkCreateXlibSurfaceKHR(instance, &ci, NULL, out_surface);
}

void cj_plat_create_window(const char* title, int width, int height,
    int32_t x, int32_t y, cj_window_state_t initial_state,
    cj_plat_window_t* out) {
  if (!out || !cj_x11_display) return;
  int screen = DefaultScreen(cj_x11_display);
  Window root = RootWindow(cj_x11_display, screen);

  int win_x = (x == CJ_WINDOW_POSITION_DEFAULT) ? 0 : x;
  int win_y = (y == CJ_WINDOW_POSITION_DEFAULT) ? 0 : y;

  /* Black background to reduce flickering during resize */
  Window w = XCreateSimpleWindow(cj_x11_display, root, win_x, win_y,
      (unsigned)width, (unsigned)height, 0,
      BlackPixel(cj_x11_display, screen), BlackPixel(cj_x11_display, screen));
  if (!w) return;
  out->handle = (uintptr_t)w;

  /* Position hint, if a position was asked for */
  if (x != CJ_WINDOW_POSITION_DEFAULT && y != CJ_WINDOW_POSITION_DEFAULT) {
    XSizeHints* hints = XAllocSizeHints();
    if (hints) {
      hints->flags = USPosition;
      hints->x = x;
      hints->y = y;
      XSetWMNormalHints(cj_x11_display, w, hints);
      XFree(hints);
    }
  }

  Atom wm_state = XInternAtom(cj_x11_display, "_NET_WM_STATE", False);
  Atom max_horz = XInternAtom(cj_x11_display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
  Atom max_vert = XInternAtom(cj_x11_display, "_NET_WM_STATE_MAXIMIZED_VERT", False);

  if (initial_state == CJ_WINDOW_STATE_MAXIMIZED
      && wm_state != None && max_horz != None && max_vert != None) {
    XChangeProperty(cj_x11_display, w, wm_state, XA_ATOM, 32, PropModeReplace,
        (unsigned char*)&max_horz, 1);
    /* Both atoms go on via ClientMessage after mapping, below. */
  }

  XSelectInput(cj_x11_display, w,
      StructureNotifyMask | KeyPressMask | KeyReleaseMask | ExposureMask |
      ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
      EnterWindowMask | LeaveWindowMask | FocusChangeMask | PropertyChangeMask);

  /* Smooth scrolling if XInput2 is there; traditional events if not. */
  cj_x11_select_xinput2_events(w);

  Atom wmDelete = XInternAtom(cj_x11_display, "WM_DELETE_WINDOW", False);
  XStoreName(cj_x11_display, w, title);
  XSetWMProtocols(cj_x11_display, w, &wmDelete, 1);

  /* No background pixmap, so X does not paint one between frames during a
   * resize. */
  XSetWindowBackgroundPixmap(cj_x11_display, w, None);

  XMapWindow(cj_x11_display, w);

  if (initial_state == CJ_WINDOW_STATE_MAXIMIZED) {
    XEvent ev = {0};
    ev.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = wm_state;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 1; /* _NET_WM_STATE_ADD */
    ev.xclient.data.l[1] = (long)max_horz;
    ev.xclient.data.l[2] = (long)max_vert;
    ev.xclient.data.l[3] = 1; /* Source indication: application */
    ev.xclient.data.l[4] = 0;
    XSendEvent(cj_x11_display, root, False,
        SubstructureNotifyMask | SubstructureRedirectMask, &ev);
  } else if (initial_state == CJ_WINDOW_STATE_MINIMIZED) {
    XIconifyWindow(cj_x11_display, w, screen);
  }

  /* CLIENT coordinates, because XMoveWindow takes client coordinates and the
   * library uses them throughout. */
  Window child;
  int client_x, client_y;
  if (XTranslateCoordinates(cj_x11_display, w, root, 0, 0,
          &client_x, &client_y, &child)) {
    out->x = client_x;
    out->y = client_y;
  }

  out->dpi_scale = cj_window__get_dpi_scale_linux(cj_x11_display, root, out->x, out->y);

  XFlush(cj_x11_display);
}

const char* cj_plat_surface_extension_name(void) {
  return VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
}

bool cj_plat_open_display(void) { return cj_x11_open_display(); }
void cj_plat_close_display(void) { cj_x11_close_display(); }
