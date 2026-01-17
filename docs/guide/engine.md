# Engine Architecture

This document explains the CJelly engine architecture, including resource ownership, threading model, signal handling, and the separation between engine and windows.

## Overview

The CJelly engine is designed with a clear separation between:

- **Engine**: Process-wide owner of Vulkan resources (device, queues, allocators, global resources)
- **Windows**: Per-window resources (surfaces, swapchains, per-frame state)

This separation allows:
- Windows to be created and destroyed without affecting the engine
- The engine to operate headless (no windows)
- Multiple windows to share the same engine resources
- Clean resource lifecycle management

## Architecture Diagram

```
+--------------------+        owns         +--------------------------+
|      Engine        |-------------------->|   Global GPU Backend     |
|  (process-wide)    |                     | (Vulkan Instance/Device) |
+--------------------+                     +--------------------------+
        |  \                                           | 
        |   \ has N windows                            | provides
        |    \                                         v
        |     +------------------+        +---------------------------+
        +---->|   Window[ ]      |  --->  |   Swapchain + Surface     |
              +------------------+        +---------------------------+
                         | uses                        ^
                         v                             |
                 +----------------+      reads/writes  |
                 | Render Graph   |<--------------------+
                 +----------------+
```

## Engine Responsibilities

The engine owns and manages:

### Vulkan Resources

- **Vulkan Instance**: Created with validation layers (if enabled)
- **Physical Device**: Selected GPU adapter
- **Logical Device**: Vulkan device with queues
- **Queues**: Graphics, compute, and transfer queues
- **Command Pools**: Global command pools for resource uploads
- **Timeline Semaphores**: Synchronization primitives

### Global Resource Managers

- **Memory Allocator**: VMA-like allocator for GPU memory
- **Bindless Descriptor Heaps**: Global descriptor arrays for:
  - Sampled images
  - Storage images
  - Samplers
  - Uniform/storage buffers
- **Resource Managers**: Textures, buffers, shaders, pipelines, render passes
- **Sampler Cache**: Reusable sampler objects

### Systems (Planned)

- **Asset System**: Background IO threads, transcoding, content-addressed cache, hot-reload
- **Job System**: Worker thread pool, fiber-enabled optional
- **Telemetry**: Frame markers, GPU timestamps, logging, crash capture

## Window Responsibilities

Windows own and manage:

- **OS Surface**: Platform-specific window surface (Win32/X11/Wayland/macOS)
- **Swapchain**: Per-window swapchain and present images
- **Image Views**: Views for swapchain images
- **Depth/Stencil**: Optional depth/stencil attachments
- **Per-Frame Resources**: Command pools, semaphores, fences per frame-in-flight
- **Render Graph**: Reference to the render graph that determines what to draw

**Important:** Windows reference the engine but do not own the device. Windows hold weak references to global resources (pipelines, shaders) where possible.

## Resource Ownership Rules

### Engine Ownership

- The engine owns all global Vulkan resources
- The engine must outlive all windows
- All windows must be destroyed before engine shutdown

### Window Ownership

- Windows own their surfaces and swapchains
- Windows can be destroyed independently
- Destroying a window does not affect the engine or other windows

### Resource Handles (Planned)

- Resources use opaque 64-bit handles: `(index:32 | generation:32)`
- Lookups go through lock-free tables
- Strong refs increase refcount; weak refs do not
- Windows hold weak refs to global resources where possible

## Threading Model

### Main Thread

The main thread is responsible for:

- **Event Processing**: Polling and handling platform events (`cj_poll_events()`)
- **Window Management**: Creating and destroying windows
- **Render Loop**: Coordinating frame callbacks and render graph execution
- **Vulkan Operations**: All Vulkan API calls happen on the main thread

**Why single-threaded Vulkan?**
- Vulkan objects are not thread-safe
- Command buffer recording must happen on a single thread
- Synchronization is handled via semaphores and fences, not threads

### Signal Handler Thread (Windows)

On Windows, console control handlers (`SetConsoleCtrlHandler`) run in a **separate thread** created by Windows:

- Handler runs concurrently with the main loop
- Must only set flags, not destroy resources
- Main loop checks flags and handles cleanup

**Why this matters:**
- Destroying windows from the handler thread causes race conditions
- The main thread may be in the middle of rendering when the handler runs
- Solution: Handler only sets `app->shutdown_requested = 1`

### Signal Handler Context (Linux/Unix)

On Linux/Unix, signal handlers run in the **same thread** but at an arbitrary interruption point:

- Kernel interrupts the main thread (e.g., mid-malloc, mid-Vulkan call)
- Handler executes, then main thread resumes where interrupted
- Only "async-signal-safe" functions can be called (very limited set)
- `malloc`, `free`, `printf`, and ALL Vulkan functions are NOT safe

**Why this matters:**
- Main thread state may be inconsistent when handler runs
- Calling non-safe functions causes undefined behavior
- Solution: Handler only sets `app->shutdown_requested = 1`

## Signal Handling

### Design Principle

**Signal handlers must only set flags, never destroy resources or perform complex operations.**

This applies to both Windows (separate thread) and Unix (interrupted context).

### Implementation

Signal handlers set a shutdown flag:

```c
// Windows: Handler runs in separate thread
static BOOL WINAPI console_ctrl_handler(DWORD dwCtrlType) {
    CJellyApplication* app = cjelly_application_get_current();
    if (app) {
        app->shutdown_requested = 1;  // Only set flag
    }
    return TRUE;
}

// Linux/Unix: Handler interrupts main thread
static void default_signal_handler(int sig) {
    CJellyApplication* app = cjelly_application_get_current();
    if (app) {
        app->shutdown_requested = 1;  // Only set flag
    }
}
```

The main loop checks the flag each iteration:

```c
while (cj_run_once(engine)) {
    // Check shutdown flag
    if (cjelly_application_should_shutdown(app)) {
        break;  // Exit loop gracefully
    }
    // Process events, render frames, etc.
}
```

Cleanup happens in the main thread after the loop exits:

```c
cj_run_with_config(engine, &config);

// Now safe to clean up (we're back in main thread)
cj_window_destroy(window);
cj_engine_wait_idle(engine);
cj_engine_shutdown_device(engine);
cj_engine_shutdown(engine);
```

### Why `volatile sig_atomic_t`?

The shutdown flag uses `volatile sig_atomic_t`:

- **`volatile`**: Prevents compiler from caching the value in a register
- **`sig_atomic_t`**: Guaranteed atomic read/write even when interrupted
- This type is safe to access from signal handlers on all platforms

### Supported Signals

**Windows:**
- `CTRL_C_EVENT`: Ctrl+C
- `CTRL_BREAK_EVENT`: Ctrl+Break
- `CTRL_CLOSE_EVENT`: Console window closed

**Linux/Unix:**
- `SIGTERM`: Termination request
- `SIGINT`: Interrupt (Ctrl+C)
- `SIGHUP`: Hangup
- `SIGQUIT`: Quit

Register handlers with:

```c
cjelly_application_register_signal_handlers(app);
```

## Engine Lifecycle

### Creation

```
[CREATED] --init()--> [READY]
```

**Initialization steps:**

1. Create Vulkan instance (+ debug utils if enabled)
2. Enumerate adapters; pick device (user policy)
3. Create logical device + queues
4. Create global timeline semaphore(s) and command pools for upload
5. Initialize memory allocator and bindless descriptor heaps
6. Start job system + asset system threads (planned)
7. Register default pipelines/shaders if any

### Shutdown

```
[READY] --shutdown()--> [DEAD]
```

**Shutdown order:**

1. Stop accepting new work; flush asset jobs
2. Wait idle on device; destroy pipelines/shaders
3. Destroy global descriptors and allocator (after all windows destroyed)
4. Destroy device, then instance

**Rule:** All windows must be destroyed before `cj_engine_shutdown()`. The engine refuses to shut down if live windows exist.

## Window Lifecycle

### Creation

```
[NEW] -> create_surface -> create_swapchain -> [READY]
```

**Creation steps:**

1. Create OS surface (Win32/X11/Wayland/macOS layer)
2. Query surface capabilities/format; select present mode
3. Create swapchain images, views, depth buffer
4. Allocate per-frame sync objects & command pools
5. Register window with application

### Resize/Recreation

```
[READY] --(resize/out-of-date)--> recreate_swapchain -> [READY]
```

**Recreation steps:**

1. Quiesce in-flight frames targeting the old swapchain
2. Destroy old swapchain-dependent views/attachments
3. Create new swapchain and dependent resources

### Destruction

```
[READY] --destroy()--> release swapchain -> destroy_surface -> [DEAD]
```

**Destruction steps:**

1. Wait device for window's inflight fences
2. Destroy per-window pools, semaphores, fences, depth/stencil, views, swapchain, surface
3. Unregister from application

**Rule:** A window never owns global pipelines/shaders; it may hold weak refs. Swapchain-size-dependent pipelines are owned by the window or its render graph.

## Application Window Tracking

The `CJellyApplication` object maintains:

- **Window List**: Array of active windows
- **Handle Map**: Platform handle → window mapping for event processing

**API:**

```c
uint32_t count = cjelly_application_window_count(app);
void* windows[10];
uint32_t actual = cjelly_application_get_windows(app, windows, 10);
```

**Use cases:**

- Verify windows exist before using them
- Iterate over all windows for cleanup
- Platform event processing (lookup window by handle)

**Automatic Management:**

- Windows are automatically registered on creation
- Windows are automatically unregistered on destruction
- No manual registration required

## Headless Mode

The engine can operate without any windows:

- Create engine and initialize Vulkan
- Don't create any windows
- Use offscreen rendering (render to images instead of swapchains)
- Submit work to queues directly

This is useful for:
- Server-side rendering
- Batch processing
- Testing and validation

## Best Practices

### Engine Management

1. **One engine per process**: Create a single engine and reuse it
2. **Initialize before windows**: Engine must be initialized before creating windows
3. **Destroy windows first**: Always destroy all windows before engine shutdown
4. **Wait for GPU**: Call `cj_engine_wait_idle()` before destroying resources

### Signal Handling

1. **Register handlers early**: Call `cjelly_application_register_signal_handlers()` before the event loop
2. **Never destroy in handlers**: Only set flags in signal handlers
3. **Clean up in main thread**: Perform cleanup after the event loop exits

### Window Management

1. **Check existence**: Verify windows exist before using them after the event loop
2. **Use application list**: Use `cjelly_application_get_windows()` to get current window list
3. **Handle close gracefully**: Use close callbacks to save state before closing

## Summary

The CJelly engine architecture provides:

- **Clear separation**: Engine owns global resources, windows own per-window resources
- **Thread safety**: Single-threaded Vulkan operations with proper signal handling
- **Resource management**: Automatic tracking and cleanup
- **Flexibility**: Headless operation, dynamic window creation/destruction
- **Safety**: Signal handlers only set flags, cleanup happens in main thread

This design ensures reliable resource management while maintaining flexibility for different application types.
