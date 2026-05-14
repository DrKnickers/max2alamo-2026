# Building a vanilla `.alo` test corpus

The format library's reader and (in Phase 2) round-trip tests need a body of real `.alo` files to validate against. We can't ship those in the repo — they're Lucasfilm intellectual property — so each developer builds their own corpus locally from a vanilla EaW / FoC install. The corpus lives at `tests/corpus/` (gitignored).

## Prerequisites

- A legitimate install of *Star Wars: Empire at War* (and ideally the *Forces of Corruption* expansion). Steam, GOG, or original disc all work.
- Windows PowerShell or PowerShell 7+.

## Where the source archives live

| Game | File |
|---|---|
| EaW base | `<install>\GameData\Data\models.meg` |
| FoC | `<install>\corruption\Data\models.meg` |

For a Steam install, `<install>` is typically `C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War` (or another Steam library path).

## Extraction

Run [`scripts/extract-meg.ps1`](../scripts/extract-meg.ps1) once for each game:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\extract-meg.ps1 `
    -MegPath "<install>\GameData\Data\models.meg" `
    -OutDir tests\corpus\eaw `
    -Pattern *.alo

powershell -ExecutionPolicy Bypass -File scripts\extract-meg.ps1 `
    -MegPath "<install>\corruption\Data\models.meg" `
    -OutDir tests\corpus\foc `
    -Pattern *.alo
```

Expected counts (vanilla content):

| Source | `.alo` files |
|---|---|
| EaW `models.meg` | ~1383 |
| FoC `models.meg` | ~683 |

## Format support

The script handles **MEG Format 1** only — what vanilla EaW and FoC use. Format 2 (encryption-capable header) and Format 3 (AES-128 CBC, used in Grey Goo / 8-Bit Armies / *The Great War: Western Front*) are detected and rejected with a clear error. See the [Petrolution MEG format spec](https://modtools.petrolution.net/docs/MegFileFormat) for the full picture.

## Verifying the corpus

Once built, the corpus should round-trip cleanly through `alo_dump`:

```powershell
$exe = "build\tools\alo_dump\Release\alo_dump.exe"
$files = Get-ChildItem tests\corpus -Recurse -Filter *.alo
$pass = 0; $fail = 0
foreach ($f in $files) {
    & $exe $f.FullName --brief > $null 2>&1
    if ($LASTEXITCODE -eq 0) { $pass++ } else { $fail++ }
}
Write-Host "$pass / $($files.Count) parsed cleanly"
```

A non-100% pass rate means the format library has a bug — file an issue with the offending file's name and the `alo_dump` error output.

## What about ALA, mod content, MEGs in MegaFiles.xml?

- `.ala` (animation) files live in the same `models.meg` archives. Use `-Pattern *.ala` to extract them.
- Third-party mod `.meg` files may use Format 2 or 3, or reference encrypted entries via `MegaFiles.xml` precedence rules. Out of scope for v1.

## Legacy `.max` compatibility fixtures (Phase 10)

The legacy max2alamo.dle compatibility regression tests (`test_legacy_*`) require source `.max` files from the original (Max 9-era) Petroglyph plugin toolchain. These ride alongside the vanilla `.alo` corpus under `tests/corpus/legacy/`:

```
tests/corpus/legacy/
└── ThrREv/
    └── Ascendancy/
        ├── Super Battle Droid/CIS_SBD.max
        ├── Snowtrooper/EI_SNOWTROOPER.max
        ├── MC80/RV_MonCalCruiser_D_export4.max
        └── IFTX/EV_IFTX_D.max
```

These are not committed (mod IP). Each developer who wants to run the legacy regression tests copies them from a ThrREv: Ascendancy mod-tools distribution (or any equivalent legacy max2alamo.dle-authored .max files) to the path layout above.

The regression tests run in **loose mode** under `validate_alo.py` because legacy artist-authored content occasionally has degenerate vertex-tangent geometry (e.g. tangent ~ normal at UV seams or zero-area UV triangles) that the strict mode rejects. Loose mode is the same setting `scripts/sweep_corpus_validator.py` uses for the vanilla EaW/FoC corpus — calibrated to real-world content rather than synthetic test scenes.

If your fixtures aren't present, the corresponding `test_legacy_*` tests fail with a clear "fixture missing" message and a pointer back to this doc.
