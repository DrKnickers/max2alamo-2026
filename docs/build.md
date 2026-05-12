# Build instructions

## What gets built where

| Target | Built in | Notes |
|---|---|---|
| `alamo_format` static lib | CMake / CI | Pure C++17, no Max SDK dependency |
| `alo_dump` CLI | CMake / CI | Standalone tool |
| `alo_roundtrip` CLI | CMake / CI | Standalone tool |
| `max2alamo.dle` | CMake (local only) | Requires 3ds Max 2026 SDK; not redistributable, hence not in CI |

## Prerequisites

- **Windows 10 / 11** (the Max plugin is Windows-only).
- **Visual Studio 2022 or 2026** with the "Desktop development with C++" workload (MSVC v143 or v144).
- **CMake 3.20+** (ships with VS, or install separately via `winget install Kitware.CMake`).
- For the Max plugin: **3ds Max 2026** + **3ds Max 2026 SDK** (free with Max install).

## Building the format library + CLI tools

From the repo root:

```bash
cmake -B build -S . -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

Outputs:
- `build/alamo_format/Release/alamo_format.lib`
- `build/tools/alo_dump/Release/alo_dump.exe`
- `build/tools/alo_roundtrip/Release/alo_roundtrip.exe`

If the Max SDK is also installed at one of the autodetected paths (`C:\Program Files\Autodesk\3ds Max 2026 SDK\maxsdk` or `D:\Autodesk\3ds Max 2026 SDK\maxsdk`), `max2alamo.dle` is built too.

## Building only the Max plugin

The plugin target is configured automatically when the Max SDK is found. If the SDK lives somewhere non-standard, point CMake at it:

```bash
cmake -B build -S . -DMAX_SDK_DIR="D:/path/to/maxsdk"
cmake --build build --config Release --target max2alamo
```

Output: `build/max2alamo/Release/max2alamo.dle` (~ a few hundred KB).

## Installing the plugin into 3ds Max 2026

Copy the built `.dle` into Max's plugins folder:

```powershell
Copy-Item build\max2alamo\Release\max2alamo.dle `
    "C:\Program Files\Autodesk\3ds Max 2026\Plugins\"
```

Restart 3ds Max. If the plugin loads correctly, **File → Export...** now lists `Alamo Object (*.ALO)` as a save-as type. Selecting it and confirming will currently pop a "Phase 3 scaffold reached DoExport()" dialog -- that's expected; real export logic lands in Phases 4 onward.

If Max fails to load the plugin, check `<MAX>\Network\Max.log` for an error. Most common cause: the `.dle` was built against the wrong SDK version (must match Max 2026, not 2024 / 2025 / etc.).

## Building everything in one go

```bash
cmake -B build -S . -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
```

This produces the static lib, both CLIs, the `.dle` (if SDK present), and the test executable.

## CI

GitHub Actions builds only the SDK-independent targets (`alamo_format`, `alo_dump`, `alo_roundtrip`, tests). The Max plugin is built locally by contributors and attached to GitHub Releases as a downloadable artifact at version tags.
