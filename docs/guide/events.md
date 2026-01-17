# Input Events

This document explains how input events are handled in the CJelly framework, including keyboard, mouse, and focus events.

## Overview

CJelly provides a callback-based input system that delivers platform-independent input events to your application. Input events are processed immediately during the event loop and dispatched to registered callbacks.

**Currently Implemented:**
- Keyboard events (key press, release, repeat)
- Mouse events (button press/release, movement, scroll, enter/leave)
- Focus events (window gained/lost focus)

**Planned:**
- Touch events (multi-touch support)
- Gesture recognition
- Text input events

## Event Processing

Input events are processed during the event loop's event polling phase (see [Event Loop System](event-loops.md)). Events are handled immediately with no queuing:

1. **Platform events are polled** from the OS
2. **Events are translated** to platform-independent formats
3. **Callbacks are dispatched** synchronously to registered handlers
4. **Window state is updated** (key state, mouse position, focus state)

This immediate dispatch model ensures low latency and predictable event ordering.

## Keyboard Events

### Event Structure

Keyboard events are delivered via the `cj_key_event_t` structure:

```c
typedef struct cj_key_event {
  cj_keycode_t keycode;     // Platform-independent keycode
  cj_scancode_t scancode;   // Physical key scancode
  cj_key_action_t action;   // DOWN, UP, or REPEAT
  cj_modifiers_t modifiers; // Modifier keys held
  bool is_repeat;           // True if auto-repeat event
} cj_key_event_t;
```

### Keycodes

Keycodes (`cj_keycode_t`) are platform-independent virtual keys that represent the semantic meaning of a key:

- **Letters**: `CJ_KEY_A` through `CJ_KEY_Z`
- **Numbers**: `CJ_KEY_0` through `CJ_KEY_9`
- **Function Keys**: `CJ_KEY_F1` through `CJ_KEY_F12`
- **Navigation**: `CJ_KEY_UP`, `CJ_KEY_DOWN`, `CJ_KEY_LEFT`, `CJ_KEY_RIGHT`, `CJ_KEY_HOME`, `CJ_KEY_END`, `CJ_KEY_PAGE_UP`, `CJ_KEY_PAGE_DOWN`
- **Editing**: `CJ_KEY_BACKSPACE`, `CJ_KEY_DELETE`, `CJ_KEY_INSERT`, `CJ_KEY_ENTER`, `CJ_KEY_TAB`, `CJ_KEY_ESCAPE`
- **Modifiers**: `CJ_KEY_LEFT_SHIFT`, `CJ_KEY_RIGHT_SHIFT`, `CJ_KEY_LEFT_CTRL`, `CJ_KEY_RIGHT_CTRL`, `CJ_KEY_LEFT_ALT`, `CJ_KEY_RIGHT_ALT`, `CJ_KEY_LEFT_META`, `CJ_KEY_RIGHT_META`
- **Symbols**: `CJ_KEY_SPACE`, `CJ_KEY_MINUS`, `CJ_KEY_EQUALS`, `CJ_KEY_BRACKET_LEFT`, `CJ_KEY_BRACKET_RIGHT`, `CJ_KEY_BACKSLASH`, `CJ_KEY_SEMICOLON`, `CJ_KEY_APOSTROPHE`, `CJ_KEY_GRAVE`, `CJ_KEY_COMMA`, `CJ_KEY_PERIOD`, `CJ_KEY_SLASH`
- **Numpad**: `CJ_KEY_NUMPAD_0` through `CJ_KEY_NUMPAD_9`, `CJ_KEY_NUMPAD_ADD`, `CJ_KEY_NUMPAD_SUBTRACT`, `CJ_KEY_NUMPAD_MULTIPLY`, `CJ_KEY_NUMPAD_DIVIDE`, `CJ_KEY_NUMPAD_DECIMAL`, `CJ_KEY_NUMPAD_ENTER`
- **Special**: `CJ_KEY_CAPS_LOCK`, `CJ_KEY_NUM_LOCK`, `CJ_KEY_SCROLL_LOCK`, `CJ_KEY_PRINT_SCREEN`, `CJ_KEY_PAUSE`
- **Unknown**: `CJ_KEY_UNKNOWN` for unmapped keys

**Note:** Keycodes map to English key names. For non-English keyboard layouts, the physical key that produces "Q" on QWERTY may produce "A" on AZERTY, but the keycode will still be `CJ_KEY_Q` (based on physical position). For layout-aware character input, use text input events (planned feature).

### Scancodes

Scancodes (`cj_scancode_t`) are platform-specific physical key identifiers. They represent the physical position of a key on the keyboard, regardless of keyboard layout.

**Use cases:**
- Game controls (WASD movement works regardless of layout)
- Key remapping based on physical position
- Debugging and diagnostics

**Platform differences:**
- **Windows**: Scancodes follow the USB HID standard
- **Linux**: Scancodes are X11 keycodes (may differ from Windows)

### Key Actions

Keys can have three actions:

- **`CJ_KEY_ACTION_DOWN`**: Key was pressed (initial press)
- **`CJ_KEY_ACTION_UP`**: Key was released
- **`CJ_KEY_ACTION_REPEAT`**: Key is being held (auto-repeat)

### Auto-Repeat Detection

The `is_repeat` flag indicates whether an event is an auto-repeat (key held down). This flag is set for both `CJ_KEY_ACTION_DOWN` and `CJ_KEY_ACTION_REPEAT` events when the key is being auto-repeated.

**Platform behavior:**
- **Windows**: Auto-repeat is detected via the repeat flag in `WM_KEYDOWN`
- **Linux**: Auto-repeat is detected by identifying fake `KeyRelease`/`KeyPress` pairs from X11

### Modifiers

Modifier keys are represented as bit flags in `cj_modifiers_t`:

- **`CJ_MOD_SHIFT`**: Shift key held
- **`CJ_MOD_CTRL`**: Control key held
- **`CJ_MOD_ALT`**: Alt key held (Option on macOS)
- **`CJ_MOD_META`**: Windows key held (Cmd on macOS)
- **`CJ_MOD_CAPS`**: Caps Lock active
- **`CJ_MOD_NUM`**: Num Lock active

Modifiers can be combined (e.g., `CJ_MOD_SHIFT | CJ_MOD_CTRL`). The modifier state reflects the keys held **at the time of the event**.

### Registering a Keyboard Callback

```c
void my_key_callback(cj_window_t* window, const cj_key_event_t* event, void* user_data) {
  if (event->action == CJ_KEY_ACTION_DOWN && event->keycode == CJ_KEY_ESCAPE) {
    // Handle Escape key press
  }

  // Convert keycode to string for logging
  const char* key_name = cj_keycode_to_string(event->keycode);
  printf("Key: %s, Action: %s, Modifiers: 0x%x\n",
         key_name,
         event->action == CJ_KEY_ACTION_DOWN ? "DOWN" :
         event->action == CJ_KEY_ACTION_UP ? "UP" : "REPEAT",
         event->modifiers);
}

// Register the callback
cj_window_on_key(window, my_key_callback, NULL);
```

### Key State Polling

You can query whether a key is currently pressed:

```c
bool is_pressed = cj_key_is_pressed(window, CJ_KEY_SPACE);
```

**Important:** Key state is cleared when the window loses focus to prevent "stuck key" issues.

### Keycode to String Conversion

The library provides a utility function to convert keycodes to human-readable strings:

```c
const char* key_name = cj_keycode_to_string(CJ_KEY_F12);  // Returns "F12"
const char* unknown = cj_keycode_to_string(CJ_KEY_UNKNOWN);  // Returns "UNKNOWN"
```

The returned string is statically allocated and should not be freed.

## Mouse Events

### Event Structure

Mouse events are delivered via the `cj_mouse_event_t` structure:

```c
typedef struct cj_mouse_event {
  cj_mouse_event_type_t type;  // Event type (BUTTON_DOWN, MOVE, etc.)
  int32_t x;                   // X position in window coordinates
  int32_t y;                   // Y position in window coordinates
  int32_t dx;                  // Delta X since last move (MOVE events only)
  int32_t dy;                  // Delta Y since last move (MOVE events only)
  float scroll_x;               // Horizontal scroll delta (SCROLL events only)
  float scroll_y;               // Vertical scroll delta (SCROLL events only)
  cj_mouse_button_t button;    // Button involved (BUTTON_DOWN/UP events only)
  cj_modifiers_t modifiers;    // Modifier keys held during event
} cj_mouse_event_t;
```

### Event Types

Mouse events can be one of the following types:

- **`CJ_MOUSE_BUTTON_DOWN`**: Mouse button was pressed
- **`CJ_MOUSE_BUTTON_UP`**: Mouse button was released
- **`CJ_MOUSE_MOVE`**: Cursor moved within the window
- **`CJ_MOUSE_SCROLL`**: Scroll wheel moved (vertical or horizontal)
- **`CJ_MOUSE_ENTER`**: Cursor entered the window
- **`CJ_MOUSE_LEAVE`**: Cursor left the window

### Mouse Buttons

Mouse buttons are identified by `cj_mouse_button_t`:

- **`CJ_MOUSE_BUTTON_LEFT`**: Left mouse button
- **`CJ_MOUSE_BUTTON_MIDDLE`**: Middle mouse button (wheel click)
- **`CJ_MOUSE_BUTTON_RIGHT`**: Right mouse button
- **`CJ_MOUSE_BUTTON_4`**: Extra button 4 (typically "back" button)
- **`CJ_MOUSE_BUTTON_5`**: Extra button 5 (typically "forward" button)

### Mouse Position

Mouse position (`x`, `y`) is in window coordinates:
- **Origin**: Top-left corner of the window's client area
- **X-axis**: Increases to the right
- **Y-axis**: Increases downward
- **Units**: Pixels

For `CJ_MOUSE_MOVE` events, `dx` and `dy` provide the movement delta since the last move event.

### Scroll Events

Scroll events (`CJ_MOUSE_SCROLL`) provide scroll deltas:

- **`scroll_y`**: Vertical scroll (positive = down, negative = up)
- **`scroll_x`**: Horizontal scroll (positive = right, negative = left)

Scroll deltas are typically in "lines" or "pixels" depending on the platform and input device. The exact units may vary, but the sign and relative magnitude are consistent.

**Platform notes:**
- **Windows**: Scroll deltas are typically multiples of `WHEEL_DELTA` (120)
- **Linux**: Scroll deltas may vary by input device and driver

### Registering a Mouse Callback

```c
void my_mouse_callback(cj_window_t* window, const cj_mouse_event_t* event, void* user_data) {
  switch (event->type) {
    case CJ_MOUSE_BUTTON_DOWN:
      printf("Button %d pressed at (%d, %d)\n", event->button, event->x, event->y);
      break;
    case CJ_MOUSE_MOVE:
      printf("Mouse moved to (%d, %d), delta (%d, %d)\n",
             event->x, event->y, event->dx, event->dy);
      break;
    case CJ_MOUSE_SCROLL:
      printf("Scrolled: x=%.2f, y=%.2f\n", event->scroll_x, event->scroll_y);
      break;
    case CJ_MOUSE_ENTER:
      printf("Mouse entered window\n");
      break;
    case CJ_MOUSE_LEAVE:
      printf("Mouse left window\n");
      break;
  }
}

// Register the callback
cj_window_on_mouse(window, my_mouse_callback, NULL);
```

### Mouse State Polling

You can query the current mouse state:

```c
// Get current mouse position
int32_t x, y;
cj_mouse_get_position(window, &x, &y);

// Check if a button is currently pressed
bool left_pressed = cj_mouse_button_is_pressed(window, CJ_MOUSE_BUTTON_LEFT);
```

**Important:** Mouse button state is cleared when the window loses focus to prevent "stuck button" issues.

### Mouse Capture

Mouse capture allows a window to receive mouse events even when the cursor is outside the window bounds. This is useful for:
- Drag operations that extend beyond window boundaries
- First-person camera controls
- Drawing tools that need continuous input

```c
// Capture the mouse (cursor is hidden and locked to window)
cj_window_capture_mouse(window);

// Release mouse capture
cj_window_release_mouse(window);

// Check if mouse is captured
bool is_captured = cj_window_has_mouse_capture(window);
```

**Behavior:**
- When captured, the cursor is typically hidden
- Mouse move events continue to be delivered even when the cursor would be outside the window
- Mouse capture is automatically released when the window loses focus
- Only one window can capture the mouse at a time

## Focus Events

### Event Structure

Focus events are delivered via the `cj_focus_event_t` structure:

```c
typedef struct cj_focus_event {
  cj_focus_action_t action;  // GAINED or LOST
} cj_focus_event_t;
```

### Focus Actions

- **`CJ_FOCUS_GAINED`**: Window received input focus (user clicked on window, or focus was programmatically set)
- **`CJ_FOCUS_LOST`**: Window lost input focus (user clicked on another window, or focus was moved away)

### Registering a Focus Callback

```c
void my_focus_callback(cj_window_t* window, const cj_focus_event_t* event, void* user_data) {
  if (event->action == CJ_FOCUS_GAINED) {
    printf("Window gained focus\n");
  } else {
    printf("Window lost focus\n");
    // Clear any pressed keys/buttons to prevent stuck state
  }
}

// Register the callback
cj_window_on_focus(window, my_focus_callback, NULL);
```

### Focus State Management

When a window loses focus:
- All pressed keys are cleared (prevents "stuck key" issues)
- All pressed mouse buttons are cleared (prevents "stuck button" issues)
- Mouse capture is automatically released

This ensures that when focus returns, the input state is clean and predictable.

## Event Callback Lifecycle

### Registration

Callbacks are registered per-window:

```c
cj_window_on_key(window, key_callback, user_data);
cj_window_on_mouse(window, mouse_callback, user_data);
cj_window_on_focus(window, focus_callback, user_data);
```

### Removal

To remove a callback, pass `NULL` as the callback:

```c
cj_window_on_key(window, NULL, NULL);  // Remove keyboard callback
```

### User Data

The `user_data` pointer is passed to all callback invocations. This allows you to pass context (e.g., application state, window-specific data) to your callbacks without using global variables.

### Callback Invocation

Callbacks are invoked synchronously during event processing:
- Callbacks run on the main thread
- Callbacks are invoked immediately when events occur
- Multiple events may be processed in a single event loop iteration
- Callbacks should be fast (avoid blocking operations)

## Best Practices

### Event Handling

1. **Keep callbacks fast**: Avoid heavy computation or blocking I/O in callbacks
2. **Use state polling for continuous checks**: Don't rely solely on events for state queries
3. **Handle focus loss**: Clear application state when focus is lost
4. **Use mouse capture judiciously**: Only capture when necessary (drag operations, camera controls)

### Key Handling

1. **Use keycodes for semantic actions**: Use `CJ_KEY_ESCAPE` for "quit", not scancodes
2. **Use scancodes for physical actions**: Use scancodes for game controls (WASD movement)
3. **Check modifiers correctly**: Use bitwise operations to check modifier combinations
4. **Handle auto-repeat**: Use `is_repeat` flag to distinguish initial press from repeat

### Mouse Handling

1. **Use position for UI**: Use `x`/`y` for UI element hit testing
2. **Use deltas for movement**: Use `dx`/`dy` for relative movement (camera controls, dragging)
3. **Normalize scroll deltas**: Scroll deltas may vary by platform/device, normalize for consistent behavior
4. **Handle enter/leave**: Use enter/leave events to update UI state (hover effects, cursor changes)

### Focus Handling

1. **Pause on focus loss**: Pause animations, timers, or background processing when focus is lost
2. **Resume on focus gain**: Restore state when focus returns
3. **Clear input state**: The framework clears key/button state automatically, but you may need to clear application-specific state

## Platform Differences

### Windows

- **Scancodes**: Follow USB HID standard
- **Auto-repeat**: Detected via repeat flag in `WM_KEYDOWN`
- **Modifiers**: Caps Lock and Num Lock state extracted from keyboard state
- **Scroll**: Uses `WM_MOUSEWHEEL` and `WM_MOUSEHWHEEL` messages

### Linux (X11)

- **Scancodes**: X11 keycodes (may differ from Windows)
- **Auto-repeat**: Detected by identifying fake `KeyRelease`/`KeyPress` pairs
- **Modifiers**: Caps Lock and Num Lock extracted from event state mask
- **Scroll**: Uses `ButtonPress` events for buttons 4/5 (vertical) and 6/7 (horizontal)
- **XInput2**: Initialized for future touch support, but scroll uses traditional X11 events

### Future: macOS

- **Scancodes**: macOS virtual keycodes
- **Modifiers**: Cmd key mapped to `CJ_MOD_META`

## Summary

The CJelly input system provides:
- **Platform-independent abstractions**: Keycodes and event structures work across platforms
- **Immediate event dispatch**: Low-latency, synchronous callback invocation
- **State tracking**: Per-window key and mouse button state
- **Focus management**: Automatic state clearing on focus loss
- **Mouse capture**: Support for drag operations and camera controls
- **Utility functions**: Keycode-to-string conversion, state polling

This design allows applications to handle input consistently across platforms while maintaining access to platform-specific features (scancodes) when needed.
