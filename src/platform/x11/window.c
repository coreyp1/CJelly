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

#include <ghoti.io/cjelly/platform_internal.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
