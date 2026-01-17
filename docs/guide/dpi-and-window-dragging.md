# DPI and Window Dragging

This document explains how DPI (dots per inch) scaling affects window dragging in CJelly.

## Coordinate Spaces

Window dragging uses two coordinate spaces:
1. **Window position**: Where the window is located on screen
2. **Mouse screen coordinates**: Where the mouse cursor is in screen space

For dragging to work correctly, these must be in the **same coordinate space**.

## Windows

### What is "DPI-Aware"?

**DPI awareness is something the application must explicitly declare to Windows** - it's not automatic. The application communicates its DPI awareness to Windows through API calls that must be made before creating any windows.

### Current CJelly Status

**CJelly is DPI-aware (Per-Monitor V2)** - the application declares DPI awareness programmatically at startup using `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` with fallbacks for older Windows versions. This means:

- All window coordinates are in **logical pixels** (scaled by DPI)
- Windows does not apply automatic bitmap scaling
- Each monitor can have different DPI
- The application receives `WM_DPICHANGED` messages when windows move between monitors

### DPI Awareness Implementation

CJelly declares DPI awareness in `cjelly_application_create()` using a three-tier fallback:

1. **Per-Monitor V2** (Windows 10 1703+): `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`
2. **Per-Monitor** (Windows 8.1+): `SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)`
3. **System DPI** (Windows Vista+): `SetProcessDPIAware()`

The DPI awareness mode is determined at runtime based on available APIs.

### Current Implementation

On Windows, the current implementation uses:

- **Window position**: `GetWindowRect()` / `SetWindowPos()` - Returns/expects **logical pixels** (automatic in DPI-aware apps)
- **Mouse screen coordinates**: `ClientToScreen()` - Returns **logical pixels** (automatic in DPI-aware apps)
- **DPI tracking**: `GetDpiForWindow()` is used to get per-window DPI, stored in `window->plat->dpi_scale`
- **DPI changes**: `WM_DPICHANGED` handler updates DPI scale and marks swapchain for recreation when window moves to different monitor

**Result**: Since CJelly is DPI-aware, both window positions and mouse screen coordinates are in logical pixels, so dragging works correctly.

### DPI Scale Factor

The DPI scale factor is stored per-window in `window->plat->dpi_scale`:
- `1.0` = 96 DPI (standard)
- `2.0` = 192 DPI (200% scaling)
- `1.5` = 144 DPI (150% scaling)

This scale factor is used to convert between logical pixels (API coordinates) and physical pixels (rendering):
- **API coordinates** (window position, size, mouse coordinates): Logical pixels
- **Rendering** (swapchain size, framebuffers): Physical pixels (logical * scale)

### Swapchain and Rendering

The swapchain is created using **physical pixels** to match the actual display resolution:
- Window size in logical pixels is converted to physical pixels using `logical_to_physical()`
- This ensures crisp rendering on high-DPI displays
- When DPI changes (via `WM_DPICHANGED`), the swapchain is marked for recreation

### Multi-Monitor Support

When a window moves between monitors with different DPI:
1. Windows sends `WM_DPICHANGED` message
2. The new DPI is extracted from `wParam`
3. The DPI scale is updated in `window->plat->dpi_scale`
4. The window is resized to the suggested rect (maintains physical size)
5. The swapchain is marked for recreation

## Linux/X11

### Current Implementation

On Linux/X11:

- **Window position**: `XTranslateCoordinates()` / `XMoveWindow()` - These work in **physical pixels**
- **Mouse screen coordinates**: `x_root` / `y_root` from X11 events - These are in **physical pixels**
- **DPI detection**: XRandR extension is used to query monitor DPI based on physical dimensions

**Result**: Both window positions and mouse screen coordinates are in physical pixels, so dragging works correctly.

### DPI Detection via XRandR

CJelly uses the XRandR (X Resize and Rotate) extension to detect monitor DPI:

1. **Monitor enumeration**: XRandR is used to query all connected outputs
2. **Physical dimensions**: Each monitor's physical size (`mm_width`, `mm_height`) is queried
3. **DPI calculation**: DPI = (pixels / physical_size_inches) * 25.4mm_per_inch
4. **DPI scale**: Scale factor = DPI / 96.0 (stored in `window->plat->dpi_scale`)

The DPI scale is determined based on which monitor contains the window's position. When a window moves (detected via `ConfigureNotify`), the DPI is re-queried and the swapchain is marked for recreation if the DPI changed.

### DPI on Linux

X11 itself doesn't have built-in DPI scaling at the coordinate level. DPI scaling is typically handled by:
- **Window managers**: Some apply scaling at the WM level
- **Compositors**: Wayland compositors handle DPI scaling
- **Xft.dpi**: X11 font DPI setting (affects fonts, not window coordinates)

### Window Positioning on Linux

X11 window positioning requires careful handling due to how window managers interpret coordinates:

**The challenge:**
- `XMoveWindow()` specifies where to place a window
- Different window managers interpret coordinates differently
- Reparenting WMs create a frame window around the client window
- The relationship between client coordinates and frame coordinates varies

**Current implementation:**

CJelly uses `XMoveWindow()` with coordinate adjustments based on window manager decoration sizes:

1. **Query decoration sizes**: The `_NET_FRAME_EXTENTS` property provides the left, right, top, and bottom decoration sizes
2. **Apply offset**: On WSLg/XWayland, an offset is applied to account for how the compositor interprets coordinates
3. **Cache position**: The cached window position is updated from `ConfigureNotify` events

**WSLg-specific behavior:**

The current implementation was developed and tested on WSL2/WSLg (Windows Subsystem for Linux using XWayland). WSLg has specific behaviors that may differ from native Linux:

- WSLg applies an internal offset to `XMoveWindow` coordinates
- The formula `offset = decoration - 32` compensates for this
- **This may not work correctly on native Linux** - see `docs/Planning/Todo.md`

### Current Limitations

1. **Coordinate space**: Unlike Windows, Linux coordinates remain in physical pixels (no logical pixel abstraction)
2. **Fractional scaling**: Some compositors support fractional scaling (e.g., 1.5x), but this doesn't affect X11 coordinate space
3. **Wayland**: If/when Wayland support is added, coordinate handling may differ
4. **Multi-monitor DPI**: XRandR can detect different DPI per monitor, but coordinates are still physical pixels
5. **Native Linux untested**: Window dragging was developed on WSL2/WSLg and may behave differently on native Linux

## Current Status

✅ **Implemented and working** for:
- Per-monitor DPI awareness on Windows (Per-Monitor V2)
- DPI detection on Linux via XRandR
- Automatic DPI change handling when windows move between monitors
- Swapchain recreation on DPI changes
- Window dragging with correct coordinate space matching

✅ **Coordinate Space Summary**:
- **Windows**: Both window position and mouse screen coordinates are in **logical pixels** (DPI-aware)
- **Linux**: Both window position and mouse screen coordinates are in **physical pixels** (XRandR DPI detection for rendering only)

⚠️ **Future considerations**:
- Wayland support (coordinate handling may differ)
- Fractional scaling on Linux (doesn't affect X11 coordinates, but may affect rendering)

## Implementation Notes

The drag logic in `main.c` uses:
```c
int32_t mouse_delta_x = event->screen_x - state->drag_start_mouse_screen_x;
int32_t mouse_delta_y = event->screen_y - state->drag_start_mouse_screen_y;

int32_t new_x = state->drag_start_window_x + mouse_delta_x;
int32_t new_y = state->drag_start_window_y + mouse_delta_y;
```

This works because:
- Both `screen_x`/`screen_y` and window positions are in the same coordinate space
  - **Windows**: Both are in logical pixels (DPI-aware)
  - **Linux**: Both are in physical pixels (X11 coordinates)
- The delta calculation is coordinate-space independent (it's just a difference)
- The new position is calculated from the original position plus the delta

The key insight is that **both coordinate systems must match**, which they do:
- On Windows, DPI awareness ensures both are logical pixels
- On Linux, X11 ensures both are physical pixels

## API Functions

### Querying DPI

Applications can query a window's DPI scale:

```c
float scale = cj_window_get_dpi_scale(window);
bool is_high_dpi = cj_window_is_high_dpi(window);
```

- `cj_window_get_dpi_scale()`: Returns the DPI scale factor (1.0 = 96 DPI)
- `cj_window_is_high_dpi()`: Returns true if scale > 1.0

### Coordinate Conversion

The library handles coordinate conversion internally:
- **API coordinates** (window position, size, mouse): Logical pixels on Windows, physical pixels on Linux
- **Rendering** (swapchain, framebuffers): Physical pixels (converted from logical using DPI scale)

Applications typically don't need to perform manual coordinate conversion - the library abstracts this away.
