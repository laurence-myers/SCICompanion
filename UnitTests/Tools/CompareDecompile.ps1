<#
    Compares two folders of decompiled .sc files and reports the ones that
    differ, as CRLF-normalized text. Use it to review a snapshot change: point
    it at the committed snapshots and the actuals the test wrote.

    For the QfG4 golden diff, where variable names and formatting differ but
    the structure should match, use the C++ structural compare instead:
    DiagnosticDumps::Compare_Structural (see UnitTests\README.md). It parses
    both sides with the real parser and compares per function.

    Example:
      .\CompareDecompile.ps1 -Expected ..\Files\Decompile\Snapshots\SCI1.1 `
                             -Actual ..\..\Release\SnapshotActuals\SCI1.1
#>
param(
    [Parameter(Mandatory = $true)][string]$Expected,
    [Parameter(Mandatory = $true)][string]$Actual
)

$ErrorActionPreference = "Stop"

function Normalize-Exact([string]$text) {
    return ($text -replace "`r", "")
}

function Get-Files([string]$dir) {
    Get-ChildItem -Path $dir -File | Where-Object { $_.Extension -eq '.sc' } | ForEach-Object { $_.Name }
}

$expectedFiles = @{}
Get-Files $Expected | ForEach-Object { $expectedFiles[$_] = $true }
$actualFiles = @{}
Get-Files $Actual | ForEach-Object { $actualFiles[$_] = $true }

$onlyExpected = $expectedFiles.Keys | Where-Object { -not $actualFiles.ContainsKey($_) } | Sort-Object
$onlyActual = $actualFiles.Keys | Where-Object { -not $expectedFiles.ContainsKey($_) } | Sort-Object
$common = $expectedFiles.Keys | Where-Object { $actualFiles.ContainsKey($_) } | Sort-Object

$changed = [System.Collections.Generic.List[string]]::new()
foreach ($name in $common) {
    $e = Get-Content -Raw -Path (Join-Path $Expected $name)
    $a = Get-Content -Raw -Path (Join-Path $Actual $name)
    if ((Normalize-Exact $e) -ne (Normalize-Exact $a)) { $changed.Add($name) }
}

Write-Host "Common files: $($common.Count), Changed: $($changed.Count), OnlyExpected: $($onlyExpected.Count), OnlyActual: $($onlyActual.Count)"
if ($changed.Count) { Write-Host "`nChanged:"; $changed | ForEach-Object { Write-Host "  $_" } }
if ($onlyExpected.Count) { Write-Host "`nOnly in expected:"; $onlyExpected | ForEach-Object { Write-Host "  $_" } }
if ($onlyActual.Count) { Write-Host "`nOnly in actual:"; $onlyActual | ForEach-Object { Write-Host "  $_" } }
exit ($changed.Count + $onlyExpected.Count + $onlyActual.Count)
