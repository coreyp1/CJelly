# CJelly Design: Engine vs. Windows Lifecycles

> **Goal**: cleanly separate the **Engine** (device, resources, jobs,
> asset pipeline) from **Windows** (OS surfaces, swapchains, input), so
> that windows can come and go without destabilizing the engine, and the
> engine can operate headless. All APIs are C-first.

------------------------------------------------------------------------

## 1) High-level view

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

-   **Engine**: single logical owner of device, queues, allocators,
    descriptor heap, asset DB, job system, telemetry, and global caches.
-   **Window**: thin object that owns OS surface + swapchain +
    per-window frame state and attaches a render graph. Windows are
    disposable.
-   **Headless mode**: zero windows, still able to record/submit
    offscreen work.

------------------------------------------------------------------------

## 2) Object model & ownership

### 2.1 Engine

-   **Singleton-per-process** (by policy, not enforced): `cj_engine_t*`
    returned by `cj_engine_create()`.
-   Owns **Vulkan instance**, selected **physical device**, **logical
    device**, **queues** (graphics, compute, transfer), **timeline
    semaphores**, and a **memory allocator** (VMA-like).
-   Owns **Bindless descriptor heap**: global descriptor arrays for
    sampled images, storage images, samplers, uniform/storage buffers.
-   Owns **Resource Managers**: textures, buffers, shaders, pipelines,
    render passes, sampler cache.
-   Owns **Asset System**: background IO threads, transcoding,
    content-addressed cache, hot-reload.
-   Owns **Job System**: worker thread pool, fiber-enabled optional.
-   Owns **Telemetry**: frame markers, GPU timestamps, logging, crash
    capture.

### 2.2 Window

-   `cj_window_t` references the engine but **does not own** the device.
-   Owns **OS surface** and **swapchain**, **per-frame images** &
    **image views**, **depth/stencil** target if needed.
-   Owns a small **per-window descriptor pool** for transient
    descriptors (optional; most descriptors are bindless globals).
-   Owns **Frame N staging**: per-window command pools (record), fences,
    and semaphores.
-   Holds a **Render Graph** instance (see §4) that determines what to
    draw.

### 2.3 Application Window Tracking

-   `CJellyApplication` maintains a list of active windows and a
    handle-to-window map for platform lookups.
-   Windows are automatically registered on creation and unregistered
    on destruction.
-   `cjelly_application_window_count()` and `cjelly_application_get_windows()`
    provide safe access to the current window list.

### 2.4 Resource handles

-   All resources are **opaque handles**: 64-bit with
    `(index:32 | generation:32)`.
-   Lookups go through lock-free tables; debug builds also maintain name
    → handle maps.
-   **Strong refs** increase refcount; **weak refs** do not. Windows
    hold only weak refs to global resources where possible.

------------------------------------------------------------------------

## 3) Lifecycles & state machines

### 3.1 Engine lifecycle

    [CREATED] --init()--> [READY] --shutdown()--> [DEAD]

**init() steps:** 1. Create Vulkan instance (+ debug utils if enabled).
2. Enumerate adapters; pick device (user policy). Create logical
device + queues. 3. Create global timeline semaphore(s) and command
pools for upload. 4. Initialize memory allocator and bindless descriptor
heaps. 5. Start job system + asset system threads. 6. Register default
pipelines/shaders if any.

**shutdown() order:** 1. Stop accepting new work; flush asset jobs. 2.
Wait idle on device; destroy pipelines/shaders. 3. Destroy global
descriptors and allocator after all windows destroyed. 4. Destroy
device, then instance.

> **Rule**: **All windows must be destroyed before
> `cj_engine_shutdown()`**. The engine refuses to shut down if live
> windows exist.

### 3.2 Window lifecycle

    [NEW] -> create_surface -> create_swapchain ->
    [READY] --(resize/out-of-date)--> recreate_swapchain -> [READY]
    [READY] --destroy()--> release swapchain -> destroy_surface -> [DEAD]

**Create:** 1. Create OS surface (Win32/X11/Wayland/macOS layer). 2.
Query surface capabilities/format; select present mode. 3. Create
swapchain images, views, depth buffer. 4. Allocate per-frame sync
objects & command pools. 5. Register window with application.

**Resize/recreate path:** - Quiesce in-flight frames targeting the old
swapchain. - Destroy old swapchain-dependent views/attachments. - Create
new swapchain and dependent resources.

**Destroy:** - Wait device for window's inflight fences. - Destroy
per-window pools, semaphores, fences, depth/stencil, views, swapchain,
surface. - Unregister from application.

> **Rule**: A window **never** owns global pipelines/shaders; it may
> hold weak refs. Swapchain-size-dependent pipelines (e.g.,
> specialization constants) are owned by the window or its render graph.

------------------------------------------------------------------------

## 4) Window closing & signal handling

### 4.1 User-initiated close (clicking X button)

**Windows:**
1. `WM_CLOSE` message is sent to `CjWndProc`
2. Window is looked up via `cjelly_application_find_window_by_handle()`
3. Close callback is invoked (if registered) - can prevent close
4. If allowed, `cj_window_destroy()` is called
5. Returns 0 to indicate message was handled

**Linux (X11):**
1. `ClientMessage` with `WM_DELETE_WINDOW` is received in `processWindowEvents()`
2. Window is looked up via application
3. Close callback is invoked
4. If allowed, `cj_window_close_with_callback()` destroys the window

### 4.2 Signal handling (Ctrl+C, SIGTERM, etc.)

**Critical design principle**: Signal handlers run in a separate context
(different thread on Windows, async signal context on Unix) and must not
perform complex operations like destroying windows or freeing memory.

**Implementation:**
- Signal handlers **only** set `app->shutdown_requested = 1`
- The main loop checks `cjelly_application_should_shutdown()` and exits gracefully
- Window cleanup happens in the main thread after the loop exits

**Why this matters:**
- On Windows, `SetConsoleCtrlHandler` callbacks run in a **separate thread**
- On Unix, signal handlers have strict limitations on safe functions
- Destroying windows from signal handlers causes race conditions with the main loop

### 4.3 `cj_window_destroy()` - the single cleanup path

All window destruction flows through `cj_window_destroy()`:

1. Guard against null or double-destruction (`is_destroyed` flag)
2. Mark window as destroyed immediately
3. Unregister from application (before freeing resources)
4. Wait for GPU idle (`vkDeviceWaitIdle`)
5. Clean up Vulkan resources (semaphores, fences, command buffers,
   framebuffers, image views, swapchain, surface)
6. Destroy platform window (`DestroyWindow` on Windows, `XDestroyWindow` on Linux)
7. Free the `cj_window_t` structure

### 4.4 Main loop considerations

When windows can be closed dynamically, the main loop must:

1. **Check for shutdown** after processing events
2. **Verify windows still exist** before using window-specific resources
3. **Handle dangling pointers** - after a window is destroyed via X button,
   stored pointers to that window are invalid

Example pattern:
```c
// Get current window list
uint32_t count = cjelly_application_window_count(app);
void* windows[10];
uint32_t actual = cjelly_application_get_windows(app, windows, count);

// Check if specific window still exists before using its resources
bool win1_exists = false;
for (uint32_t i = 0; i < actual; i++) {
    if (windows[i] == win1) win1_exists = true;
}

if (win1_exists) {
    // Safe to use win1's resources
}
```

------------------------------------------------------------------------

## 5) Threading model

### 5.1 Main thread responsibilities

- Event processing (`cj_poll_events()`)
- Window creation and destruction
- Render loop coordination

### 5.2 Signal handler thread (Windows only)

- Console control handlers run in a separate thread
- Must only set flags, not destroy resources
- Main loop checks flags and handles cleanup

### 5.3 GPU work submission

- All Vulkan operations happen on the main thread
- `vkDeviceWaitIdle()` is called before destroying window resources

------------------------------------------------------------------------

## 6) API summary

### Engine

```c
cj_engine_t* cj_engine_create(void);
void cj_engine_shutdown(cj_engine_t* engine);
void cj_engine_wait_idle(cj_engine_t* engine);
```

### Window

```c
cj_window_t* cj_window_create(cj_engine_t* engine, const cj_window_desc_t* desc);
void cj_window_destroy(cj_window_t* win);
cj_result_t cj_window_begin_frame(cj_window_t* win, cj_frame_info_t* out);
cj_result_t cj_window_execute(cj_window_t* win);
cj_result_t cj_window_present(cj_window_t* win);
void cj_window_on_close(cj_window_t* win, cj_window_close_callback_t cb, void* user_data);
```

### Application

```c
CJellyApplication* cjelly_application_get_current(void);
uint32_t cjelly_application_window_count(CJellyApplication* app);
uint32_t cjelly_application_get_windows(CJellyApplication* app, void** out, uint32_t max);
bool cjelly_application_should_shutdown(const CJellyApplication* app);
void cjelly_application_register_signal_handlers(CJellyApplication* app);
```

### Event processing

```c
void cj_poll_events(void);  // Platform-specific message pump
```
