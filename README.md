# Deadlock engine

A C++20 game engine built on SDL3, shared across the team as a linkable
static library. Games live in separate consuming repos (e.g. `game-template`) that
`FetchContent` this repo at a pinned tag and link against the `engine` target.

## Required toolchain

- CMake 4.4.2
- Ninja 1.13.2
- A C++20-capable GCC or Clang (`CMakePresets.json` uses the system default `g++`)
- `ccache` (used as a compiler launcher to speed up rebuilds after a full wipe)

apt's CMake (3.28) and Ninja on Ubuntu 24.04 are both older than our pins, so
install those two straight from their GitHub releases:

```sh
sudo apt-get update
sudo apt-get install -y g++ ccache unzip \
    libasound2-dev libpulse-dev libaudio-dev libfribidi-dev libjack-dev \
    libsndio-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
    libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev \
    libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev \
    libthai-dev libusb-1.0-0-dev libpipewire-0.3-dev libwayland-dev \
    libdecor-0-dev liburing-dev

curl -fsSL -o /tmp/cmake.tar.gz \
    https://github.com/Kitware/CMake/releases/download/v4.4.2/cmake-4.4.2-linux-x86_64.tar.gz
sudo tar -xzf /tmp/cmake.tar.gz -C /opt
echo 'export PATH="/opt/cmake-4.4.2-linux-x86_64/bin:$PATH"' >> ~/.bashrc

curl -fsSL -o /tmp/ninja.zip \
    https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-linux.zip
sudo unzip -q /tmp/ninja.zip -d /usr/local/bin
sudo chmod +x /usr/local/bin/ninja
```

The `libasound2-dev`... block is SDL3's own Linux build dependency list (X11,
Wayland, audio, GL/EGL, input) — SDL3 is built from source via `FetchContent`, so
these need to be present on whatever machine configures the build.

## Build and test

Configure — only needed once. CMake regenerates automatically afterward if
`CMakeLists.txt`/`cmake/Dependencies.cmake` change, so you rarely need to run
this again by hand:

```sh
cmake --preset linux-debug
```

Build — safe to run after every code change; only rebuilds what changed:

```sh
cmake --build --preset linux-debug
```

Or configure + build + test in a single step:

```sh
cmake --workflow --preset linux-debug
```

Run the tests:

```sh
ctest --test-dir build/linux-debug --output-on-failure
```

Run the example:

```sh
./build/linux-debug/examples/hello_window
```

`hello_window` opens a 1920x1080 window, clears it to blue every frame, and exits
cleanly when the window is closed.

Clean — only needed after a toolchain/compiler change; not needed for normal
code edits, since `cmake --build` only rebuilds what changed:

```sh
rm -rf build
```

## Formatting

```sh
cmake --build --preset linux-debug --target format
```

Reformats every `.hpp`/`.cpp` under `include/`, `src/`, `examples/`, `tests/` with
`clang-format` in place.

## Documentation

Requires [Doxygen](https://www.doxygen.nl/):

```sh
sudo apt-get install -y doxygen
cmake --build --preset linux-debug --target docs
```

Generates the public API reference from `include/` into `docs/html/index.html`
(gitignored, regenerated on demand — not committed).

## Rule for consuming repos

Only `include/engine/` is a stable public contract. `src/`, `cmake/`, and internal
target names may change without notice consuming repos must never `#include` anything outside `include/engine/`.
