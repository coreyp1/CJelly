# Drawing a model

CJelly can load a Wavefront OBJ file and render it in a window. The demo's
fourth window does exactly that:

```bash
make demo                          # the checked-in cube
./build/linux/release/apps/main path/to/model.obj
```

In code it is one call:

```c
cj_rgraph_t * graph = cj_rgraph_create(engine, &desc);
cj_window_set_render_graph(window, graph);
cj_rgraph_add_model_node(graph, "model", "teapot.obj");
```

The node loads the file, uploads it, frames a camera on it, and turns it
slowly about its vertical axis.

## Where the parsing happens

Not here. OBJ and MTL parsing is
[ghoti.io-model](https://github.com/Ghoti-io/model), a separate library with
its own tests and fuzzers. What CJelly adds is the translation from what an
OBJ file describes to what a GPU can draw, in
`ghoti.io/cjelly/format/3d/mesh.h`:

- **Triangulation.** A face of any number of corners becomes a fan of
  triangles. Correct for convex faces, which is what exporters emit.
- **Normals, when the file has none.** Area-weighted and averaged per
  position. Worth knowing: without this a file with no `vn` lines - the
  Stanford bunny, for one - renders unlit and therefore black, which looks
  like a pipeline fault rather than missing data.
- **A range check.** The OBJ parser does not reject an index naming a vertex
  that does not exist, because readers differ on how to treat one. The mesh
  builder drops those faces and reports how many, rather than reading past the
  end of the position array.
- **A flipped V coordinate**, because OBJ measures from the bottom of the
  image and Vulkan samples from the top.

None of that needs a GPU, so all of it is tested: `tests/test_mesh.cpp`.

## Why the model renders into its own target

The engine has a single render pass with one colour attachment and no depth
buffer, because until now everything CJelly drew was a flat quad in a fixed
order. Geometry needs depth testing.

Adding a depth attachment to the shared render pass would mean touching every
pipeline in the engine - each would need a depth-stencil state it does not
have today - and a mistake in any one of them breaks a window that currently
works. So the model node renders into an offscreen colour-and-depth target
with a render pass of its own, and composites the result into the window as a
textured quad.

That is purely additive: nothing that already renders changes. The costs are
one full-screen blit per frame, and an offscreen target of a fixed 1024×1024
rather than the window's size. Moving to a shared depth attachment would
remove both, and is the right change to make once there is a second consumer
for it.

The fixed square target does not distort the model, because the projection is
built for the **window's** aspect ratio rather than the target's. Projecting
for the window pre-distorts the image by exactly the amount that stretching
the square target to fill the window undoes. What the fixed size does cost is
resolution: a window much larger than 1024 pixels in either direction is
showing a magnified image.

This is also why the demo's model window is deliberately not square. A square
window cannot show an aspect-handling mistake, so it would let one back in
unnoticed.

## The frame, in order

A render pass cannot be nested inside another, so the model cannot draw itself
while the window's pass is open:

```
vkBeginCommandBuffer
  cj_rgraph_execute_prepass   → model draws into its own target (own pass)
  vkCmdBeginRenderPass        → the window's pass
    cj_rgraph_execute         → the model node composites its target
  vkCmdEndRenderPass
vkEndCommandBuffer
```

`cj_rgraph_execute_prepass` records nothing for a graph without such a node,
so the other windows are unaffected.

## The camera

`cjelly_camera_distance_for_sphere()` places the camera far enough back that
the mesh's bounding sphere fits, using the tighter of the two field-of-view
angles - a viewport taller than it is wide is limited by its width, not its
height. The projection is built for Vulkan's clip space: depth 0 to 1, and Y
pointing down.

Both are in `ghoti.io/cjelly/mat4.h` and both are tested, which matters more than it
looks: a sign error in a projection does not fail, it renders the model upside
down or not at all, and there is no way to tell which from reading the code.

## A cross-platform trap worth knowing about

`Win32`'s `CreateWindowEx` sizes the **whole frame** and fits the client area
inside it. X11's `XCreateSimpleWindow` sizes the **client area** and the window
manager hangs its decoration outside. So the same `cj_window_desc_t` produces a
smaller drawable on Windows than on Linux - by the height of a title bar and
the width of two borders.

That does not reach the rendering, because the swapchain is sized from
`VkSurfaceCapabilitiesKHR::currentExtent`, which reports the drawable area on
both platforms. It does reach anything that reasons about `desc->width` and
`desc->height`, including where a cascade of windows lands.

The size a caller asks for should mean the same thing everywhere. Making it so
means calling `AdjustWindowRectEx()` on the Win32 path to grow the requested
size by the frame it will acquire.

## Limitations

- **Materials are not applied.** The MTL file is parsed and can be queried
  through the model library, but the node draws with one flat colour.
- **The offscreen target is a fixed size** and is not recreated on resize.
- **One model per node**, with no transform control beyond the built-in spin.
- **The fan triangulation is wrong for concave faces.** A real triangulator
  would be needed for those.

## Verifying the rendering without a display

The model path can be checked end to end on a virtual display, which is how
the screenshots in this document were produced and how the Vulkan half of it
was first verified at all. Two things make that worth doing: the validation
layers report the mistakes that do not show up on screen, and a captured frame
settles the ones that do.

```bash
sudo apt install vulkan-validationlayers xvfb

Xvfb :99 -screen 0 1600x1200x24 &

cd build/linux/release/apps
DISPLAY=:99 \
  LD_LIBRARY_PATH="$(cd ../../../..; pwd)/build/linux/release/apps:..." \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  VK_LOADER_LAYERS_ENABLE=VK_LAYER_KHRONOS_validation \
  CJELLY_DEMO_CAPTURE=/tmp/shots \
  ./main /path/to/model.obj
```

The demo renders a few frames, writes each window to `/tmp/shots/windowN.png`
through `cj_window_capture()`, and exits.

The pixels come from the swapchain, not from the screen, which is what makes
this workable: no screenshot tool, no window manager, and an obscured or
partly off-screen window captures correctly - none of which was true of the
`xwd` route this replaced.

`VK_DRIVER_FILES` points at lavapipe because Xvfb has no DRI3 and the hardware
driver cannot present to it. Software rendering is slow, and it will not catch
a fault specific to a real driver, but for correctness it is the better test:
lavapipe is strict where a vendor driver is often forgiving. The
validation layers need `VK_LAYER_PATH` set if they are not in the loader's
default search path.

`CJELLY_DEMO_WINDOW_OFFSET` spreads the demo's cascade out. It is no longer
needed for capturing - reading the swapchain does not care what is in front of
the window - but it remains useful when watching the demo on a real display.
