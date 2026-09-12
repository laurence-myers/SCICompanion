<#
    Compares two folders of decompiled .sc files and reports the ones that
    differ. Two modes:

      -Mode Exact       CRLF-normalized text compare. Use to review a snapshot
                        change: point it at the committed snapshots and the
                        actuals the test wrote.

      -Mode Structural  Strips comments and string literals, canonicalizes
                        identifiers, and reduces each file to its control-flow
                        token stream before comparing. Use for the QfG4 golden
                        diff, where variable names and formatting differ but the
                        structure should match.

    Example (review a snapshot change):
      .\CompareDecompile.ps1 -Expected ..\Files\Decompile\Snapshots\SCI1.1 `
                             -Actual ..\..\Kawa\SnapshotActuals\SCI1.1 -Mode Exact

    Example (golden diff):
      .\CompareDecompile.ps1 -Expected 'E:\Code\Esoteric\sci-scripts\qfg4-cd-dos-1.0\src' `
                             -Actual 'F:\dump' -Mode Structural
#>
param(
    [Parameter(Mandatory = $true)][string]$Expected,
    [Parameter(Mandatory = $true)][string]$Actual,
    [ValidateSet("Exact", "Structural")][string]$Mode = "Exact"
)

$ErrorActionPreference = "Stop"

function Normalize-Exact([string]$text) {
    return ($text -replace "`r", "")
}

function Normalize-Structural([string]$text) {
    # Drop line comments.
    $text = ($text -split "`n" | ForEach-Object { $_ -replace ';.*$', '' }) -join "`n"
    # Drop string, said and braced-text literals.
    $text = $text -replace '"[^"]*"', '""'
    $text = $text -replace "'[^']*'", "''"
    $text = $text -replace '\{[^}]*\}', '{}'
    # Canonicalize identifiers that differ between the two decompilers.
    $text = $text -replace '\b(temp|local|param|global)\d+\b', 'v'
    $text = $text -replace '\bg[A-Z]\w*\b', 'g'
    # Reduce to a token stream: keep parens, operators and control keywords.
    $tokens = [System.Collections.Generic.List[string]]::new()
    foreach ($m in [regex]::Matches($text, '\(|\)|\[|\]|[-+*/=<>!&|]+|\b(if|else|cond|and|or|not|while|repeat|for|break|breakif|continue|contif|return|switch|case)\b')) {
        $tokens.Add($m.Value)
    }
    return ($tokens -join ' ')
}

function Get-Files([string]$dir) {
    Get-ChildItem -Path $dir -Filter *.sc -File | ForEach-Object { $_.Name }
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
    if ($Mode -eq "Exact") {
        $e = Normalize-Exact $e; $a = Normalize-Exact $a
    } else {
        $e = Normalize-Structural $e; $a = Normalize-Structural $a
    }
    if ($e -ne $a) { $changed.Add($name) }
}

Write-Host "Mode: $Mode"
Write-Host "Common: $($common.Count), Changed: $($changed.Count), OnlyExpected: $($onlyExpected.Count), OnlyActual: $($onlyActual.Count)"
if ($changed.Count) { Write-Host "`nChanged:"; $changed | ForEach-Object { Write-Host "  $_" } }
if ($onlyExpected.Count) { Write-Host "`nOnly in expected:"; $onlyExpected | ForEach-Object { Write-Host "  $_" } }
if ($onlyActual.Count) { Write-Host "`nOnly in actual:"; $onlyActual | ForEach-Object { Write-Host "  $_" } }

exit ($changed.Count + $onlyExpected.Count + $onlyActual.Count)
