# build-sfz-index.ps1 — rebuild .sapp-sfz-index.json for the shared sample root.
#
# The plugins read <root>\.sapp-sfz-index.json (root from SAPP_SFZ_ROOT) to fill
# their instrument-select list. Their own rescan lives in the GUI SoundsPanel,
# which never runs in the headless station host — so the index is built here.
#
#   powershell -File build-sfz-index.ps1 [-Root C:\Users\micha\Samples] [-WhatIf]
#
# Only PLAYABLE programs are indexed. SFZ libraries ship fragments meant to be
# #include-d (Sonatina's includes/, Karoryfer's modules/{maps,controls}/); a
# fragment has no <region> of its own and would appear as a dead instrument.
param(
    [string]$Root = $(if ($env:SAPP_SFZ_ROOT) { $env:SAPP_SFZ_ROOT } else { Join-Path $env:USERPROFILE "Samples" }),
    [switch]$WhatIf
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $Root)) { throw "sample root not found: $Root" }

# Directory names that only ever hold #include fragments.
$fragmentDirs = @("includes", "include", "modules", "maps", "controls", "curves")

# Top-level folders to ignore entirely. `sonatina/` is the fetch-library.sh
# clone of the same library that also sits at the root as "Sonatina Symphonic
# Orchestra/"; the two trees are different releases, so a size-based de-dupe
# cannot pair them. Index the root copy and skip the clone.
$excludeTopLevel = @("sonatina")

$all = Get-ChildItem $Root -Recurse -Filter *.sfz -File -ErrorAction SilentlyContinue
Write-Host ("scanning {0} .sfz files under {1}" -f $all.Count, $Root)

$entries = New-Object System.Collections.Generic.List[object]
$skippedFragmentDir = 0; $skippedNoRegion = 0; $skippedExcluded = 0

foreach ($f in $all) {
    $rel = $f.FullName.Substring($Root.Length).TrimStart('\', '/')
    $parts = $rel -split '[\\/]'

    if ($excludeTopLevel -contains $parts[0].ToLower()) { $skippedExcluded++; continue }

    # skip anything living in a fragment directory
    if ($parts[0..($parts.Length - 2)] | Where-Object { $fragmentDirs -contains $_.ToLower() }) {
        $skippedFragmentDir++; continue
    }

    # A playable program either defines regions itself, or pulls them in via
    # #include (Karoryfer's programs are pure <control>/<global> + includes of
    # modules/maps/*.sfz — no literal <region> anywhere in the file).
    $text = Get-Content $f.FullName -Raw -ErrorAction SilentlyContinue
    if (-not ($text -match '<region>' -or $text -match '(?m)^\s*#include')) {
        $skippedNoRegion++; continue
    }

    # label: path without extension, forward slashes (matches the existing index)
    $label = ($rel -replace '\.sfz$', '') -replace '\\', '/'
    $entries.Add([pscustomobject]@{ label = $label; path = $f.FullName })
}

# Some libraries exist twice under the root: the fetch-library.sh copy
# (sonatina/Sonatina Symphonic Orchestra/...) and a hand-extracted copy at the
# top level (Sonatina Symphonic Orchestra/...). Same instrument, two labels —
# de-duplicate on (filename + size) so the select list shows each once. The
# deeper path wins: it carries the library folder, so the label stays
# self-describing.
$before = $entries.Count
$entries = $entries |
    Group-Object { "{0}|{1}" -f (Split-Path $_.path -Leaf).ToLower(), (Get-Item $_.path).Length } |
    ForEach-Object {
        $_.Group | Sort-Object { ($_.label -split '/').Count } -Descending | Select-Object -First 1
    }
$deduped = $before - $entries.Count
if ($deduped -gt 0) { Write-Host ("  de-duplicated {0} entries present under two roots" -f $deduped) }

$sorted = $entries | Sort-Object { $_.label.ToLower() }
Write-Host ("  playable: {0}   skipped: {1} in fragment dirs, {2} with no <region>, {3} in excluded folders" -f `
        $sorted.Count, $skippedFragmentDir, $skippedNoRegion, $skippedExcluded)

$index = [ordered]@{
    sappSfzIndex = 1
    root         = $Root
    order        = "case-insensitive by label; see sapplink manifest instrumentSelect"
    count        = $sorted.Count
    entries      = @($sorted)
}

$out = Join-Path $Root ".sapp-sfz-index.json"
if ($WhatIf) { Write-Host "WhatIf: would write $out"; return }

if (Test-Path $out) { Copy-Item $out "$out.bak" -Force; Write-Host "  previous index backed up to $out.bak" }
# UTF8 without BOM: Set-Content -Encoding UTF8 on Windows PowerShell writes a
# BOM, and every JSON.parse-based reader (the engine's resolver) rejects it.
[IO.File]::WriteAllText($out, ($index | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding $false))
Write-Host ("wrote {0} ({1} instruments)" -f $out, $sorted.Count)
