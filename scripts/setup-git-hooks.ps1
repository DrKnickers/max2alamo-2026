<#
.SYNOPSIS
  One-time setup: tell this clone to use .githooks/ for git hooks.

.DESCRIPTION
  Git does not propagate hooks via `git clone`; every contributor must
  opt in locally. This script sets `core.hooksPath` to `.githooks/`
  and confirms the active hooks.

  Currently the only hook installed is `pre-commit`, which rejects
  any commit authored or committed with the maintainer's pre-v0.9.1
  private email (history was scrubbed; this prevents recurrence).
  See `.githooks/pre-commit` for the full rule.

.PARAMETER Force
  Overwrite an existing `core.hooksPath` setting without prompting.
#>
[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$hooksDir = Join-Path $repoRoot '.githooks'

if (-not (Test-Path $hooksDir)) {
    Write-Error "No .githooks/ directory at $hooksDir -- is this the right repo?"
    exit 1
}

$current = (git -C $repoRoot config --get core.hooksPath 2>$null)
if ($current -and -not $Force) {
    if ($current -eq '.githooks') {
        Write-Host "core.hooksPath is already set to '.githooks' -- nothing to do." -ForegroundColor Green
    } else {
        Write-Warning "core.hooksPath is already set to '$current' (not '.githooks')."
        Write-Warning "Re-run with -Force to overwrite, or set it manually:"
        Write-Warning "  git config core.hooksPath .githooks"
        exit 1
    }
} else {
    git -C $repoRoot config core.hooksPath '.githooks'
    Write-Host "Set core.hooksPath = '.githooks' for this clone." -ForegroundColor Green
}

Write-Host ""
Write-Host "Active hooks:"
Get-ChildItem -Path $hooksDir -File | ForEach-Object {
    Write-Host "  $($_.Name)"
}
Write-Host ""
Write-Host "Smoke check (pre-commit hook runs cleanly against current author):"
# The hook is a POSIX shell script (#!/bin/sh). On Windows we have
# to dispatch via `sh` explicitly -- PowerShell's `&` invocation on
# an extensionless shebang file would hang waiting for Windows to
# pick an interpreter. `sh` ships with Git for Windows at a
# predictable path; fall back to PATH if the standard location
# isn't there.
$sh = "C:/Program Files/Git/bin/sh.exe"
if (-not (Test-Path $sh)) { $sh = "sh" }
& $sh (Join-Path $hooksDir 'pre-commit')
if ($LASTEXITCODE -eq 0) {
    Write-Host "  pre-commit accepts current identity. OK." -ForegroundColor Green
} else {
    Write-Warning "pre-commit rejected current identity. Fix and re-run."
    exit 1
}
