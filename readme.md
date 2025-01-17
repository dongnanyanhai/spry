# Spry

[Spry](https://jasonliang.js.org/spry/) is a 2D game framework made for rapid
prototyping.

- [Download Spry](https://github.com/jasonliang-dev/spry/releases)
- [Quick Start](https://jasonliang.js.org/spry/quick-start.html)
- [API reference](https://jasonliang.js.org/spry/docs.html)

## Basic example

The following code creates a window and draws `Hello, World!` to the screen.

```lua
function spry.start()
  font = spry.default_font()
end

function spry.frame(dt)
  font:draw('Hello, World!', 100, 100)
end
```

## Spry vs. LÖVE

Spry takes heavy inspiration from [LÖVE](https://love2d.org/). Below is a
non-exhaustive list of differences between the two:

- Spry's API uses short function names more suitable for prototyping.
- Spry implicitly loads all Lua scripts in a project.
- Spry projects can be deployed to the web.
- Spry has hot reload support for images, sprites, and tilemaps.
- Spry can load Aseprite and LDtk files without needing to convert/export
  assets to `.json`. LÖVE cannot load these files directly.
- Spry is a single executable, weighting in at about 1.6mb (0.8mb zipped).
  LÖVE is 10mb.
- LÖVE uses LuaJIT 2.1. Spry uses PUC Lua 5.4.
- LÖVE has lots of documentation and community support.
- LÖVE is mature, stable, and battle-tested.
- LÖVE uses `conf.lua` for configuration options. Spry does not need a
  separate config file.
- LÖVE has more overall features, such as system threads, touchscreen support,
  filesystem access, gamepad input, and networking sockets.

## Run the examples

This repository includes some project examples. You can run them with the
following commands:

```sh
spry examples/basic
spry examples/planes
spry examples/dungeon
spry examples/jump
spry examples/boxes
```

## Building from source

Requires CMake and one of the following C/C++ compilers depending on the
platform:

- Visual Studio for Windows
- Clang for Linux
- Emscripten for web browsers

Other compilers might work, but they haven't been tested.

In the command line:

```sh
mkdir build
cd build
cmake .. # This command probably looks different
cmake --build .
```

The first `cmake` command might need extra flags depending on the environment.
For example, the command below generates a release build for a Linux machine
that has both `gcc` and `clang` installed, since CMake would likely choose
`gcc` over `clang`:

```sh
cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release ..
```

This command should be used when building for web browsers:

```sh
emcmake cmake -DCMAKE_BUILD_TYPE=Release ..
```

## Shoutouts

Special thanks to:

- floooh, for making [Sokol](https://github.com/floooh/sokol).
- RandyGaul, for making [cute_headers](https://github.com/RandyGaul/cute_headers).
- Erin Catto for making [Box2D](https://github.com/erincatto/box2d).
- Casey Muratori, for showing me that I don't need to a huge engine to make
  games through [Handmade Hero](https://handmadehero.org/).
- rxi, for making [lite](https://github.com/rxi/lite). It was my introduction
  to creating programs with Lua.
- [LÖVE](https://love2d.org/), for being an awesome framework, and for being
  the main inspiration for this project.
