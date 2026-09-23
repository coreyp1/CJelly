# CJelly - A Cross-Platform GUI Library

## MSYS2 MINGW64 Setup

To install packages, from the repo base directory, run:

```
pacman -S --needed - < install/msys2_packages.txt
```

## WSL2 Ubuntu 22.04

To install packages, from the repo base directory, run:

```
sudo apt install $(cat install/wsl2_ubuntu22_packages.txt)
```

It may be that the installation seems to get hung.  Worse yet, it happens on a
message that says, in part, "this may take a while...".  Evidently this is a
known bug that has been around at least 7 years.  Just press `Enter` a dozen or
so times, and it will resume.  (Check `htop` or something like that to see
whether or not you actually see activity before spamming the `Enter` key.)

This may work on other versions, but I haven't tried it.

## Run a test

From the repo base directory, run:

```
make test
```

For other command, run:

```
make help
```

## Drawing a model

The demo's fourth window loads a Wavefront OBJ file and spins it. Pass a path
to use your own:

```
./build/linux/release/apps/main path/to/model.obj
```

See [docs/models.md](docs/models.md) for how that is put together, and what it
does not do yet.

## Diagnostics

The library is quiet unless something failed. To hear more:

```
CJELLY_LOG=debug ./build/linux/release/apps/main
```

`CJELLY_LOG` takes `off`, `error` (the default), `warn`, `info`, `debug` or
`trace`, or the digits `0` to `5`. `trace` includes bulk listings such as
every extension a device reports, which runs to a few hundred lines per
device.

An embedding application can set the level itself with `cj_log_set_level`,
and take the messages rather than letting them reach stderr with
`cj_log_set_sink`. See
[cj_log.h](include/ghoti.io/cjelly/cj_log.h).

Vulkan validation messages arrive at the level matching the severity the
layer gave them, so validation errors are visible by default and the layers'
own chatter is not.

## License

LGPL-3.0-only. See [COPYING.LESSER](COPYING.LESSER) for the license, and
[COPYING](COPYING) for the GPL text it is written as additional permissions
on top of.

Contributions are not being accepted at this time; see
[CONTRIBUTING.md](CONTRIBUTING.md) for what is useful instead.
