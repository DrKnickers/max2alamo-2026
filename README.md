# max2alamo-2026

A modern 3ds Max 2026 plugin for exporting models and animations to the **Alamo** (`.alo` / `.ala`) format used by *Star Wars: Empire at War* and *Forces of Corruption*.

This is a clean-room rewrite of Petroglyph's 3ds Max 9 `max2alamo.dle`, which has been frozen since 2007 and ships only as a closed-source binary. The original plugin's SDK ABI is incompatible with every modern Max release, so a fresh implementation against the Max 2026 SDK is the only path forward for current Max users.

## Status

**Pre-release, private development.** The repository will be flipped to public under the MIT license once v1.0 ships (EaW + FoC export feature-complete). See [docs/format-notes.md](docs/format-notes.md) and the project's GitHub issues for current phase status.

## Goals (v1)

- Export 3ds Max 2026 scenes to valid `.alo` files loadable in vanilla EaW and FoC.
- Export `.ala` animations sampled from Max bone tracks.
- **Object visibility tracks.** Sample each object's animated visibility (Max's per-object visibility track) per frame and write it into the `.ala` as a bit-packed visibility track, matching the legacy exporter's behavior. EaW uses these in-game for parts that appear / disappear (blinking nav lights, hardpoint state changes, debris reveal, etc.).
- Cover: static meshes, skinned meshes (multi-bone weighting), Petroglyph stock shaders with parameter export, hardpoints, proxies, lights.

## Non-goals (v1)

- UaW Format #2 dazzles.
- Backporting to older Max versions.

## Roadmap (post-v1)

- **Importer.** Once the exporter is stable, the next project is a modern Max 2026 importer to replace Mike Lankamp's `alamo2max.dlu`. The `alamo_format` library is designed so the read side is already there (exercised by `alo_dump` + round-trip tests); only the Max-side glue remains.

## Architecture

Two-project layout:

- **`alamo_format/`** — Static C++17 library implementing the `.alo` and `.ala` chunk formats. No 3ds Max dependencies. Built in CI.
- **`max2alamo/`** *(added in Phase 3)* — The actual `.dle` plugin. Targets the 3ds Max 2026 SDK. Built locally only; the Max SDK is not redistributable.
- **`tools/`** — Standalone CLIs (`alo_dump`, `alo_roundtrip`) used as test oracles.

## Building

The format library and CLI tools build with CMake on Windows / MSVC. The Max plugin builds via Visual Studio with the Max 2026 SDK installed locally. See [docs/build.md](docs/build.md) for full instructions.

## References

- [Petrolution Mod-Tools — `.alo` format spec](https://modtools.petrolution.net/docs/AloFileFormat)
- [Petrolution Mod-Tools — `.ala` format spec](https://modtools.petrolution.net/docs/AlaFileFormat)
- [Gaukler/Blender-ALAMO-Plugin](https://github.com/Gaukler/Blender-ALAMO-Plugin) — open-source Blender importer/exporter (Python); used as a working executable reference for the format.
