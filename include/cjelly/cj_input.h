/*
 * CJelly — Input handling API
 * Copyright (c) 2025
 *
 * Licensed under the MIT license for prototype purposes.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "cj_macros.h"
#include "cj_types.h"

/** @file cj_input.h
 *  @brief Input event types and structures for keyboard, mouse, touch, and gestures.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Platform-independent keycode (semantic/virtual key). */
typedef enum cj_keycode {
  /* Letters */
  CJ_KEY_A = 0,
  CJ_KEY_B,
  CJ_KEY_C,
  CJ_KEY_D,
  CJ_KEY_E,
  CJ_KEY_F,
  CJ_KEY_G,
  CJ_KEY_H,
  CJ_KEY_I,
  CJ_KEY_J,
  CJ_KEY_K,
  CJ_KEY_L,
  CJ_KEY_M,
  CJ_KEY_N,
  CJ_KEY_O,
  CJ_KEY_P,
  CJ_KEY_Q,
  CJ_KEY_R,
  CJ_KEY_S,
  CJ_KEY_T,
  CJ_KEY_U,
  CJ_KEY_V,
  CJ_KEY_W,
  CJ_KEY_X,
  CJ_KEY_Y,
  CJ_KEY_Z,

  /* Numbers */
  CJ_KEY_0,
  CJ_KEY_1,
  CJ_KEY_2,
  CJ_KEY_3,
  CJ_KEY_4,
  CJ_KEY_5,
  CJ_KEY_6,
  CJ_KEY_7,
  CJ_KEY_8,
  CJ_KEY_9,

  /* Function keys */
  CJ_KEY_F1,
  CJ_KEY_F2,
  CJ_KEY_F3,
  CJ_KEY_F4,
  CJ_KEY_F5,
  CJ_KEY_F6,
  CJ_KEY_F7,
  CJ_KEY_F8,
  CJ_KEY_F9,
  CJ_KEY_F10,
  CJ_KEY_F11,
  CJ_KEY_F12,

  /* Navigation */
  CJ_KEY_UP,
  CJ_KEY_DOWN,
  CJ_KEY_LEFT,
  CJ_KEY_RIGHT,
  CJ_KEY_HOME,
  CJ_KEY_END,
  CJ_KEY_PAGE_UP,
  CJ_KEY_PAGE_DOWN,

  /* Editing */
  CJ_KEY_BACKSPACE,
  CJ_KEY_DELETE,
  CJ_KEY_INSERT,
  CJ_KEY_ENTER,
  CJ_KEY_TAB,
  CJ_KEY_ESCAPE,

  /* Modifiers (can also be keys) */
  CJ_KEY_LEFT_SHIFT,
  CJ_KEY_RIGHT_SHIFT,
  CJ_KEY_LEFT_CTRL,
  CJ_KEY_RIGHT_CTRL,
  CJ_KEY_LEFT_ALT,
  CJ_KEY_RIGHT_ALT,
  CJ_KEY_LEFT_META,  /* Windows key / Cmd key */
  CJ_KEY_RIGHT_META,

  /* Symbols/Punctuation */
  CJ_KEY_SPACE,
  CJ_KEY_MINUS,
  CJ_KEY_EQUALS,
  CJ_KEY_BRACKET_LEFT,
  CJ_KEY_BRACKET_RIGHT,
  CJ_KEY_BACKSLASH,
  CJ_KEY_SEMICOLON,
  CJ_KEY_APOSTROPHE,
  CJ_KEY_GRAVE,  /* Backtick */
  CJ_KEY_COMMA,
  CJ_KEY_PERIOD,
  CJ_KEY_SLASH,

  /* Numpad */
  CJ_KEY_NUMPAD_0,
  CJ_KEY_NUMPAD_1,
  CJ_KEY_NUMPAD_2,
  CJ_KEY_NUMPAD_3,
  CJ_KEY_NUMPAD_4,
  CJ_KEY_NUMPAD_5,
  CJ_KEY_NUMPAD_6,
  CJ_KEY_NUMPAD_7,
  CJ_KEY_NUMPAD_8,
  CJ_KEY_NUMPAD_9,
  CJ_KEY_NUMPAD_ADD,
  CJ_KEY_NUMPAD_SUBTRACT,
  CJ_KEY_NUMPAD_MULTIPLY,
  CJ_KEY_NUMPAD_DIVIDE,
  CJ_KEY_NUMPAD_DECIMAL,
  CJ_KEY_NUMPAD_ENTER,

  /* Special */
  CJ_KEY_CAPS_LOCK,
  CJ_KEY_NUM_LOCK,
  CJ_KEY_SCROLL_LOCK,
  CJ_KEY_PRINT_SCREEN,
  CJ_KEY_PAUSE,

  CJ_KEY_UNKNOWN = -1
} cj_keycode_t;

/** @brief Physical key scancode (platform-specific, but exposed for advanced use). */
typedef int32_t cj_scancode_t;

/** @brief Key action type. */
typedef enum cj_key_action {
  CJ_KEY_ACTION_DOWN = 0,   /**< Key pressed (initial press). */
  CJ_KEY_ACTION_UP,         /**< Key released. */
  CJ_KEY_ACTION_REPEAT      /**< Key held (auto-repeat). */
} cj_key_action_t;

/** @brief Modifier key flags. */
typedef enum cj_modifiers {
  CJ_MOD_NONE  = 0,
  CJ_MOD_SHIFT = (1 << 0),
  CJ_MOD_CTRL  = (1 << 1),
  CJ_MOD_ALT   = (1 << 2),
  CJ_MOD_META  = (1 << 3),  /**< Windows key / Cmd key. */
  CJ_MOD_CAPS  = (1 << 4),  /**< Caps Lock active. */
  CJ_MOD_NUM   = (1 << 5),  /**< Num Lock active. */
} cj_modifiers_t;

/** @brief Keyboard event structure. */
typedef struct cj_key_event {
  cj_keycode_t keycode;     /**< Platform-independent keycode. */
  cj_scancode_t scancode;   /**< Physical key scancode (platform-specific). */
  cj_key_action_t action;   /**< Key action (DOWN, UP, REPEAT). */
  cj_modifiers_t modifiers; /**< Modifier keys held during event. */
  bool is_repeat;           /**< True if this is an auto-repeat event. */
} cj_key_event_t;

/** Convert a keycode to a human-readable string.
 *  @param keycode The keycode to convert.
 *  @return A null-terminated string describing the keycode, or "UNKNOWN" for invalid keycodes.
 *          The returned string is statically allocated and should not be freed.
 */
CJ_API const char* cj_keycode_to_string(cj_keycode_t keycode);

/** @brief Mouse button identifiers. */
typedef enum cj_mouse_button {
  CJ_MOUSE_BUTTON_LEFT = 0,   /**< Left mouse button. */
  CJ_MOUSE_BUTTON_MIDDLE,     /**< Middle mouse button (wheel click). */
  CJ_MOUSE_BUTTON_RIGHT,      /**< Right mouse button. */
  CJ_MOUSE_BUTTON_4,          /**< Extra button 4 (typically back). */
  CJ_MOUSE_BUTTON_5,          /**< Extra button 5 (typically forward). */
} cj_mouse_button_t;

/** @brief Mouse event type. */
typedef enum cj_mouse_event_type {
  CJ_MOUSE_BUTTON_DOWN = 0,   /**< Mouse button pressed. */
  CJ_MOUSE_BUTTON_UP,         /**< Mouse button released. */
  CJ_MOUSE_MOVE,              /**< Cursor moved. */
  CJ_MOUSE_SCROLL,            /**< Scroll wheel moved (vertical/horizontal). */
  CJ_MOUSE_ENTER,             /**< Cursor entered window. */
  CJ_MOUSE_LEAVE,             /**< Cursor left window. */
} cj_mouse_event_type_t;

/** @brief Mouse event structure.
 *
 *  Coordinate Space Notes:
 *  - On Windows (DPI-aware): x, y, screen_x, screen_y are in LOGICAL pixels (matches window position API).
 *  - On Linux: x, y, screen_x, screen_y are in PHYSICAL pixels (X11 always uses physical pixels).
 *  - The _physical fields are ALWAYS in physical pixels on all platforms, for rendering/hit-testing use.
 *
 *  Usage Guidelines:
 *  - Use x, y, screen_x, screen_y for UI logic (matches window position/size API coordinate space).
 *  - Use x_physical, y_physical, screen_x_physical, screen_y_physical when you need actual pixel positions
 *    (e.g., for rendering, pixel-perfect hit testing, or when working with physical pixel buffers).
 */
typedef struct cj_mouse_event {
  cj_mouse_event_type_t type; /**< Type of mouse event. */

  /** Window-relative coordinates (logical pixels on Windows, physical pixels on Linux).
   *  These match the coordinate space used by cj_window_get_position() and cj_window_get_size().
   *  Use these for UI logic that needs to match window position/size API. */
  int32_t x;                  /**< X position in window coordinates (0 = left edge). */
  int32_t y;                  /**< Y position in window coordinates (0 = top edge). */

  /** Screen coordinates (logical pixels on Windows, physical pixels on Linux).
   *  These match the coordinate space used by cj_window_get_position() and cj_window_set_position().
   *  Use these for dragging and other screen-space operations. */
  int32_t screen_x;           /**< X position in screen coordinates (for dragging). */
  int32_t screen_y;           /**< Y position in screen coordinates (for dragging). */

  /** Window-relative coordinates in PHYSICAL pixels (always physical, all platforms).
   *  Use these when you need actual pixel positions for rendering or pixel-perfect hit testing. */
  int32_t x_physical;          /**< X position in window coordinates, physical pixels. */
  int32_t y_physical;          /**< Y position in window coordinates, physical pixels. */

  /** Screen coordinates in PHYSICAL pixels (always physical, all platforms).
   *  Use these when you need actual pixel positions for screen-space operations. */
  int32_t screen_x_physical;  /**< X position in screen coordinates, physical pixels. */
  int32_t screen_y_physical;  /**< Y position in screen coordinates, physical pixels. */

  /** Deltas in window-relative coordinates (same coordinate space as x, y). */
  int32_t dx;                 /**< Delta X since last move (for MOVE events). */
  int32_t dy;                 /**< Delta Y since last move (for MOVE events). */

  float scroll_x;             /**< Horizontal scroll delta (for SCROLL events, positive = right). */
  float scroll_y;             /**< Vertical scroll delta (for SCROLL events, positive = down). */
  cj_mouse_button_t button;   /**< Button involved (for BUTTON_DOWN/UP events). */
  cj_modifiers_t modifiers;   /**< Modifier keys held during event. */
} cj_mouse_event_t;

/** @brief Focus action type. */
typedef enum cj_focus_action {
  CJ_FOCUS_GAINED = 0,        /**< Window received input focus. */
  CJ_FOCUS_LOST,              /**< Window lost input focus. */
} cj_focus_action_t;

/** @brief Focus event structure. */
typedef struct cj_focus_event {
  cj_focus_action_t action;   /**< Focus action (GAINED or LOST). */
} cj_focus_event_t;

#ifdef __cplusplus
} /* extern "C" */
#endif
