# Event Loop System

This document explains how the CJelly event loop system works, including event processing, frame timing, FPS control, and integration with window callbacks.

## Overview

CJelly uses a callback-based event loop that processes platform events and invokes window callbacks at controlled intervals. The event loop provides fine-grained control over frame pacing, VSync behavior, and window update rates through configuration options.

## Event Loop Architecture

The event loop (`cj_run_with_config()`) is the central coordinator that:

1. **Processes Platform Events**: Polls and handles OS events (window resize, close, visibility changes, input)
2. **Manages Window Lifecycle**: Tracks window state, handles minimized windows, manages swapchain recreation
3. **Invokes Callbacks**: Calls registered frame callbacks at appropriate intervals
4. **Controls Frame Pacing**: Maintains target FPS through sleep timing and VSync coordination
5. **Profiles Performance**: Optionally tracks and reports detailed timing statistics

## Starting the Event Loop

### Basic Usage

```c
cj_engine_t* engine = cj_engine_create(&desc);
// ... create windows, register callbacks ...
cj_run(engine);  // Uses default configuration
```

### With Configuration

```c
cj_run_config_t config = {
    .target_fps = 60,
    .vsync = true,
    .run_when_minimized = false,
    .enable_fps_profiling = true
};
cj_run_with_config(engine, &config);
```

### Single Iteration

For applications that need custom loop control:

```c
while (cj_run_once(engine)) {
    // Custom logic between iterations
}
```

## Configuration Options

### `target_fps`

Controls the maximum rate at which the event loop iterates.

- **0**: Unlimited (loop runs as fast as possible)
- **> 0**: Target frames per second (loop sleeps to maintain rate)

**Behavior:**
- The loop calculates required sleep time based on actual frame duration
- Sleep time accounts for VSync wait (if VSync is active)
- Per-window FPS limits can further restrict individual window rendering rates

**Example:**
- `target_fps = 30`: Loop iterates at most 30 times per second
- `target_fps = 60`: Loop iterates at most 60 times per second
- `target_fps = 0`: Loop runs at maximum speed (limited by VSync and per-window FPS)

### `vsync`

Controls whether VSync timing is considered when calculating sleep duration.

- **true**: Skip sleep when VSync is active (VSync already provides pacing)
- **false**: Always apply sleep based on target FPS

**How it works:**
- When VSync is active (FIFO present mode), frames are naturally paced by the display
- The loop detects VSync activity by measuring frame times
- If VSync is active and `vsync = true`, sleep is skipped to avoid double-pacing
- If `vsync = false`, sleep is always applied regardless of VSync state

**Note:** This is a timing optimization, not a present mode selector. The actual present mode is determined by the window's swapchain configuration.

### `run_when_minimized`

Controls whether the event loop continues when all windows are minimized.

- **true**: Loop continues running even when all windows are minimized
- **false**: Loop exits when all windows are minimized (default)

**Use cases:**
- **false**: Desktop applications that should pause when minimized
- **true**: Background services or applications that need to continue processing

**Behavior:**
- Minimized windows are skipped during rendering (no callbacks, no GPU work)
- Platform events are still processed (allows windows to be restored)
- Loop exits when `run_when_minimized = false` and all windows are minimized

### `enable_fps_profiling`

Enables detailed FPS and timing statistics output.

- **true**: Print FPS statistics to stdout every second
- **false**: No profiling output (default)

**Output includes:**
- Current FPS (frames per second)
- Frame time statistics (average, min, max)
- Per-segment timing breakdown:
  - Event polling
  - Window list retrieval
  - Minimized state checks
  - Frame begin time
  - Callback execution time
  - Render graph execution time
  - Present time
  - VSync wait time
  - Sleep time
  - Other overhead

## Event Loop Iteration

Each iteration of the event loop performs the following steps:

### 1. Event Polling

Platform-specific events are polled and processed:
- **Window Events**: Resize, close, minimize/restore, expose
- **Input Events**: Keyboard, mouse (future)
- **System Events**: Application shutdown signals

**Processing:**
- Events are handled immediately (no queuing)
- Window state is updated (minimized flags, size changes)
- Callbacks are dispatched (resize callbacks, close callbacks)
- Dirty flags are set automatically (resize, expose events)

### 2. Window Iteration

For each active window:

1. **Check Minimized State**: Skip if minimized (unless `run_when_minimized = true`)

2. **Determine Callback Invocation**:
   - `CJ_REDRAW_ALWAYS`: Callback runs at window's FPS rate
   - `CJ_REDRAW_ON_EVENTS`: Callback runs at global FPS rate
   - `CJ_REDRAW_ON_DIRTY`: Callback runs only when dirty

3. **Check Rendering Need**:
   - Evaluate redraw policy
   - Check dirty flag
   - Check per-window FPS limits
   - Check render reason (forced events bypass FPS limits)

4. **Invoke Frame Callback** (if applicable):
   - Callback can update state, mark window dirty, or skip rendering
   - Callback receives frame information (index, render reason, timing)

5. **Execute Render Graph** (if rendering):
   - Begin frame (acquire swapchain image)
   - Execute render graph (record commands)
   - Present frame (submit to display)

6. **Update State**:
   - Update last render time (for FPS limiting)
   - Clear dirty flag (if policy requires it)
   - Update render reason (reset to TIMER for next frame)

### 3. Frame Pacing

After processing all windows, the loop calculates sleep time:

1. **Measure Frame Duration**: Time since last iteration
2. **Calculate Target Duration**: `1.0 / target_fps` seconds
3. **Check VSync**: If VSync is active and `vsync = true`, skip sleep
4. **Sleep**: Sleep for remaining time to maintain target FPS

**Timing Precision:**
- Uses microsecond-precision timers
- Accounts for actual work done (not just sleep)
- Handles VSync wait time correctly

### 4. Profiling (if enabled)

If `enable_fps_profiling = true`:
- Accumulate timing statistics
- Every second, print summary to stdout
- Reset counters for next interval

## Frame Timing

### Frame Duration Calculation

The event loop tracks frame timing at microsecond precision:

- **Frame Start**: Time when iteration begins
- **Frame End**: Time when iteration completes
- **Frame Duration**: `frame_end - frame_start`
- **Target Duration**: `1,000,000 / target_fps` microseconds

### Sleep Calculation

Sleep time is calculated as:

```
sleep_time = target_duration - actual_duration
```

If `sleep_time < 0`, no sleep occurs (frame took longer than target).

### VSync Detection

VSync is detected by observing frame times:
- If frame times cluster around display refresh rate (e.g., ~16.67ms for 60Hz), VSync is likely active
- VSync wait time is included in frame duration measurement
- When VSync is active, sleep is skipped (if `vsync = true`) to avoid double-pacing

## Integration with Windows

### Window State Tracking

The event loop tracks window state:
- **Minimized**: Windows are skipped during rendering
- **Dirty**: Windows need rendering (based on policy)
- **FPS Limits**: Per-window FPS limits are checked before rendering
- **Render Reason**: Determines if FPS limits should be bypassed

### Callback Invocation

Frame callbacks are invoked based on:
- **Redraw Policy**: Determines when callbacks run
- **Dirty State**: Some policies only invoke when dirty
- **FPS Limits**: Callbacks respect per-window FPS limits (for `CJ_REDRAW_ALWAYS`)

### Render Graph Execution

Render graphs are executed when:
- Window needs rendering (based on policy and dirty state)
- FPS limits allow rendering (or render reason bypasses limits)
- Frame callback returns `CJ_FRAME_CONTINUE` (or window has no callback)

## Stopping the Event Loop

The event loop stops when:

1. **All Windows Closed**: No windows remain
2. **Shutdown Requested**: `cj_request_stop()` was called
3. **Application Shutdown**: Signal handler set shutdown flag
4. **All Windows Minimized**: If `run_when_minimized = false`

### Requesting Stop

Applications can request the loop to stop:

```c
cj_request_stop(engine);
```

This sets a global flag that causes the loop to exit on the next iteration. Useful for:
- Graceful shutdown from user input
- Error conditions that require exit
- Application-level shutdown logic

## Best Practices

### FPS Configuration

- **Games/Animations**: Use `target_fps = 60` or higher
- **UI Applications**: Use `target_fps = 30` or `target_fps = 60`
- **Background Services**: Use `target_fps = 0` (unlimited) or low FPS
- **Battery-Conscious**: Use lower FPS (30 or lower) with per-window limits

### VSync Settings

- **Smooth Animation**: Use `vsync = true` to avoid double-pacing
- **Low Latency**: Use `vsync = false` with `target_fps` matching display refresh rate
- **Power Saving**: Use `vsync = true` to let display control pacing

### Minimized Behavior

- **Desktop Apps**: Use `run_when_minimized = false` (pause when minimized)
- **Background Services**: Use `run_when_minimized = true` (continue processing)
- **Games**: Use `run_when_minimized = false` (pause game when minimized)

### Profiling

- **Development**: Enable `enable_fps_profiling = true` to monitor performance
- **Release**: Disable `enable_fps_profiling = false` to avoid stdout overhead
- **Debugging**: Use profiling to identify performance bottlenecks

### Per-Window FPS Limits

Combine global FPS with per-window limits:
- Set global FPS to maximum needed by any window
- Set per-window FPS limits for windows that need lower rates
- This allows different update rates for different content types

## Performance Considerations

### Event Polling

- Event polling is fast (typically < 1ms)
- Platform-specific optimizations minimize overhead
- Events are processed immediately (no queuing delay)

### Window Iteration

- Window list is retrieved efficiently (stack allocation for small counts)
- Minimized checks use cached state (no OS polling)
- FPS limit checks use microsecond-precision timers

### Frame Pacing

- Sleep uses platform-agnostic high-resolution timers
- VSync detection avoids unnecessary sleep
- Frame timing accounts for all work done (not just rendering)

### Profiling Overhead

- Profiling adds minimal overhead (< 0.1ms per frame)
- Statistics are accumulated and printed once per second
- Can be disabled in release builds for zero overhead

## Summary

The CJelly event loop provides:
- **Flexible Configuration**: Control FPS, VSync behavior, minimized handling, profiling
- **Efficient Processing**: Minimal overhead, optimized event handling, smart frame pacing
- **Window Integration**: Seamless coordination with window callbacks and render graphs
- **Performance Monitoring**: Optional detailed timing statistics
- **Graceful Shutdown**: Multiple ways to stop the loop cleanly

This design allows applications to optimize for their specific needs while maintaining responsive event handling and efficient resource usage.
