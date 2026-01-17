# Application Lifecycle

This document explains how to use the CJelly framework from application startup to shutdown, including engine creation, window management, event loop execution, and cleanup.

## Overview

A typical CJelly application follows this lifecycle:

1. **Create Engine**: Initialize the Vulkan engine
2. **Create Application**: Set up application context for window tracking
3. **Create Windows**: Create one or more windows
4. **Register Callbacks**: Set up frame, input, and event callbacks
5. **Run Event Loop**: Start the main event loop
6. **Cleanup**: Destroy windows and engine resources

## Step-by-Step Guide

### 1. Create the Engine

The engine is the central object that manages Vulkan resources (instance, device, queues, etc.):

```c
cj_engine_desc_t desc = {0};
cj_engine_t* engine = cj_engine_create(&desc);
if (!engine) {
    fprintf(stderr, "Failed to create engine\n");
    return EXIT_FAILURE;
}

// Initialize Vulkan (creates instance, device, queues, etc.)
bool use_validation = true;  // Enable validation layers in debug builds
if (!cj_engine_init(engine, use_validation)) {
    fprintf(stderr, "Failed to initialize Vulkan\n");
    cj_engine_shutdown(engine);
    return EXIT_FAILURE;
}

// Set as current engine (required for some operations)
cj_engine_set_current(engine);
```

**Important:** The engine must be initialized before creating windows, as windows need access to the Vulkan device.

### 2. Create the Application

The application object tracks windows and handles signal processing:

```c
CJellyApplication* app = NULL;
cjelly_application_create(&app, "My Application", 1);
if (!app) {
    fprintf(stderr, "Failed to create application\n");
    cj_engine_shutdown(engine);
    return EXIT_FAILURE;
}

cjelly_application_set_current(app);
```

**Note:** The application name and version are used for identification and may be used in platform-specific contexts (e.g., window titles, system integration).

### 3. Create Windows

Windows are created with a descriptor that specifies size, title, and presentation mode:

```c
cj_window_desc_t wdesc = {0};
wdesc.title.ptr = "My Window";
wdesc.title.len = strlen(wdesc.title.ptr);
wdesc.width = 800;
wdesc.height = 600;
wdesc.present_mode = CJ_PRESENT_VSYNC;  // Use VSync (FIFO present mode)

cj_window_t* window = cj_window_create(engine, &wdesc);
if (!window) {
    fprintf(stderr, "Failed to create window\n");
    // Handle error
}
```

**Multiple Windows:** You can create multiple windows, each with its own render graph and callbacks:

```c
cj_window_t* win1 = cj_window_create(engine, &wdesc1);
cj_window_t* win2 = cj_window_create(engine, &wdesc2);
cj_window_t* win3 = cj_window_create(engine, &wdesc3);
```

### 4. Set Up Rendering

Attach a render graph to each window and configure rendering behavior:

```c
// Create a render graph
cj_rgraph_desc_t rgraph_desc = {0};
cj_rgraph_t* graph = cj_rgraph_create(engine, &rgraph_desc);

// Attach to window
cj_window_set_render_graph(window, graph);

// Configure redraw policy and FPS limits
cj_window_set_redraw_policy(window, CJ_REDRAW_ALWAYS);
cj_window_set_max_fps(window, 60);
```

### 5. Register Callbacks

Register callbacks for frame updates, input events, and window events:

```c
// Frame callback (called each frame)
cj_window_on_frame(window, my_frame_callback, user_data);

// Input callbacks
cj_window_on_key(window, my_key_callback, user_data);
cj_window_on_mouse(window, my_mouse_callback, user_data);
cj_window_on_focus(window, my_focus_callback, user_data);

// Window event callbacks
cj_window_on_close(window, my_close_callback, user_data);
cj_window_on_resize(window, my_resize_callback, user_data);
```

See [Input Events](events.md) and [Windowing System](windowing.md) for details on callback usage.

### 6. Register Signal Handlers

Register signal handlers for graceful shutdown (Ctrl+C, SIGTERM, etc.):

```c
cjelly_application_register_signal_handlers(app);
```

**Important:** Signal handlers only set a shutdown flag. The main loop checks this flag and exits gracefully. Window cleanup happens in the main thread after the loop exits. See [Engine Architecture](engine.md) for details.

### 7. Run the Event Loop

Start the main event loop. This processes events, invokes callbacks, and renders frames:

```c
cj_run_config_t config = {0};
config.target_fps = 60;              // Maximum event loop iteration rate
config.vsync = true;                  // Skip sleep when VSync is active
config.run_when_minimized = false;    // Exit when all windows minimized
config.enable_fps_profiling = false;  // Disable FPS statistics output

cj_run_with_config(engine, &config);
```

**Alternative:** Use `cj_run(engine)` for default configuration, or `cj_run_once(engine)` for custom loop control.

The event loop continues until:
- All windows are closed
- `cj_request_stop(engine)` is called
- A signal handler sets the shutdown flag
- All windows are minimized (if `run_when_minimized = false`)

See [Event Loop System](event-loops.md) for detailed configuration options.

### 8. Cleanup

After the event loop exits, clean up resources in reverse order:

```c
// Check which windows still exist (some may have been closed via X button)
uint32_t count = cjelly_application_window_count(app);
void* windows[10];
uint32_t actual = cjelly_application_get_windows(app, windows, count < 10 ? count : 10);

// Destroy remaining windows
for (uint32_t i = 0; i < actual; i++) {
    cj_window_destroy((cj_window_t*)windows[i]);
}

// Destroy render graphs
cj_rgraph_destroy(graph);

// Wait for GPU to finish all work
cj_engine_wait_idle(engine);

// Shutdown device (destroys Vulkan resources)
cj_engine_shutdown_device(engine);

// Shutdown engine (frees engine object)
cj_engine_shutdown(engine);
```

**Important:** Always destroy windows before shutting down the engine. The engine will refuse to shut down if live windows exist.

## Complete Example

Here's a minimal complete example:

```c
#include "cjelly/cj_engine.h"
#include "cjelly/cj_window.h"
#include "cjelly/application.h"

cj_frame_result_t frame_callback(cj_window_t* window, 
                                 const cj_frame_info_t* frame_info,
                                 void* user_data) {
    // Update application state
    // Mark window dirty if content changed
    cj_window_mark_dirty(window);
    return CJ_FRAME_CONTINUE;
}

int main(void) {
    // 1. Create engine
    cj_engine_desc_t desc = {0};
    cj_engine_t* engine = cj_engine_create(&desc);
    if (!engine || !cj_engine_init(engine, false)) {
        return EXIT_FAILURE;
    }
    cj_engine_set_current(engine);

    // 2. Create application
    CJellyApplication* app = NULL;
    cjelly_application_create(&app, "Example", 1);
    cjelly_application_set_current(app);

    // 3. Create window
    cj_window_desc_t wdesc = {0};
    wdesc.title.ptr = "Example Window";
    wdesc.title.len = strlen(wdesc.title.ptr);
    wdesc.width = 800;
    wdesc.height = 600;
    cj_window_t* window = cj_window_create(engine, &wdesc);

    // 4. Set up rendering
    cj_rgraph_desc_t rgraph_desc = {0};
    cj_rgraph_t* graph = cj_rgraph_create(engine, &rgraph_desc);
    cj_window_set_render_graph(window, graph);
    cj_window_set_redraw_policy(window, CJ_REDRAW_ALWAYS);

    // 5. Register callbacks
    cj_window_on_frame(window, frame_callback, NULL);

    // 6. Register signal handlers
    cjelly_application_register_signal_handlers(app);

    // 7. Run event loop
    cj_run(engine);

    // 8. Cleanup
    cj_window_destroy(window);
    cj_rgraph_destroy(graph);
    cj_engine_wait_idle(engine);
    cj_engine_shutdown_device(engine);
    cj_engine_shutdown(engine);

    return 0;
}
```

## Window Lifecycle Management

### Dynamic Window Creation

Windows can be created and destroyed at any time during the event loop:

```c
// Create a new window during the event loop
cj_window_t* new_window = cj_window_create(engine, &wdesc);
cj_window_set_render_graph(new_window, graph);
cj_window_on_frame(new_window, frame_callback, NULL);

// Later, destroy the window
cj_window_destroy(new_window);
```

**Important:** After a window is destroyed, any stored pointers to that window become invalid. Always check if a window still exists before using it:

```c
// Check if window still exists
uint32_t count = cjelly_application_window_count(app);
void* windows[10];
uint32_t actual = cjelly_application_get_windows(app, windows, 10);

bool window_exists = false;
for (uint32_t i = 0; i < actual; i++) {
    if (windows[i] == my_window) {
        window_exists = true;
        break;
    }
}

if (window_exists) {
    // Safe to use my_window
}
```

### Window Closing

Windows can be closed in several ways:

1. **User clicks X button**: Close callback is invoked (can prevent close)
2. **Frame callback returns `CJ_FRAME_CLOSE_WINDOW`**: Window is closed
3. **Application calls `cj_window_destroy()`**: Window is immediately destroyed

See [Windowing System](windowing.md) for details on close callbacks.

## Error Handling

### Engine Creation Failures

If engine creation or initialization fails:

```c
cj_engine_t* engine = cj_engine_create(&desc);
if (!engine) {
    // Engine creation failed (out of memory, etc.)
    return EXIT_FAILURE;
}

if (!cj_engine_init(engine, false)) {
    // Vulkan initialization failed (no compatible device, etc.)
    cj_engine_shutdown(engine);
    return EXIT_FAILURE;
}
```

### Window Creation Failures

If window creation fails:

```c
cj_window_t* window = cj_window_create(engine, &wdesc);
if (!window) {
    // Window creation failed (invalid descriptor, platform error, etc.)
    // Engine is still valid, can create other windows
}
```

### Event Loop Errors

The event loop handles errors internally and continues running. If a window's frame callback or render graph fails, that window is skipped for that frame, but other windows continue to render.

## Best Practices

### Resource Management

1. **Destroy in reverse order**: Windows before engine, render graphs before windows
2. **Check for existence**: Verify windows exist before using them after the event loop
3. **Wait for GPU**: Call `cj_engine_wait_idle()` before destroying resources
4. **One engine per process**: Create a single engine and reuse it for all windows

### Signal Handling

1. **Always register handlers**: Call `cjelly_application_register_signal_handlers()` before running the event loop
2. **Graceful shutdown**: Let the event loop exit naturally, then clean up in the main thread
3. **Save state**: If needed, save application state in a shutdown callback

### Window Management

1. **Track window pointers**: Use the application's window list to verify windows exist
2. **Handle close callbacks**: Use close callbacks to prompt for unsaved changes
3. **Clean up on close**: Release window-specific resources in close callbacks or after destruction

## Summary

The CJelly application lifecycle provides:

- **Simple startup**: Create engine, application, windows, and run
- **Flexible window management**: Create and destroy windows dynamically
- **Graceful shutdown**: Signal handlers and cleanup ensure proper resource release
- **Error resilience**: Failures are isolated and don't crash the entire application

This design allows applications to manage resources safely while maintaining responsive event handling and rendering.
