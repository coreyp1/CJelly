/**
 * @file input.c
 * @brief Implementation of input-related utility functions.
 */

#include <cjelly/cj_input.h>
#include <cjelly/cj_macros.h>

/** Convert a keycode to a human-readable string.
 *  @param keycode The keycode to convert.
 *  @return A null-terminated string describing the keycode, or "UNKNOWN" for invalid keycodes.
 *          The returned string is statically allocated and should not be freed.
 */
CJ_API const char* cj_keycode_to_string(cj_keycode_t keycode) {
  switch (keycode) {
    /* Letters */
    case CJ_KEY_A: return "A";
    case CJ_KEY_B: return "B";
    case CJ_KEY_C: return "C";
    case CJ_KEY_D: return "D";
    case CJ_KEY_E: return "E";
    case CJ_KEY_F: return "F";
    case CJ_KEY_G: return "G";
    case CJ_KEY_H: return "H";
    case CJ_KEY_I: return "I";
    case CJ_KEY_J: return "J";
    case CJ_KEY_K: return "K";
    case CJ_KEY_L: return "L";
    case CJ_KEY_M: return "M";
    case CJ_KEY_N: return "N";
    case CJ_KEY_O: return "O";
    case CJ_KEY_P: return "P";
    case CJ_KEY_Q: return "Q";
    case CJ_KEY_R: return "R";
    case CJ_KEY_S: return "S";
    case CJ_KEY_T: return "T";
    case CJ_KEY_U: return "U";
    case CJ_KEY_V: return "V";
    case CJ_KEY_W: return "W";
    case CJ_KEY_X: return "X";
    case CJ_KEY_Y: return "Y";
    case CJ_KEY_Z: return "Z";

    /* Numbers */
    case CJ_KEY_0: return "0";
    case CJ_KEY_1: return "1";
    case CJ_KEY_2: return "2";
    case CJ_KEY_3: return "3";
    case CJ_KEY_4: return "4";
    case CJ_KEY_5: return "5";
    case CJ_KEY_6: return "6";
    case CJ_KEY_7: return "7";
    case CJ_KEY_8: return "8";
    case CJ_KEY_9: return "9";

    /* Function keys */
    case CJ_KEY_F1: return "F1";
    case CJ_KEY_F2: return "F2";
    case CJ_KEY_F3: return "F3";
    case CJ_KEY_F4: return "F4";
    case CJ_KEY_F5: return "F5";
    case CJ_KEY_F6: return "F6";
    case CJ_KEY_F7: return "F7";
    case CJ_KEY_F8: return "F8";
    case CJ_KEY_F9: return "F9";
    case CJ_KEY_F10: return "F10";
    case CJ_KEY_F11: return "F11";
    case CJ_KEY_F12: return "F12";

    /* Navigation */
    case CJ_KEY_UP: return "UP";
    case CJ_KEY_DOWN: return "DOWN";
    case CJ_KEY_LEFT: return "LEFT";
    case CJ_KEY_RIGHT: return "RIGHT";
    case CJ_KEY_HOME: return "HOME";
    case CJ_KEY_END: return "END";
    case CJ_KEY_PAGE_UP: return "PAGE_UP";
    case CJ_KEY_PAGE_DOWN: return "PAGE_DOWN";

    /* Editing */
    case CJ_KEY_BACKSPACE: return "BACKSPACE";
    case CJ_KEY_DELETE: return "DELETE";
    case CJ_KEY_INSERT: return "INSERT";
    case CJ_KEY_ENTER: return "ENTER";
    case CJ_KEY_TAB: return "TAB";
    case CJ_KEY_ESCAPE: return "ESCAPE";

    /* Modifiers */
    case CJ_KEY_LEFT_SHIFT: return "LEFT_SHIFT";
    case CJ_KEY_RIGHT_SHIFT: return "RIGHT_SHIFT";
    case CJ_KEY_LEFT_CTRL: return "LEFT_CTRL";
    case CJ_KEY_RIGHT_CTRL: return "RIGHT_CTRL";
    case CJ_KEY_LEFT_ALT: return "LEFT_ALT";
    case CJ_KEY_RIGHT_ALT: return "RIGHT_ALT";
    case CJ_KEY_LEFT_META: return "LEFT_META";
    case CJ_KEY_RIGHT_META: return "RIGHT_META";

    /* Symbols/Punctuation */
    case CJ_KEY_SPACE: return "SPACE";
    case CJ_KEY_MINUS: return "MINUS";
    case CJ_KEY_EQUALS: return "EQUALS";
    case CJ_KEY_BRACKET_LEFT: return "BRACKET_LEFT";
    case CJ_KEY_BRACKET_RIGHT: return "BRACKET_RIGHT";
    case CJ_KEY_BACKSLASH: return "BACKSLASH";
    case CJ_KEY_SEMICOLON: return "SEMICOLON";
    case CJ_KEY_APOSTROPHE: return "APOSTROPHE";
    case CJ_KEY_GRAVE: return "GRAVE";
    case CJ_KEY_COMMA: return "COMMA";
    case CJ_KEY_PERIOD: return "PERIOD";
    case CJ_KEY_SLASH: return "SLASH";

    /* Numpad */
    case CJ_KEY_NUMPAD_0: return "NUMPAD_0";
    case CJ_KEY_NUMPAD_1: return "NUMPAD_1";
    case CJ_KEY_NUMPAD_2: return "NUMPAD_2";
    case CJ_KEY_NUMPAD_3: return "NUMPAD_3";
    case CJ_KEY_NUMPAD_4: return "NUMPAD_4";
    case CJ_KEY_NUMPAD_5: return "NUMPAD_5";
    case CJ_KEY_NUMPAD_6: return "NUMPAD_6";
    case CJ_KEY_NUMPAD_7: return "NUMPAD_7";
    case CJ_KEY_NUMPAD_8: return "NUMPAD_8";
    case CJ_KEY_NUMPAD_9: return "NUMPAD_9";
    case CJ_KEY_NUMPAD_ADD: return "NUMPAD_ADD";
    case CJ_KEY_NUMPAD_SUBTRACT: return "NUMPAD_SUBTRACT";
    case CJ_KEY_NUMPAD_MULTIPLY: return "NUMPAD_MULTIPLY";
    case CJ_KEY_NUMPAD_DIVIDE: return "NUMPAD_DIVIDE";
    case CJ_KEY_NUMPAD_DECIMAL: return "NUMPAD_DECIMAL";
    case CJ_KEY_NUMPAD_ENTER: return "NUMPAD_ENTER";

    /* Special */
    case CJ_KEY_CAPS_LOCK: return "CAPS_LOCK";
    case CJ_KEY_NUM_LOCK: return "NUM_LOCK";
    case CJ_KEY_SCROLL_LOCK: return "SCROLL_LOCK";
    case CJ_KEY_PRINT_SCREEN: return "PRINT_SCREEN";
    case CJ_KEY_PAUSE: return "PAUSE";

    case CJ_KEY_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}
