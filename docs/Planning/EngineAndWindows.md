# CJelly Design: Engine vs. Windows Lifecycles

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

### 2.3 Resource handles

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
objects & command pools.

**Resize/recreate path:** - Quiesce in-flight frames targeting the old
swapchain. - Destroy old swapchain-dependent views/attachments. - Create
new swapchain and dependent resources.

**Destroy:** - Wait device for window's inflight fences. - Destroy
per-window pools, semaphores, fences, depth/stencil, views, swapchain,
surface.

> **Rule**: A window **never** owns global pipelines/shaders; it may
> hold weak refs. Swapchain-size-dependent pipelines (e.g.,
> specialization constants) are owned by the window or its render graph.

------------------------------------------------------------------------

(Full document continues with threading model, resource system, API,
resize paths, and example flows...)
