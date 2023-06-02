# Spry

[Spry](https://jasonliang.js.org/spry/) is a 2D game framework made for rapid
prototyping.

- [Download for Windows](https://github.com/jasonliang-dev/spry/releases/download/1.0/spry-windows-x86-64.exe)
- [Download for Linux](https://github.com/jasonliang-dev/spry/releases/download/1.0/spry-linux-x86-64)
- [Documentation](https://jasonliang.js.org/spry/)

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

## Run the examples

This repository includes some project examples. You can run them with the
following commands:

```sh
spry examples/planes
spry examples/dungeon
spry examples/boxes
```

## Building from source

Build `src/spry.cpp` through a C++17 compiler. `src/deps/box2d` needs to be in
the include path, and either `DEBUG` or `RELEASE` must be defined.

An optimized build of Spry using MSVC looks like this:

```sh
cl /std:c++17 /O2 /EHsc /DNOMINMAX /DRELEASE /Isrc/deps/box2d src/spry.cpp
```