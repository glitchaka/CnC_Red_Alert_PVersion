# Red Alert Framework

This fork is being refactored from the released Command & Conquer: Red Alert source into a reusable Windows game framework.

The **default build no longer attempts to compile the entire 1990s game monolith**. It builds a clean framework library and a small sandbox application that proves the public API can be used by a separate game target.

## Default outputs

Running `build.cmd` produces:

- `build/bin/Release/ra_sandbox.exe` — example game using the framework API.
- `build/lib/Release/ra_engine.lib` — reusable static framework library.
- Public headers under `FRAMEWORK/include/ra/`.

## Current framework API

`RA::Engine` currently provides:

- application/game loop;
- resizable SDL3 window;
- frame timing / delta time;
- keyboard input with down / pressed / released state;
- basic 2D rendering primitives;
- an application-facing `ra::Game` interface with start, update, render and stop callbacks.

The sandbox in `examples/basic_game.cpp` is intentionally a separate executable linked against the framework. This is the pattern new games should use instead of including Red Alert's old global game code directly.

## Using it from another CMake target

When this repository is part of your CMake build, link your game against the exported target:

```cmake
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE RA::Engine)
```

Then include the public API:

```cpp
#include <ra/Engine.h>
```

Derive your game from `ra::Game`, put simulation in `onUpdate`, drawing in `onRender`, and call `ra::Engine::run` from your executable.

## Architecture

- `FRAMEWORK/` — reusable modern framework code and public API.
- `examples/` — standalone framework examples/sandboxes.
- `CODE/`, `WIN32LIB/`, `IPX/`, `VQ/`, `WINVQ/` — original/legacy Red Alert source retained for incremental extraction of useful engine systems.
- `PORT/` — compatibility/porting work that may be reused while legacy systems are migrated.

The original game source is **not** part of the default framework target. Legacy subsystems should be migrated behind clean framework interfaces rather than added wholesale to `ra_engine`.

## Platform dependencies

The framework-first build uses **SDL3** as the platform layer. The obsolete DirectX 5 / DirectDraw, HMI SOS and Greenleaf dependencies are not required by the default framework build.

Audio, asset/container loading, Red Alert palette/sprite support, scene/entity systems and adapters for reusable legacy gameplay systems are intended to be added as framework modules rather than restoring the old monolithic dependency graph.

## Licensing

This repository contains source released by Electronic Arts under GPLv3. Any reuse or distribution must comply with the licensing terms present in the repository and applicable third-party licenses.
