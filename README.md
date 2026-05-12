# max2alamo-2026

A modern 3ds Max 2026 plugin for exporting models and animations to the **Alamo** (`.alo` / `.ala`) format used by *Star Wars: Empire at War* and *Forces of Corruption*.

This is a clean-room rewrite of Petroglyph's 3ds Max 9 `max2alamo.dle`, which has been frozen since 2007 and ships only as a closed-source binary. The original plugin's SDK ABI is incompatible with every modern Max release, so a fresh implementation against the Max 2026 SDK is the only path forward for current Max users.

## Authorship

**This project is being built by Claude (Anthropic's AI assistant), working under the direction of [@DrKnickers](https://github.com/DrKnickers).**

Every commit, file, and design decision in this repo was authored by Claude during interactive sessions. @DrKnickers provides direction, scope, priorities, testing on real `.alo`/`.ala` content, and (in the upcoming Phase 3+) the in-Max validation that no AI assistant can run on its own. Public commit history reflects the collaboration honestly: human sets the goal, AI does the implementation, human verifies in the actual game.

If you're using this plugin, you should know that. If you're auditing the code, the relevant context is "this was written end-to-end by an LLM with format references in front of it" — which has implications for how you should review it (look for genuine understanding, not pattern-matched plausibility).

## Status

**Pre-release, not yet feature-complete.** The repo is public so that GitHub Actions runs without consuming the private-repo Actions quota — visibility does not mean PRs are open. See [CONTRIBUTING.md](CONTRIBUTING.md). The first public release (v1.0) ships when EaW + FoC export is feature-complete.

For the full picture — what's shipped, what's next, where things live, key decisions, command cheat sheet — see **[`docs/development-log.md`](docs/development-log.md)**.

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

- **`alamo_format/`** — Static C++17 library implementing the `.alo` and `.ala` chunk formats. No 3ds Max dependencies. Built in CI; round-trips 100 % of the vanilla EaW + FoC corpus byte-for-byte.
- **`max2alamo/`** *(added in Phase 3)* — The actual `.dle` plugin. Targets the 3ds Max 2026 SDK. Built locally only; the Max SDK is not redistributable.
- **`tools/`** — Standalone CLIs (`alo_dump`, `alo_roundtrip`) used as test oracles.

## Building

The format library and CLI tools build with CMake on Windows / MSVC. The Max plugin builds via Visual Studio with the Max 2026 SDK installed locally. See [docs/build.md](docs/build.md) for full instructions.

The test corpus is *not* committed (it contains Lucasfilm IP). See [docs/corpus.md](docs/corpus.md) for how to extract one from a vanilla install.

## License

MIT. See [LICENSE](LICENSE).

## Acknowledgments

**This plugin would not exist without [Mike Lankamp (Mike.NL)](https://mike.nl/) and his foundational work on the Alamo engine toolchain.** Years before this project started, Mike reverse-engineered the `.alo` and `.ala` chunk formats and built the community tooling that everyone — including this plugin — still depends on:

- **AloViewer** — the standalone `.alo` renderer. It's the ground-truth visual check for every export this plugin produces; if a file looks right in AloViewer, we have high confidence it will load in-game.
- **`alamo2max` importer** (MAXScript) — the original `.alo`/`.ala` reader for 3ds Max. It's effectively the format specification in executable form, and was the primary cross-reference used while implementing `alamo_format`'s read path.
- **File-format documentation** — the chunk-tag tables, vertex-format layouts, and animation-frame encodings that the Petrolution mod-tools docs codify trace back to Mike's reverse-engineering work.

The exporter in this repo, and the importer planned for after v1, are both built directly on top of what Mike figured out. Huge thanks.

**Thanks also to [Gaukler](https://github.com/Gaukler)** for the [Blender ALAMO plugin](https://github.com/Gaukler/Blender-ALAMO-Plugin) — an open-source, actively maintained importer/exporter for Blender. Its Python source is the canonical reference for `material_parameter_dict`, `vertex_format_dict`, and the bump-mapping shader list, and was used throughout development as a second working implementation to cross-check format details against.

**And finally, thanks to [Petroglyph](https://www.petroglyphgames.com/)** — for shipping *Empire at War* / *Forces of Corruption* as a moddable game in the first place, for two decades of tolerance (and occasional active support) for the community that grew up around it, and for releasing the EaW shader source code and other internal tools to modders. Vanishingly few studios do any of that, let alone all of it. The fact that a 20-year-old game still has people writing new plugins for it is a direct consequence of those choices.

## References

- [Petrolution Mod-Tools — `.alo` format spec](https://modtools.petrolution.net/docs/AloFileFormat)
- [Petrolution Mod-Tools — `.ala` format spec](https://modtools.petrolution.net/docs/AlaFileFormat)
- [Petrolution Mod-Tools — `.meg` format spec](https://modtools.petrolution.net/docs/MegFileFormat)
- [Gaukler/Blender-ALAMO-Plugin](https://github.com/Gaukler/Blender-ALAMO-Plugin) — open-source Blender importer/exporter (Python); used as a working executable reference for the format.
