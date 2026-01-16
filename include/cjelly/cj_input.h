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

#ifdef __cplusplus
} /* extern "C" */
#endif
