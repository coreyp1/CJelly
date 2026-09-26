# CJelly

A Vulkan-first GUI library in C. It opens native windows and draws with its
own renderer.

## What is implemented

This is what the library implements.

- Native windows, and a renderer that draws colored panels, an image, and a Wavefront model.
- A render graph per window, including a model node.
- A process-wide engine, input events, and a log.

It does not embed native controls. The design draft names Windows and a
software fallback; those are not what `make demo` exercises. The demo runs
on Linux, with X11 and Vulkan.

## Before you call it

- The library is quiet unless something failed. `CJELLY_LOG` takes `off`, `error` (the default), `warn`, `info`, `debug` or `trace`, or the digits `0` to `5`. An embedding program can set the level with `cj_log_set_level` and take the messages with `cj_log_set_sink` instead of letting them reach stderr.
- Vulkan validation messages arrive at the severity the layer gave them, so validation errors are visible by default.
- `cj_engine_create()` does not touch Vulkan, which is why the handle table can be tested with no device. A stale handle stays rejected after its slot is reused; the generation counter is what makes that true.
- `make test` needs neither a GPU nor a window. `make demo` does.

## Examples

The interactive demo is the fastest way to see it. It needs a GPU, a
display, and the Vulkan drivers:

```bash
make demo
make demo MODEL=path/to/model.obj
```

To hear more:

```bash
CJELLY_LOG=debug ./build/linux/release/apps/main
```

A model is a node on a window's render graph:

```c
cj_rgraph_add_model_node(graph, "model", "teapot.obj");
```

[docs/models.md](docs/models.md) is how that node is put together.
[docs/Overview.md](docs/Overview.md) is the design draft for the toolkit.

## Compile and link

Once the library is installed, pkg-config carries the include path, the
library, and its dependencies:

```bash
cc -o show show.c $(pkg-config --cflags --libs ghoti.io-cjelly-0)
```

The module name ends in the major version, `-0` for this release, so two
majors can be installed side by side. A build made with `make BRANCH=-dev`
installs `ghoti.io-cjelly-dev` instead.

The renderer also needs a Vulkan SDK and, on Linux, X11. `make test` does
not link a device.

## Building the library

[cutil](https://github.com/Ghoti-io/cutil),
[image](https://github.com/coreyp1/image) and
[model](https://github.com/coreyp1/model) must already be installed where
pkg-config can see them. A dependency it cannot find is a hard error naming
the fix.

```bash
make
make test
sudo make install
```

From the workspace, which installs the three libraries first:

```bash
./bootstrap.sh
export PKG_CONFIG_PATH="$PWD/.local/share/pkgconfig"
make -C libs/cjelly test PREFIX="$PWD/.local"
```

`make help` lists the rest.

| Target | What it does |
| --- | --- |
| `make` | The libraries and the demo binary |
| `make test` | Unit tests, headless |
| `make demo` | Build and run the interactive demo. `MODEL=` picks an OBJ file |
| `make test-asan` | Rebuild with ASan and UBSan and run the tests |
| `make docs` | The Doxygen manual, into `./docs` |

## The API

Public headers live under `<ghoti.io/cjelly/...>`. The ones an embedding
program starts from:

- **`cj_engine.h`** — the process-wide engine. `cj_engine_create()`.
- **`cj_window.h`** — a window: create, resize, a frame, present.
- **`cj_rgraph.h`** — the render graph for a window, including `cj_rgraph_add_model_node()`.
- **`cj_input.h`** — input events.
- **`cj_log.h`** — the log level and the sink.
- **`runtime.h`** — `cj_run()`, the event loop.

[What is implemented](#what-is-implemented) is the inventory.
[Before you call it](#before-you-call-it) is what that changes about a call.

## Dependencies

All three are found through pkg-config, and the installed `.pc` file names
them, so a program that links `ghoti.io-cjelly-0` links these too.

- [ghoti.io-cutil](https://github.com/Ghoti-io/cutil) — the handle map and the allocator.
- [ghoti.io-image](https://github.com/coreyp1/image) — every image the toolkit loads.
- [ghoti.io-model](https://github.com/coreyp1/model) — OBJ and MTL for the model node.

## Documentation

| Page | What it settles |
| --- | --- |
| [docs/models.md](docs/models.md) | How a model node is put together |
| [docs/Overview.md](docs/Overview.md) | The design draft for the toolkit |
| [include/ghoti.io/cjelly/cj_log.h](include/ghoti.io/cjelly/cj_log.h) | The log level and the sink |

`make docs` builds the manual.

## Status

The demo runs on Linux, with X11 and Vulkan.

## License

LGPL-3.0-only. See [COPYING.LESSER](COPYING.LESSER) for the license, and
[COPYING](COPYING) for the GPL text it is written as additional permissions
on top of.

Contributions are not being accepted at this time; see
[CONTRIBUTING.md](CONTRIBUTING.md) for what is useful instead.
