<#
.SYNOPSIS
  Run the Max-side end-to-end regression tests.

.DESCRIPTION
  Discovers every tests/maxscript/test_*.ms file. For each:

    1. If the .alo output is older than the .ms script OR older than
       the installed max2alamo.dle, invoke 3dsmaxbatch to re-export.
       Otherwise reuse the cached output.
    2. Tier 1 -- universal invariants via validate_alo.py
       (skeleton structure, mesh structure, skinning weights,
       connection sanity). Same checks for every .alo.
    3. Tier 2 -- writer round-trip via alo_roundtrip.exe
       (asserts byte-identical re-emission; catches writer regressions).
    4. Tier 3 -- bespoke verify_<test_name>.py
       (pins the specific feature the test was written to cover).

  All three tiers must pass for the test to be green. Tier 4 (Mike's
  importer / AloViewer / in-game rendering) is the manual checklist
  in docs/build.md; this script doesn't attempt it.

  Each Max boot is ~25 seconds, so the cache saves real time when only
  some tests' inputs changed. Pass -Force to re-export everything.

.PARAMETER Filter
  Wildcard filter on the test base name (e.g. "test_skinned_*").

.PARAMETER Force
  Re-export every test even if its cached output is fresh.

.PARAMETER MaxBatch
  Path to 3dsmaxbatch.exe.

.PARAMETER PluginDle
  Path to the installed plugin (used as a cache invalidator). Defaults
  to the in-repo install dir that the CMake POST_BUILD step writes to.
  Max must be configured (Customize -> Configure System Paths -> 3rd
  Party Plug-Ins) to scan that same folder.

.EXAMPLE
  powershell -File scripts/run-max-tests.ps1

.EXAMPLE
  powershell -File scripts/run-max-tests.ps1 -Filter test_bumpcolorize_* -Force
#>
[CmdletBinding()]
param(
    [string]$Filter   = "*",
    [switch]$Force,
    [string]$MaxBatch = "C:\Program Files\Autodesk\3ds Max 2026\3dsmaxbatch.exe",
    [string]$PluginDle = (Join-Path (Split-Path -Parent $PSScriptRoot) 'plugin\max2alamo.dle')
)

$ErrorActionPreference = 'Stop'

$repoRoot   = Split-Path -Parent $PSScriptRoot
$testDir    = Join-Path $repoRoot 'tests\maxscript'
$verifyDir  = Join-Path $testDir  'verify'
$outDir     = Join-Path $repoRoot 'build\maxbatch'

if (-not (Test-Path $MaxBatch)) {
    Write-Error "3dsmaxbatch.exe not found at: $MaxBatch"
    exit 2
}
if (-not (Test-Path $PluginDle)) {
    Write-Warning "Installed plugin not found at: $PluginDle"
    Write-Warning "Tests will run against whatever max2alamo.dle Max picks up at startup."
}

New-Item -ItemType Directory -Path $outDir -Force | Out-Null

$pluginMtime = if (Test-Path $PluginDle) { (Get-Item $PluginDle).LastWriteTime } else { [DateTime]::MinValue }

$tests = Get-ChildItem -Path $testDir -Filter 'test_*.ms' |
    Where-Object { $_.BaseName -like $Filter } |
    Sort-Object Name

if (-not $tests) {
    Write-Host "No tests matched filter '$Filter' under $testDir"
    exit 0
}

Write-Host "Discovered $($tests.Count) test(s) under tests/maxscript/"
if ($pluginMtime -ne [DateTime]::MinValue) {
    Write-Host "Installed .dle mtime: $pluginMtime"
}
Write-Host ""

$pass = 0
$fail = 0
$failed = @()

foreach ($t in $tests) {
    $name      = $t.BaseName
    $aloPath   = Join-Path $outDir "$name.alo"
    $listenLog = Join-Path $outDir "$name.log"
    $verifyPy  = Join-Path $verifyDir "verify_$name.py"

    Write-Host -NoNewline "[$name] "

    if (-not (Test-Path $verifyPy)) {
        Write-Host "MISSING verifier: $verifyPy" -ForegroundColor Red
        $fail++; $failed += $name
        continue
    }

    $aloFresh = (Test-Path $aloPath) `
                -and ((Get-Item $aloPath).LastWriteTime -ge $t.LastWriteTime) `
                -and ((Get-Item $aloPath).LastWriteTime -ge $pluginMtime)

    if ($Force -or -not $aloFresh) {
        if (Test-Path $aloPath)   { Remove-Item $aloPath }
        if (Test-Path $listenLog) { Remove-Item $listenLog }

        & $MaxBatch $t.FullName -listenerlog $listenLog -v 2 *> $null
        $batchExit = $LASTEXITCODE
        if ($batchExit -ne 0) {
            Write-Host "FAIL (3dsmaxbatch exit=$batchExit, see $listenLog)" -ForegroundColor Red
            $fail++; $failed += $name
            continue
        }
        if (-not (Test-Path $aloPath)) {
            Write-Host "FAIL (no .alo at $aloPath, see $listenLog)" -ForegroundColor Red
            $fail++; $failed += $name
            continue
        }
    } else {
        Write-Host -NoNewline "(cached) "
    }

    # Tier 1: universal invariant validator. Runs on every .alo
    # regardless of which test produced it -- catches structural /
    # skinning / connection regressions that test-specific verifiers
    # might miss. Lives in tests/maxscript/verify/validate_alo.py.
    $validatePy = Join-Path $verifyDir 'validate_alo.py'
    $validateOut = & python $validatePy $aloPath 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL (Tier 1: universal invariants)" -ForegroundColor Red
        $validateOut | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
        $fail++; $failed += $name
        continue
    }

    # Tier 2: writer round-trip. Re-emit the exported .alo through the
    # alo_roundtrip CLI; non-zero exit = the writer's re-serialisation
    # diverges from the original bytes. Catches writer regressions for
    # free -- no per-test boilerplate.
    $roundtripExe = Join-Path $repoRoot 'build\tools\alo_roundtrip\Release\alo_roundtrip.exe'
    if (Test-Path $roundtripExe) {
        $rtOut = & $roundtripExe $aloPath 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "FAIL (Tier 2: alo_roundtrip)" -ForegroundColor Red
            $rtOut | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
            $fail++; $failed += $name
            continue
        }
    } else {
        Write-Warning "alo_roundtrip.exe not found; skipping Tier 2 (build the format library + tools to enable)"
    }

    # Tier 2-ala (Phase 8b): if the exporter dropped a sibling .ala
    # next to the .alo, typed-round-trip it to confirm byte-identity.
    # This step runs only when the test scene actually authored
    # animation (Alamo_Anim_Start/End user props -> at least one
    # animated bone). Existing 24 static tests skip silently.
    $alaPath = [System.IO.Path]::ChangeExtension($aloPath, ".ala")
    if (Test-Path $alaPath) {
        $alaRt = Join-Path $repoRoot 'build\tools\ala_typed_roundtrip\Release\ala_typed_roundtrip.exe'
        if (Test-Path $alaRt) {
            $rtOut = & $alaRt $alaPath 2>&1
            if ($LASTEXITCODE -ne 0) {
                Write-Host "FAIL (Tier 2-ala: ala_typed_roundtrip)" -ForegroundColor Red
                $rtOut | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
                $fail++; $failed += $name
                continue
            }
        }
    }

    # Tier 3: feature-specific verifier (the bespoke verify_<name>.py).
    $verifyOut = & python $verifyPy $aloPath 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "OK   $verifyOut" -ForegroundColor Green
        $pass++
    } else {
        Write-Host "FAIL (Tier 3: $verifyPy)" -ForegroundColor Red
        $verifyOut | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
        $fail++; $failed += $name
    }
}

Write-Host ""
Write-Host "==== Summary: $pass passed, $fail failed ===="
if ($fail -gt 0) {
    Write-Host "Failed: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
exit 0
