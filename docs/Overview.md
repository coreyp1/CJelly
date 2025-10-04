# Overview.md

## CJelly --- Cross-Platform GUI Library (C + Vulkan)

**Status:** Draft 0.1\
**Targets:** Windows (Win32), Linux (X11 and/or Wayland), macOS
(MoltenVK path handled by contributors)\
**Rendering:** Vulkan-first (bindless), configurable CPU fallback\
**Language/Std:** C17\
**License:** MIT (no GPL/AGPL dependencies)

------------------------------------------------------------------------

## 1. Goals

-   **Cross-platform UI toolkit** that renders *everything* itself
    (widgets, text, effects), using only native OS window/input and
    Vulkan for GPU rendering.
-   **Bindless-leaning renderer** for low driver overhead and batched
    draw submission.
-   **Multi-window** support from day one (multiple swapchains,
    independent event loops glued to one process-level scheduler).
-   **Dual API model:** retained-mode widget tree **and** immediate-mode
    facade.
-   **Deterministic CPU fallback** for environments without
    Vulkan-capable GPUs (aiming for visual equivalence; perf is
    configurable).
-   **Strict minimal dependencies:** native window/input APIs + Vulkan
    SDK; ICU permitted. Everything else "from-scratch" over time.
-   **Theming, light/dark**, and **built-in animations** (timeline,
    easing, springs).
-   **Hi-DPI / per-monitor scaling** and dynamic scale-change handling.

### Non-Goals (for v1)

-   Mobile (may be evaluated later).
-   Embedding native OS controls (anti-goal for desktop; potentially
    revisited for mobile).
-   Full accessibility tree in v1 (ship stubs; complete in v2).
-   Shipping an HTML/CSS runtime in v1 (we will plan the architecture so
    a custom parser can be added later).

------------------------------------------------------------------------

## 2. Platform Scope

-   **Windows:** Win32 windowing/input; Vulkan via standard loader.
-   **Linux:** Start with **X11** (existing work). Keep a **Wayland**
    abstraction ready; decide during Alpha whether to switch or maintain
    both.
-   **macOS:** Rendering via Vulkan-on-Metal (MoltenVK). OS integration
    (NSWindow, input, timers) handled by macOS contributors.

------------------------------------------------------------------------

## 3. High-Level Architecture

    +-----------------------------------------------------------+
    |                   Application (User Code)                 |
    |  - Immediate-mode API (optional)                          |
    |  - Retained-mode tree (widgets, state, data binding TBD)  |
    +----------------------↓--------------------↓---------------+
    |                  UI Framework Core                        |
    |  - Event System (capture/bubble)                          |
    |  - Layout (Flex, Grid, Constraints, Absolute)             |
    |  - Styling/Theming + Animations                           |
    |  - Text subsystem (shaping, fallback, bidi via ICU)       |
    |  - Widget library (v1 set)                                |
    +----------------------↓------------------------------------+
    |          Scene Graph / Draw List (retained)               |
    |  - Node graph → Render items (vectors, images, glyphs)    |
    |  - Caching: glyph atlases, RT textures, layer caches      |
    +----------------------↓------------------------------------+
    |            Rendering Backends (pluggable)                 |
    |  Vulkan Renderer (bindless)        |   CPU Fallback       |
    |  - Swapchain(s) per window         |   - Raster pipeline  |
    |  - Descriptor indexing atlases     |   - Same draw ops    |
    |  - Batched pipelines               |   - Configurable QoS |
    +----------------------↓------------------------------------+
    |      Platform Layer (Windowing/Input/Timers/Clipboard)    |
    |  Win32 | X11/Wayland | Cocoa                              |
    +-----------------------------------------------------------+

**Threading model (initial plan):** - **UI thread:** input, tree
mutation, style recompute, layout. - **Render thread:** consumes stable
frames; handles Vulkan/CPU backend. - **Worker pool:** text shaping,
image decode, heavy layout, animations. - Thread-safe widget mutation:
*not guaranteed*; mutations marshaled to UI thread (provide utilities to
post tasks).

------------------------------------------------------------------------

## 4. Rendering Model

-   **Vulkan Baseline:** Aim for 1.2 with **descriptor indexing**
    (bindless). Fall back to non-bindless paths if unavailable.
-   **Pipelines:** Separate pipelines for vector paths, images, text;
    sRGB-aware by default. Color management and HDR are **TBD** (tracked
    in roadmap).
-   **Batching & Resources:**
    -   Big **texture/glyph atlases** with sparse updates.
    -   **Instance buffers** for per-draw data; push constants for tiny
        params.
    -   **Render-to-texture** for complex widgets/effects; cache
        invalidation on style/size changes.
-   **Animations:** frame-tick timeline integrated with the render
    scheduler.

------------------------------------------------------------------------

## 5. CPU Fallback

-   **Objective:** Pixel-for-pixel parity where feasible. If exact
    parity is costly, allow **quality presets** (exact vs approximate
    AA, shadows, blurs).
-   **Scope:** Vectors (fills/strokes), images (sampling, nine-patch),
    text (subpixel/AA), composition (opacity, clipping, layers).
-   **Architecture:** Same draw-list IR feeds a software rasterizer.
    Choose conservative defaults that favor correctness; app can set
    perf caps (target FPS / quality).

------------------------------------------------------------------------

## 6. Eventing & Input

-   **Routing:** capture → target → bubble; compose synthetic events
    (click, double-click, drag, focus, IME).
-   **Devices (MVP):** mouse, keyboard, touch/gestures. Gamepad & tablet
    later.
-   **Timing:** per-window loop with central scheduler (vsync aware
    where possible).
-   **Hi-DPI:** per-monitor DPI awareness, dynamic re-scale events,
    pixel snapping policies to avoid blurry edges.

------------------------------------------------------------------------

## 7. Layout System

-   **Supported in v1:** **Flex**, **Grid**, **Constraints**, and
    **Absolute/Anchor** positioning.
-   **Measurement:** synchronous measure pass; cache with dirty flags;
    support min/max/intrinsic sizes.
-   **Text measurement:** via text subsystem (ICU + custom shaper; no
    external shaping deps beyond ICU).

------------------------------------------------------------------------

## 8. Text & Internationalization

-   **Dependencies:** **ICU allowed** (normalization, bidi, break
    iterators).\
    *Intent is to avoid other external libs; we will build our own text
    shaper and font manager over time.*
-   **Fonts:** system font discovery + app-supplied fonts; fallback
    chain; color/emoji fonts (COLR/CPAL, CBDT/CBLC, SVG) planned.\
-   **Features:** complex shaping (Indic, Arabic), bidi, grapheme/word
    wrap, hyphenation plan (later).\
-   **Atlases:** glyph caching, subpixel variants as needed; atlas
    eviction policy.

------------------------------------------------------------------------

## 9. Theming & Styling

-   **Theme engine:** variables, states, light/dark; animated
    transitions.
-   **Future:** custom **CSS-like** parser (CJellyCSS) and optional HTML
    subset for certain views. (Parser is later; design now to avoid
    painting ourselves into a corner.)

------------------------------------------------------------------------

## 10. Widgets (v1 Set)

-   **Core:** label, image, button, checkbox, radio, slider, textfield,
    textarea, list, tree, table, tabs, menu, dialog, scrollview,
    canvas/drawing area.
-   **Quality bar:** keyboard navigation, focus ring, accessible
    names/roles (stubbed v1; full A11y v2).

------------------------------------------------------------------------

## 11. Error Handling & Logging

-   **API style:** return codes; out-params for details.\
-   **Diagnostics:** pluggable logger with severity (trace...error);
    build-time switch for verbose GPU validation (Vulkan validation
    layers on debug builds).

------------------------------------------------------------------------

## 12. Build, Tooling, Packaging

-   **Build:** **Make** first; Dev Containers recommended (Linux &
    Windows/MSYS2).\
-   **Artifacts:** static and shared libs; samples as separate
    executables.\
-   **SPIR-V:** precompiled and embedded (hot-reload optional later).\
-   **Public ABI:** C-stable, versioned symbols post-beta.

------------------------------------------------------------------------

## 13. Dependencies & Licensing

-   **Allowed (baseline):** Vulkan SDK/loader, platform SDKs, **ICU**.\
-   **Discouraged:** Any copyleft (GPL/AGPL).\
-   **Project license:** **MIT**.

------------------------------------------------------------------------

## 14. Testing & Quality

-   **Headless tests:** offscreen Vulkan **and** CPU raster paths;
    golden image diffs (tolerances & gamma-aware comparison).\
-   **Fuzzing:** parsers (future CSS/HTML), image decoders.\
-   **Perf CI:** frame time and memory budgets tracked on sample scenes.

------------------------------------------------------------------------

## 15. Performance Targets (initial)

-   **Desktop "typical form" scene:** 60 FPS on integrated GPUs; 30--60
    FPS on CPU fallback depending on preset.\
-   **Latency:** input-to-present under \~25 ms on GPU when vsync
    permits.\
-   **Budgets:** working set and VRAM budgets TBD as we profile (track
    per-scene).

------------------------------------------------------------------------

## 16. Extensibility

-   **Plugin ABI:** desired (post-beta). Targets: custom widgets, render
    effects, image decoders, input devices.\
-   **Scripting/bindings:** TBD; keep C ABI "friendly" to FFI (C++, Go,
    Rust, Python), but don't commit to official bindings yet.

------------------------------------------------------------------------

## 17. Open Questions (to be resolved in design docs)

1.  **Wayland strategy**: switch vs dual-backend with X11.\
2.  **Color management/HDR** policy and timing.\
3.  **CPU raster architecture** (tile-based? SIMD strategy? subpixel AA
    policy).\
4.  **Data binding** API and scope.\
5.  **Resource system** (VFS/packfiles) vs host FS long-term.\
6.  **Text shaper detail** and font feature coverage roadmap.\
7.  **Exact parity policy** between GPU and CPU and which effects can
    degrade.\
8.  **Animation tick source** (vsync vs monotonic timer; frame-skip
    rules).\
9.  **Security**: untrusted content rendering requirements (if any).\
10. **macOS specifics**: color spaces, input nuances, lifecycle.

------------------------------------------------------------------------

## 18. Milestones & Bootstrap Plan

**v0.1 (Bootstrap GPU path)** - Win32 + X11 windowing, swapchains, event
loop.\
- Basic retained tree; immediate facade wrapper.\
- Draw ops: rects, images, text (Latin fallback), clipping; glyph
atlas.\
- Animations: time-based opacity/position.\
- Theming: light/dark primitives.\
- Samples: multi-window gallery; text & list demo.\
- Headless Vulkan tests; image diffs.

**v0.2 (CPU fallback + Text internationalization)** - CPU raster with
configurable quality; parity pass for basic widgets.\
- ICU bidi/wrap; initial complex shaping; font fallback.\
- Wayland spike; macOS windowing bootstrap.

**v0.3 (Layout & Effects)** - Grid & Constraints; perf caches (RT
textures).\
- Effects: shadows, rounded clips; better scrolling.\
- A11y stubs wired to OS APIs.

**v1.0 (Stabilization)** - API freeze; docs complete; perf & memory
targets met.\
- Cross-platform parity pass; theming polish; expanded widget set.\
- Accessibility v1 (baseline roles/events on all platforms).

------------------------------------------------------------------------

## 19. Repository & Directory Layout (proposed)

    /docs/
      Overview.md                         ← this file
      /architecture/
        rendering.md
        cpu-fallback.md
        scene-graph.md
        threading.md
        layout.md
        text-subsystem.md
        theming-animations.md
        eventing-input.md
        windowing.md                      (Win32, X11, Wayland, Cocoa)
        resources-and-assets.md
        error-logging.md
      /platforms/
        windows.md
        linux-x11.md
        linux-wayland.md
        macos.md
      /roadmap/
        milestones.md
        risks-and-mitigations.md
      /api/
        api-design-principles.md
        error-codes.md
        versioning-abi.md
      /testing/
        headless-render-tests.md
        perf-ci.md
        fuzzing.md
      /style/
        coding-style.md
        commit-conventions.md
        doc-style.md

    /src/
      /cjelly/
        api/                (public headers)
        core/               (retained tree, events, layout, theme)
        render_vk/          (Vulkan backend)
        render_cpu/         (fallback raster)
        text/               (ICU integration, shaping, fonts)
        platform/
          win32/
          x11/
          wayland/
          cocoa/
        utils/              (allocators, logging, containers)

    /shaders/
      pipelines/            (SPIR-V outputs + sources if kept)
    /assets/                (demo fonts/images; optional)
    /samples/
      widget-gallery/
      text-lab/
      multi-window-demo/
    /tests/
      golden/               (reference images)
    /tools/
      spirv-build/
      gen-headers/

------------------------------------------------------------------------

## 20. Next Steps (immediate)

1.  **Confirm Wayland vs X11** approach for v0.1 (keep X11 now, plan
    Wayland spike in v0.2?).
2.  **Lock Vulkan feature floor** (target 1.2 + descriptor indexing;
    define non-bindless fallback).
3.  **Decide CPU raster strategy** (scanline vs tile-based; pick initial
    AA method).
4.  **Start docs for** `rendering.md`, `windowing.md`, and
    `text-subsystem.md`---these drive API surface.

------------------------------------------------------------------------

*End of Overview.*
