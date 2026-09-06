# Deadlock

A C++20 game engine built on SDL3, shared across the team as a linkable static
library. A game lives in its own repo and pulls the engine in with `FetchContent`
at a pinned tag, linking against `engine::engine`.

Licensed under the [MIT License](LICENSE).

## Adding Deadlock to your project

You do not need to vendor or install anything. Add this to your game's
`CMakeLists.txt` and CMake fetches and builds the engine into your build tree.
Pin a tag, never `main`:

```cmake
include(FetchContent)

FetchContent_Declare(
    engine
    GIT_REPOSITORY https://github.com/ShounakDeshmukh/GEF_engine.git
    GIT_TAG v0.1.0
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(engine)

add_executable(mygame main.cpp)
target_link_libraries(mygame PRIVATE engine::engine)
```

Link against `engine::engine`, not `engine`. The alias exists so that a typo
fails at configure time instead of quietly linking nothing.

A minimal `main.cpp` to confirm it is wired up:

```cpp
#include <engine/engine.hpp>

int main() {
    engine::log::init();
    engine::Window window("mygame", 1280, 720);
    engine::Renderer renderer(window);

    while (!window.shouldClose()) {
        window.pollEvents();
        renderer.clear({30, 30, 40, 255});
        renderer.present();
    }
    return 0;
}
```

`<engine/engine.hpp>` is the umbrella header; you can also include the individual
headers under `include/engine/` directly.

Three things that are easy to miss:

- **Pass `-DCMAKE_BUILD_TYPE=...` when you configure.** This is ordinary CMake
  behavior for single-config generators, not something the engine does, but it
  bites harder here: SDL3, freetype and harfbuzz all build inside your tree. With
  no build type, CMake adds no `-O` flag to any of them and the compiler defaults
  to `-O0`, so you get a debug-speed SDL3 in what you thought was a release build.
  The engine will not set it for you: `CMAKE_BUILD_TYPE` is a whole-tree cache
  variable, so an engine that set it would be picking the build type for *your*
  targets too, and forcing it would override a value you passed on the command
  line. What it does instead is warn at configure time if you forgot, so the
  `-O0` build is never silent. Pass it on the configure line:

  ```sh
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build build
  ```

  Use `Debug` while developing and `RelWithDebInfo` (optimized, keeps symbols) or
  `Release` for anything you time or hand to someone. The build type is stored in
  `build/CMakeCache.txt`, so you only pass it on the first configure of a given
  build directory - after that `cmake --build build` reuses it. To switch, either
  configure a second directory (`-B build-debug`) or re-pass the flag.
- **On Linux you still need the SDL3 apt dependency block below.** SDL3 is built
  from source inside your build tree, so its build-time dependencies have to be
  present on your machine too.
- **Only `include/engine/` is a stable public contract.** `src/`, `cmake/`, and
  internal target names may change without notice. Never `#include` anything
  outside `include/engine/`.

The engine's own examples and test suite are not built in your tree
(`ENGINE_BUILD_EXAMPLES` and `ENGINE_BUILD_TESTS` both default to
`PROJECT_IS_TOP_LEVEL`).

### Dependency contract

`glm` and `fmt` are both linked `PUBLIC`, so both reach you transitively. They are
not equally supported:

- **`glm` is supported.** It appears in public header signatures (`Transform`,
  `Renderer::fillRect`, `SpriteSheetLayout`), so you may use `glm::vec2` and friends
  freely against the pinned version. Removing it would be a breaking change.
- **`fmt` is not.** It is `PUBLIC` only because `include/engine/log.hpp` uses
  `fmt::format_string` in its inline templates. Use it through `engine::log`, not
  directly, and do not `#include <fmt/...>` on the strength of it being on the link
  line. It may be replaced with `std::format` in a future release.

Everything else (SDL3, SDL3_image, SDL3_ttf, spdlog, nlohmann_json) is `PRIVATE`
and invisible to you.

## Required toolchain

- CMake 4.4.2 or newer
- Ninja 1.13.2 or newer
- A C++20-capable GCC or Clang
- `ccache` (used as a compiler launcher by the presets, so it must be installed)

Those CMake and Ninja versions are **minimums, not exact requirements**. They are
written that way because apt on Ubuntu 24.04 ships CMake 3.28 and an older Ninja,
both too old, so the install steps below pull from upstream releases. A newer
CMake or Ninja from any source is fine.

### Linux

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
Wayland, audio, GL/EGL, input) - SDL3 is built from source via `FetchContent`, so
these need to be present on whatever machine configures the build.

### macOS

```sh
xcode-select --install
brew install cmake ninja ccache
```

Homebrew's CMake and Ninja are current, so nothing needs to come from GitHub
releases. SDL3 uses system frameworks on macOS, so there is no equivalent of the
Linux dependency block.

The macOS presets set `CMAKE_OSX_DEPLOYMENT_TARGET` to `14.0`. That is a floor,
not a target: builds run fine on newer macOS, and the resulting binaries stay
usable on Sonoma and later.

## Build and test

The presets are `linux-debug`, `linux-release`, `macos-debug`, and `macos-release`
(the release ones are `RelWithDebInfo`). Substitute whichever matches your machine
for `<preset>` below; each preset builds into `build/<preset>/`.

Configure - only needed once. CMake regenerates automatically afterward if
`CMakeLists.txt`/`cmake/Dependencies.cmake` change:

```sh
cmake --preset <preset>
```

Build - safe to run after every code change; only rebuilds what changed:

```sh
cmake --build --preset <preset>
```

Or configure + build + test in a single step:

```sh
cmake --workflow --preset <preset>
```

Run the tests:

```sh
ctest --test-dir build/<preset> --output-on-failure
```

Run an example:

```sh
./build/<preset>/examples/hello_window
```

`hello_window` opens a 1920x1080 window, clears it to blue every frame, and exits
cleanly when the window is closed.

Clean - only needed after a toolchain/compiler change, since `cmake --build` only
rebuilds what changed:

```sh
rm -rf build
```

## Formatting

```sh
cmake --build --preset <preset> --target format
```

Reformats every `.hpp`/`.cpp` under `include/`, `src/`, `examples/`, `tests/` with
`clang-format` in place.

## Documentation

Requires [Doxygen](https://www.doxygen.nl/):

```sh
sudo apt-get install -y doxygen    # or: brew install doxygen
cmake --build --preset <preset> --target docs
```

Generates the public API reference from `include/` into `docs/html/index.html`
(gitignored, regenerated on demand - not committed).

The `format` and `docs` targets only exist when Deadlock is the top-level project,
so they never collide with a consuming repo's own targets.

## Releases

See [CHANGELOG.md](CHANGELOG.md). Current release: `v0.1.0`.
