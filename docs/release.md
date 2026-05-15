# Release procedure

This document is the end-to-end checklist for cutting a versioned release. It exists because the `.dle` Max plugin can't be built in CI (Max SDK is non-redistributable per [`docs/build.md`](build.md)) and has to be manually attached to each release, which means a release is a multi-step ceremony rather than a one-button push.

## TL;DR

```
# 1. Pre-flight on main
git checkout main && git pull
cmake -B build -S . -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure

# 2. Bump the version
#    edit top-level CMakeLists.txt: project(... VERSION X.Y.Z ...)
#    edit CHANGELOG.md: add a new "## [X.Y.Z] -- YYYY-MM-DD" section above [Unreleased]
#    commit as "chore: bump version to X.Y.Z" via a PR; merge to main.

# 3. Tag from main
git checkout main && git pull
git tag vX.Y.Z
git push origin vX.Y.Z
#    -> .github/workflows/release.yml fires, builds CLIs, creates a DRAFT release.

# 4. Build the .dle locally
cmake --build build --config Release --target max2alamo

# 5. Attach the .dle to the draft release
#    GitHub -> Releases -> the draft -> Edit -> drag-drop build/max2alamo/Release/max2alamo.dle
#    Verify the asset list contains all 4 files.

# 6. Publish the draft
#    GitHub -> Releases -> the draft -> Edit -> "Publish release"

# 7. Update the dev log
#    Open a PR adding the release announcement to docs/development-log.md;
#    note the published date + the release URL.
```

## Detailed steps

### 1. Pre-flight on `main`

Make sure `main` is in a shipping state before tagging. Tagging from a stale or dirty `main` is the most common way to ship a bad release in this kind of project.

```
git checkout main
git pull
git status            # must be clean
```

Build and run the full automated suite:

```
cmake -B build -S . -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

All Catch2 cases must pass. If the corpus is locally available, also run a Tier 2 round-trip sweep:

```
build\tools\alo_roundtrip\Release\alo_roundtrip.exe --dir tests\corpus\eaw
build\tools\alo_roundtrip\Release\alo_roundtrip.exe --dir tests\corpus\foc
```

Pass rate must match the most recent baseline recorded in [`docs/development-log.md`](development-log.md).

Build the Max plugin locally and run the harness:

```
cmake --build build --config Release --target max2alamo
powershell -File scripts/run-max-tests.ps1
```

If the harness is gated behind PowerShell execution policy, dispatch `3dsmaxbatch.exe` per test manually -- see the [`docs/build.md`](build.md) "Tier 3" section.

### 2. Bump the version

The version source-of-truth is `project(VERSION ...)` in the top-level `CMakeLists.txt`. CMake's `configure_file` propagates it to `alamo_format/include/alamo_format/version.h` (a generated header in the build tree), which the CLIs `#include` and surface via `--version`.

To bump: edit the `VERSION` field, then update `CHANGELOG.md`:

```
## [Unreleased]                <-- delete this section's body, keeping the heading

## [X.Y.Z] -- YYYY-MM-DD       <-- new section: list every notable change since the last release
```

Open a PR titled `chore: bump version to vX.Y.Z`. CI must pass. Merge to `main` via the standard rebase workflow.

### 3. Tag from `main`

```
git checkout main
git pull
git tag vX.Y.Z
git push origin vX.Y.Z
```

The tag must point at the merge commit produced in step 2. Do NOT tag from a feature branch; the release workflow extracts the `## [X.Y.Z]` section from `CHANGELOG.md` at the tagged commit, which only works if that commit has the section.

The `.github/workflows/release.yml` workflow fires on tag push and:

- Builds the SDK-independent targets (alamo_format + alo_dump + alo_roundtrip).
- Runs `ctest` against the built artifacts.
- Verifies the CLI `--version` output matches the tag (a tag-version drift detector).
- Extracts the `CHANGELOG.md` section for this tag.
- Creates a **draft** GitHub Release with the CLIs + `alamo_format.lib` attached and the changelog section as the body.

Watch the workflow in the Actions tab; if it fails, the tag is harmless (no published release yet) -- delete it and fix the issue:

```
git push --delete origin vX.Y.Z
git tag -d vX.Y.Z
```

### 4. Build the `.dle` locally

```
cmake --build build --config Release --target max2alamo
```

Output: `build\max2alamo\Release\max2alamo.dle`. Also copied to `plugin\max2alamo.dle` by the post-build install step.

The `.dle` must be built from the exact SHA the tag points at -- otherwise the binary set in the release is inconsistent. Check:

```
git rev-parse HEAD                            # must match the tag's commit
git describe --tags --exact-match HEAD        # should print vX.Y.Z
```

If `git describe` says you're not on the tag, switch to it:

```
git checkout vX.Y.Z
cmake --build build --config Release --target max2alamo
```

### 5. Attach the `.dle` to the draft

GitHub -> Releases -> the draft -> Edit. Drag-drop `build\max2alamo\Release\max2alamo.dle` into the assets section. After upload, the asset list should contain:

- `alo_dump.exe`
- `alo_roundtrip.exe`
- `alamo_format.lib`
- `max2alamo.dle`

### 6. Publish

GitHub -> Releases -> the draft -> Edit -> "Publish release". The draft becomes the public release.

If the tag includes a pre-release suffix (e.g. `v0.9.0-rc.1`), the release workflow sets `prerelease: true` automatically; the published release will be marked as a pre-release in the GitHub UI.

### 7. Update the dev log

Open a follow-up PR (title: `docs: announce vX.Y.Z release`) that:

- Adds the release announcement under a "Releases" subsection of [`docs/development-log.md`](development-log.md), with the link to the GitHub Release.
- If this is a phase-closing release, updates the relevant phase status row.

## Rollback

If a published release contains a bad artifact:

1. Don't delete the release -- mark it as a pre-release in the GitHub UI to dim it from the "Latest release" badge, then add an admonishment to the release body pointing at the fixed version.
2. Open a PR with the fix; bump the patch version (`vX.Y.Z` -> `vX.Y.Z+1`); cut a new release per steps 1-6.
3. The deleted-tag protocol (`git push --delete origin vX.Y.Z`) is only safe BEFORE step 6 (publish). Once published, the tag is part of the project's history.

## What's NOT in CI

- The `.dle` build (Max SDK is non-redistributable; manual per step 4).
- Code-signing the `.dle` (modders distribute unsigned `.dle` files as standard practice in the Alamo modding ecosystem).
- Auto-publishing the draft (intentional gate so the maintainer can attach the `.dle` and review).

## What CI DOES verify

- All Catch2 unit tests pass.
- CLI `--version` matches the tag (catches version drift between `CMakeLists.txt` and the tag).
- The `CHANGELOG.md` has a matching `## [X.Y.Z]` section (catches "forgot to add changelog entry").
- Artifacts upload successfully.
