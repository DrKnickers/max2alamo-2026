<#
.SYNOPSIS
  Run the Max-side end-to-end regression tests.

.DESCRIPTION
  Discovers every tests/maxscript/test_*.ms file. For each:

    1. If the .alo output is older than the .ms script OR older than
       the installed max2alamo.dle, invoke 3dsmaxbatch to re-export.
       Otherwise reuse the cached output.
    2. Run tests/maxscript/verify/verify_<test_name>.py against the
       resulting .alo. Pass if it exits 0.

  Each Max boot is ~25 seconds, so the cache saves real time when only
  some tests' inputs changed. Pass -Force to re-export everything.

.PARAMETER Filter
  Wildcard filter on the test base name (e.g. "test_skinned_*").

.PARAMETER Force
  Re-export every test even if its cached output is fresh.

.PARAMETER MaxBatch
  Path to 3dsmaxbatch.exe.

.PARAMETER PluginDle
  Path to the installed plugin (used as a cache invalidator).

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
    [string]$PluginDle = "C:\Program Files\Autodesk\3ds Max 2026\Plugins\max2alamo.dle"
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

    $verifyOut = & python $verifyPy $aloPath 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "OK   $verifyOut" -ForegroundColor Green
        $pass++
    } else {
        Write-Host "FAIL" -ForegroundColor Red
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
