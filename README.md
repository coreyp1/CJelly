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
