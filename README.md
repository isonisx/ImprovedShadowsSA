# ImprovedShadowsSA

GTA San Andreas 1.0 US stencil-shadow fixes. Output: `output/Release/ImprovedShadowsSA.asi`.

The PC stencil path darkens the wrong pixels, pops shadows off at 50 units, and rebuilds building volumes every fourth frame. This plugin clears the stencil buffer and darkens only marked pixels, fades each shadow over the last quarter of a 120-unit range, updates every volume every frame, and raises the slot pool from 64 to 192. Dynamic objects with a collision shadow mesh are included. The darkness is the current sky color, divided down, instead of black, so noon shadows read blue and sunset follows the sky. Aircraft extrusion follows the sun, and forgotten aircraft subclasses plus BMX use the same special cases as helicopters, planes, and bikes.

## Configuration

The build copies [`doc/ImprovedShadowsSA.ini`](doc/ImprovedShadowsSA.ini) beside `ImprovedShadowsSA.asi`. Install both files together, including when using `scripts/` or a Mod Loader subfolder. Improved stencil shadows are always active on GTA SA 1.0 US. The plugin reads the INI once at startup; restart the game after editing it.

| Section | Option | Default | Effect |
| --- | --- | --- | --- |
| DynamicShadowColor | Div | 8.0 | Positive divisor for the sky tint. Higher values bring stencil shadows closer to black. |
| DynamicShadowColor | Opacity | 100 | Base stencil shadow opacity (`0`–`255`) before time-of-day strength and distance fading. |
| Extras | ForceStaticVehicleShadows | 0 | Set to `1` to bypass the graphics-quality restriction on static vehicle shadows. Normal distance and model checks still apply. |

`ForceStaticVehicleShadows` accepts only `0` or `1`. Missing or invalid options use their individual defaults. The `DynamicShadowColor` values support inline `#` comments like Junior_Djjr’s Dynamic Shadow Color mod.

## Prerequisites

- Xcode Command Line Tools: `make` and `/usr/bin/clangd`
- Homebrew: `mingw-w64`, `bear`, `premake` (`premake5` on `PATH`)
- A built [Plugin-SDK](https://github.com/DK22Pac/plugin-sdk) for GTA SA:
  - `$PLUGIN_SDK_DIR/output/lib/Plugin.a`
  - `$PLUGIN_SDK_DIR/output/lib/Plugin_d.a`

```sh
xcode-select --install
brew install mingw-w64 bear premake
```

## premake5.sh

Set `PLUGIN_SDK_DIR` in `premake5.sh` to your Plugin-SDK checkout, then:

```sh
./premake5.sh
```

It deletes `build/` and `output/`, then writes Makefiles into `build/`.

## build.sh

Run this after `./premake5.sh`:

```sh
./build.sh
```

1. Dry-runs Debug and sends the commands to Bear. Bear writes `compile_commands.json` in the project root. `LC_ALL=C` is required so Bear can parse Make.
2. Builds Release. The `.asi` is `output/Release/ImprovedShadowsSA.asi`.

`compile_commands.json` is gitignored. Run `./build.sh` again after changing includes, defines, or `premake5.lua`.

## clangd

`./build.sh` writes `compile_commands.json` in the project root. clangd uses that database.

The database calls `i686-w64-mingw32-g++`. clangd skips that compiler unless `--query-driver` matches it.
