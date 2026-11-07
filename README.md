# InfCube

A voxel sandbox game engine written in pure C.

## Assets

Game assets (textures, sounds) are not distributed with this repository. `res/MANIFEST.txt` lists every file expected under `res/`. 
Supply your own assets with those paths before building.

## How to Build

Building mainly targets Linux natively. The Windows binary is cross-compiled with mingw-w64 and statically linked.

Before building, convert the assets into header files:
```make assets```
This needs some dependencies, such as ImageMagick and libpng-dev.

Building for Windows:
```make windows -j```
Install mingw-w64 to get gcc for Windows.

Building for Linux:
```make -j```

## Notes for developers

Everything needed to statically build `infcube.exe` is inside `./lib`; it shouldn't need to be touched. 
`make assets` converts every `.png` or `.ogg` file under `./res/assets` into `.h` files that are included by the sources (this is why `./src/sound/sounds.c` takes long to build).

The exe icon can be replaced by swapping the `.ico` file (keeping the name) and running `make icon`. The new icon is linked on the next build. Windows caches icons, so restarting or moving the file may help it show up.

### Missing

The precompiled FreeType2 library for Windows is missing.

### Fonts

Fonts use the same "compile to header" scheme as `make assets`, but with the `ttf` format. The base size values are computed at runtime and can be scaled with the constants in `font.c`.

```c
// Texels rasterized per game unit
#define FONT_SCALE 4
// Target metrics of the game text grid in game units, taken from the old 8x8 bitmap font
#define FONT_CAP_UNITS 7
#define FONT_BASELINE_UNITS 7
```
