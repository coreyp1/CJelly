# Window Rendering System

This document explains how the CJelly windowing and rendering system works, including frame callbacks, redraw policies, FPS limiting, and the event loop integration.

## Overview

CJelly uses a callback-based event loop where windows can register frame callbacks that are invoked at regular intervals. The system provides fine-grained control over when windows render through **redraw policies** and **per-window FPS limits**, allowing applications to optimize GPU usage based on content type.

## Window Lifecycle

### Creation

When a window is created:
1. The window is initialized with a default redraw policy (`CJ_REDRAW_ON_EVENTS`)
2. The window is marked as **dirty** (needs initial render)
3. The window's render reason is set to `CJ_RENDER_REASON_FORCED` (bypasses FPS limits for initial render)

### Rendering

Windows render through the main event loop, which:
1. Polls platform events (resize, visibility changes, etc.)
2. For each window, checks if it needs to render based on:
   - Redraw policy
   - Dirty flag state
   - Per-window FPS limits
   - Render reason (forced events bypass FPS limits)
3. Invokes frame callbacks (if registered)
4. Executes the render graph
5. Presents the frame to the display

## Redraw Policies

Each window has a **redraw policy** that determines when it should render:

### `CJ_REDRAW_ALWAYS`

The window renders every frame, subject to per-window FPS limits. This is ideal for:
- Games and real-time animations
- Continuously updating visualizations
- Any content that needs smooth, frame-by-frame updates

**Behavior:**
- Frame callback is invoked at the window's FPS rate (or global FPS if no limit set)
- Window always needs redraw (ignores dirty flag)
- Dirty flag is cleared after each render to reset render reason to `TIMER`

### `CJ_REDRAW_ON_DIRTY`

The window only renders when explicitly marked as dirty. This is ideal for:
- Static content (images, text that doesn't change)
- Content that updates infrequently
- UI elements that only change on user interaction

**Behavior:**
- Frame callback is only invoked when window is dirty
- Window must be explicitly marked dirty via `cj_window_mark_dirty()` to trigger rendering
- Dirty flag is cleared after successful render
- Automatically marked dirty on: creation, resize, visibility restore, swapchain recreation

### `CJ_REDRAW_ON_EVENTS`

The window renders when dirty OR when events occur. This is ideal for:
- Content that updates based on timers or external events
- Hybrid windows (mostly static, but occasionally animated)
- Applications that want automatic redraws on important events

**Behavior:**
- Frame callback is always invoked (at global FPS rate) so it can check time/state and mark dirty if needed
- Rendering only occurs when window is dirty
- Dirty flag is cleared after successful render
- Automatically marked dirty on: creation, resize, visibility restore, swapchain recreation

## FPS Limiting

CJelly provides two levels of FPS control:

### Global FPS Limit

Set via `cj_run_config_t.target_fps` when starting the event loop. This controls:
- The rate at which the event loop iterates
- The maximum rate at which all windows can render (unless overridden per-window)
- Frame pacing and sleep timing

### Per-Window FPS Limits

Set via `cj_window_set_max_fps()`. This allows individual windows to render at different rates:
- A game window might render at 60 FPS
- A UI overlay might render at 30 FPS
- A static content window might render at 10 FPS or only when dirty

**How it works:**
- Each window tracks its last render time
- Before rendering, the system checks if enough time has passed since the last render
- If not enough time has passed, rendering is skipped (but callbacks may still be invoked for `CJ_REDRAW_ON_EVENTS` windows)

**Interaction with redraw policies:**
- For `CJ_REDRAW_ALWAYS`: Both callback and rendering respect the FPS limit
- For `CJ_REDRAW_ON_EVENTS`: Callback runs at global FPS, but rendering respects per-window FPS limit
- For `CJ_REDRAW_ON_DIRTY`: FPS limit only applies when window is dirty

## Render Reasons

Each frame has a **render reason** that indicates why it's being rendered:

- `CJ_RENDER_REASON_TIMER`: Regular frame update (subject to FPS limits)
- `CJ_RENDER_REASON_RESIZE`: Window was resized (bypasses FPS limits)
- `CJ_RENDER_REASON_EXPOSE`: Window was exposed/shown (bypasses FPS limits)
- `CJ_RENDER_REASON_FORCED`: Explicitly marked dirty by application (bypasses FPS limits)
- `CJ_RENDER_REASON_SWAPCHAIN_RECREATE`: Swapchain was recreated (bypasses FPS limits)

**Why this matters:**
- Forced events (resize, expose, etc.) always render immediately, regardless of FPS limits
- Timer-based renders respect FPS limits
- Frame callbacks receive the render reason via `cj_frame_info_t.render_reason` to adjust behavior

## Dirty Flag Management

The dirty flag (`needsRedraw`) tracks whether a window's content has changed and needs rendering.

### Automatic Dirty Marking

The system automatically marks windows dirty when:
- Window is created (initial render needed)
- Window is resized (swapchain recreation + content refresh)
- Window is restored from minimized (visibility change)
- Swapchain is recreated (Vulkan resource refresh)

### Manual Dirty Marking

Applications can explicitly control dirty state:
- `cj_window_mark_dirty()`: Mark window as needing redraw
- `cj_window_clear_dirty()`: Clear dirty flag (window won't render until marked dirty again)
- `cj_window_mark_dirty_with_reason()`: Mark dirty with specific render reason

### Dirty Flag Clearing

The dirty flag is automatically cleared:
- After successful frame render (for `CJ_REDRAW_ON_DIRTY` and `CJ_REDRAW_ON_EVENTS` policies)
- When frame callback returns `CJ_FRAME_SKIP` (optional optimization)

For `CJ_REDRAW_ALWAYS` windows, the dirty flag is also cleared after render to reset the render reason to `TIMER` (ensuring FPS limits are respected on subsequent frames).

## Frame Callbacks

Frame callbacks are user-provided functions that are invoked during the rendering pipeline:

```c
typedef cj_frame_result_t (*cj_window_frame_callback_t)(
    cj_window_t* window,
    const cj_frame_info_t* frame_info,
    void* user_data
);
```

**When callbacks are invoked:**
- `CJ_REDRAW_ALWAYS`: At the window's FPS rate (or global FPS if no limit)
- `CJ_REDRAW_ON_EVENTS`: At global FPS rate (so callback can check time/state and mark dirty)
- `CJ_REDRAW_ON_DIRTY`: Only when window is dirty

**Frame information:**
- `frame_index`: Monotonically increasing frame number
- `delta_seconds`: Time since last frame (currently stubbed)
- `render_reason`: Why this frame is being rendered

**Return values:**
- `CJ_FRAME_CONTINUE`: Continue rendering (execute render graph and present)
- `CJ_FRAME_SKIP`: Skip rendering for this frame (clears dirty flag if applicable)
- `CJ_FRAME_CLOSE_WINDOW`: Close the window
- `CJ_FRAME_STOP_LOOP`: Stop the event loop

## Event Loop Integration

The event loop (`cj_run_with_config()`) orchestrates everything:

1. **Event Polling**: Processes platform events (resize, visibility, input)
2. **Window Iteration**: For each window:
   - Checks if callback should be invoked (based on policy)
   - Checks if window needs rendering (based on policy, dirty flag, FPS limits)
   - Invokes frame callback (if applicable)
   - Executes render graph (if rendering)
   - Presents frame (if rendering)
3. **Frame Pacing**: Sleeps to maintain target FPS
4. **Profiling**: Tracks timing statistics (if enabled)

**Key behaviors:**
- Minimized windows are skipped (unless `run_when_minimized` is true)
- Forced events (resize, expose) bypass FPS limits
- VSync is handled automatically (FIFO present mode)
- Global FPS limit applies to event loop iteration rate

## Best Practices

### Choosing a Redraw Policy

- **Static content**: Use `CJ_REDRAW_ON_DIRTY`, mark dirty only when content changes
- **Animated content**: Use `CJ_REDRAW_ALWAYS` with appropriate FPS limit
- **Timer-based updates**: Use `CJ_REDRAW_ON_EVENTS`, check time in callback and mark dirty when needed
- **Hybrid content**: Use `CJ_REDRAW_ON_EVENTS`, mark dirty selectively

### FPS Limits

- Set per-window FPS limits based on content requirements
- Lower FPS for UI elements that don't need smooth animation
- Higher FPS for game/animation windows
- Use 0 (unlimited) for windows that should render at maximum rate

### Dirty Flag Management

- For `CJ_REDRAW_ON_DIRTY`: Always mark dirty when content changes
- For `CJ_REDRAW_ON_EVENTS`: Mark dirty in callback when timer/state indicates update needed
- For `CJ_REDRAW_ALWAYS`: No need to manage dirty flag (system handles it)

### Performance Considerations

- `CJ_REDRAW_ON_DIRTY` minimizes GPU usage for static content
- `CJ_REDRAW_ON_EVENTS` allows callbacks to run at high rate while rendering only when needed
- `CJ_REDRAW_ALWAYS` with FPS limits provides smooth animation without excessive rendering
- Per-window FPS limits allow different update rates for different content types

## Summary

The CJelly windowing system provides flexible control over rendering through:
- **Redraw policies** that determine when windows should render
- **Per-window FPS limits** that control rendering rate
- **Dirty flag tracking** that minimizes unnecessary rendering
- **Render reasons** that ensure important events always render immediately
- **Frame callbacks** that allow applications to update state and control rendering

This design allows applications to optimize GPU usage while maintaining responsive rendering for important events like resize and visibility changes.
