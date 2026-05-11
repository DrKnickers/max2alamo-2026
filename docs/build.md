# Build instructions

## What gets built where

| Target | Built in | Notes |
|---|---|---|
| `alamo_format` static lib | CMake / CI | Pure C++17, no Max SDK dependency |
| `alo_dump` CLI | CMake / CI | Standalone tool |
| `alo_roundtrip` CLI | CMake / CI | Standalone tool |
| `max2alamo.dle` | Visual Studio (local only) | Requires 3ds Max 2026 SDK; not redistributable, hence not in CI |

## Prerequisites

- **Windows 10 / 11** (the Max plugin is Windows-only).
- **Visual Studio 2022** with the "Desktop development with C++" workload (MSVC v143).
- **CMake 3.20+** (ships with VS 2022).
- For the Max plugin (Phase 3 onwards): **3ds Max 2026** + **3ds Max 2026 SDK** (free with Max install).

## Building the format library + CLI tools

From the repo root in any shell:

```bash
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Outputs:
- `build/alamo_format/Release/alamo_format.lib`
- `build/tools/alo_dump/Release/alo_dump.exe`
- `build/tools/alo_roundtrip/Release/alo_roundtrip.exe`

## Building the Max plugin (Phase 3+)

*Instructions added in Phase 3 once the Visual Studio solution lands.*

## Running tests

*Test instructions added in Phase 1 once `tests/` lands.*
