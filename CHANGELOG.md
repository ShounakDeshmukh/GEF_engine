# Changelog

All notable changes to Deadlock are documented here. This project adheres to
[Semantic Versioning](https://semver.org/). Only `include/engine/` is covered by
the version contract.

## [0.1.0] - 2026-09-06

First tagged release. Consuming repos should pin `GIT_TAG v0.1.0` instead of `main`.

### Added

- `engine::Window` - SDL-backed OS window with event pumping and a close flag.
- `engine::Renderer` - hardware-accelerated drawing: solid rects, texture loading,
  sprite sheets (`SpriteSheetLayout::grid`), entity batch draw, and text rendering
  with a per-entity rasterization cache (`deleteCachedText`, `clearTextCache`).
- `engine::ScalingMode` - `Constant` and `Proportional` logical presentation modes,
  switchable at runtime.
- `engine::Scene` - entity storage with `Transform`, `RigidBody`, `Collider`,
  `Shape`, `Text`, and `SpriteAnimation` components.
- `engine::PhysicsSystem` - gravity integration, AABB collision tests, and
  collision overlap queries.
- `engine::Timeline` and `engine::Stepper` - pausable, speed-scalable timeline with
  fixed-step iteration and dropped-tick accounting.
- `engine::InputHandler` - scancode-based keyboard state queries.
- `engine::log` - leveled logging with `{}`-style formatting.
- `engine::engine` alias target, so a typo in a consumer's `target_link_libraries`
  fails at configure time rather than silently linking nothing.
- `ENGINE_BUILD_TESTS` and `ENGINE_BUILD_EXAMPLES`, both defaulting to
  `PROJECT_IS_TOP_LEVEL`, so consumers do not build the examples or the test suite.
- macOS support: `macos-debug` / `macos-release` presets and a macOS CI leg.
- A configure-time warning when a consuming project has no `CMAKE_BUILD_TYPE`,
  since that silently compiles SDL3 and its vendored dependencies at `-O0`.

### Known issues

- The renderer's text cache is not evicted automatically. Entries live until
  `deleteCachedText` or `clearTextCache` is called explicitly, so a game that
  renders many distinct strings without calling either will grow its texture
  memory for the lifetime of the `Renderer`. Call `clearTextCache()` on scene
  transitions. Automatic eviction is deferred past 0.1.0.
- `glm` and `fmt` are both `PUBLIC` on the `engine` target, but only `glm` is a
  supported contract. See the dependency contract section in the README.
