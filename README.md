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
