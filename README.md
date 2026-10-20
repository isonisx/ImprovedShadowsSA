# SA.ImprovedShadows

GTA San Andreas 1.0 US plugin scaffold. Output: `output/Release/SA.ImprovedShadows.asi`.

The plugin currently checks the game version and does not modify shadows.

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

## generate.sh

Set `PLUGIN_SDK_DIR` in `generate.sh` to your Plugin-SDK checkout, then:

```sh
./generate.sh
```

It deletes `build/` and `output/`, then writes Makefiles into `build/`.

## build.sh

Run this after `./generate.sh`:

```sh
./build.sh
```

1. Dry-runs Debug and sends the commands to Bear. Bear writes `compile_commands.json` in the project root. `LC_ALL=C` is required so Bear can parse Make.
2. Builds Release. The `.asi` is `output/Release/SA.ImprovedShadows.asi`.

`compile_commands.json` is gitignored. Run `./build.sh` again after changing includes, defines, or `premake5.lua`.

## clangd

`.clangd` tells clangd where the compilation database is. `./build.sh` must have run, so `compile_commands.json` exists.

The database calls `i686-w64-mingw32-g++`. clangd skips that compiler unless `--query-driver` matches it. Zed's folder settings already pass that flag.
