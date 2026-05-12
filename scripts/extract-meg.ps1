# Extract files from a MEG archive (Empire at War / Forces of Corruption).
#
# Spec: https://modtools.petrolution.net/docs/MegFileFormat
#
# Three MEG formats exist. This script supports **Format 1 only** -- used by
# vanilla EaW and FoC. Format 2 (16-byte header with encryption flags) and
# Format 3 (AES-128 CBC, used in Grey Goo / 8-Bit Armies / The Great War:
# Western Front) are detected and rejected with a clear error.
#
# Format 1 layout:
#   uint32  numFilenames
#   uint32  numFiles                       (== numFilenames for Format 1)
#   for each filename:
#     uint16 length
#     char[length] name (ASCII, NOT null-terminated)
#   for each file record (numFiles times, 20 bytes each, sorted by CRC):
#     uint32 crc
#     uint32 fileTableIndex
#     uint32 fileSize
#     uint32 fileOffset
#     uint32 filenameIndex
#   <file data at the given offsets>
#
# Format detection: if the first uint32 is 0xFFFFFFFF, it's Format 2 or 3 --
# both start with that sentinel. Format 1 starts directly with the filename
# count, which is always small relative to UINT32_MAX.
#
# Usage:
#   pwsh -File extract-meg.ps1 -MegPath <path> -OutDir <dir> [-Pattern *.alo] [-MaxFiles 0]
#
# The corpus extracted by this script lives under tests/corpus/ and is
# gitignored -- it contains Lucasfilm IP. See docs/corpus.md.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $MegPath,
    [Parameter(Mandatory = $true)] [string] $OutDir,
    [string] $Pattern = "*.alo",
    [int] $MaxFiles = 0
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $MegPath)) { throw "MEG file not found: $MegPath" }
if (-not (Test-Path $OutDir)) { $null = New-Item -ItemType Directory -Path $OutDir -Force }

$bytes = [System.IO.File]::ReadAllBytes($MegPath)
if ($bytes.Length -lt 8) { throw "File too small to be a MEG: $MegPath" }

$br = New-Object System.IO.BinaryReader -ArgumentList @([System.IO.MemoryStream]::new($bytes))

# --- Format detection ---
$firstWord = $br.ReadUInt32()
if ($firstWord -eq 0xFFFFFFFF) {
    throw @"
$MegPath
This is a Format 2 or Format 3 MEG (encrypted-capable). Only Format 1
(used by vanilla Empire at War and Forces of Corruption) is supported.

If you're trying to extract from a newer Petroglyph title (Grey Goo,
8-Bit Armies, The Great War), you'll need an extractor that handles
AES-128 CBC. See https://modtools.petrolution.net/docs/MegFileFormat
"@
}

# Sanity bound: a Format 1 numFilenames should be modest.
if ($firstWord -gt 1000000) {
    throw "Implausible numFilenames=$firstWord -- file likely not a Format 1 MEG."
}

$numFilenames = $firstWord
$numFiles = $br.ReadUInt32()

Write-Host "MEG: $MegPath"
Write-Host "  format       = 1 (vanilla EaW/FoC)"
Write-Host "  numFilenames = $numFilenames"
Write-Host "  numFiles     = $numFiles"

if ($numFilenames -ne $numFiles) {
    Write-Warning "numFilenames != numFiles. Format 1 expects equality; results may be unreliable."
}

# --- Filename table ---
$filenames = New-Object string[] $numFilenames
for ($i = 0; $i -lt $numFilenames; $i++) {
    $len = $br.ReadUInt16()
    $nameBytes = $br.ReadBytes($len)
    $filenames[$i] = [System.Text.Encoding]::ASCII.GetString($nameBytes)
}

# --- File records (20 bytes each) ---
$records = New-Object System.Collections.ArrayList
for ($i = 0; $i -lt $numFiles; $i++) {
    [void] $records.Add([PSCustomObject]@{
        CRC        = $br.ReadUInt32()
        TableIndex = $br.ReadUInt32()
        Size       = $br.ReadUInt32()
        Offset     = $br.ReadUInt32()
        NameIndex  = $br.ReadUInt32()
    })
}

# --- Filter ---
$matches = @()
foreach ($rec in $records) {
    $name = $filenames[$rec.NameIndex]
    $basename = ($name -split "[\\/]")[-1]
    if ($basename -like $Pattern) {
        $matches += [PSCustomObject]@{ Record = $rec; Name = $name; Basename = $basename }
    }
}

Write-Host "  matched      = $($matches.Count) files for pattern '$Pattern'"

if ($MaxFiles -gt 0 -and $matches.Count -gt $MaxFiles) {
    $matches = $matches | Select-Object -First $MaxFiles
    Write-Host "  capped to $MaxFiles"
}

# --- Extract ---
# Output is flat (basename only). For Format 1 / vanilla content, paths are
# backslash-separated and represent virtual locations within the archive.
$extracted = 0
foreach ($m in $matches) {
    $rec = $m.Record
    $outPath = Join-Path $OutDir $m.Basename
    $data = [byte[]]::new($rec.Size)
    [Array]::Copy($bytes, $rec.Offset, $data, 0, $rec.Size)
    [System.IO.File]::WriteAllBytes($outPath, $data)
    $extracted++
}
Write-Host "  extracted    = $extracted files into $OutDir"
