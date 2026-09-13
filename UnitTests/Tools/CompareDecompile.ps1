<#
    Compares two folders of decompiled .sc files and reports the ones that
    differ. Two modes:

      -Mode Exact       CRLF-normalized text compare. Use to review a snapshot
                        change: point it at the committed snapshots and the
                        actuals the test wrote.

      -Mode Structural  Strips comments and string literals, drops identifiers,
                        and reduces each method to its control-flow token stream
                        before comparing per method. Use for the QfG4 golden
                        diff, where variable names and formatting differ but the
                        structure should match. Reports the methods that differ,
                        not just the files.

    Structural mode maps formatting sugar to a common form so it does not show
    as a difference: an assignment operator (+= etc) to =, breakif/contif to
    break/continue, and the unsigned-comparison prefix is dropped. It does NOT
    expand cond into nested ifs, or for into while plus a step, so those still
    show as differences; that expansion needs the parser (a follow-up).

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

# Reduce one method (or procedure) body to a control-flow token stream.
function Normalize-Structural([string]$text) {
    # Drop string, said and braced-text literals FIRST, so a ';' inside a
    # literal does not truncate the line when comments are stripped next.
    $text = $text -replace '"[^"]*"', '""'
    $text = $text -replace "'[^']*'", "''"
    $text = $text -replace '\{[^}]*\}', '{}'
    # Drop line comments.
    $text = ($text -split "`n" | ForEach-Object { $_ -replace ';.*$', '' }) -join "`n"
    # Map formatting sugar to a common form.
    $text = $text -replace '\bbreakif\b', 'break'
    $text = $text -replace '\bcontif\b', 'continue'
    # A compound assignment operator (+=, -=, |= ...) compares as a plain =.
    $text = $text -replace '(?<![-+*/&|^<>~])([-+*/&|^]|<<|>>)=(?!=)', '='
    # The unsigned-comparison prefix is a type detail, not structure.
    $text = $text -replace '\bu(<=|>=|<|>)', '$1'
    # Reduce to a token stream: parens, operators and control keywords.
    $tokens = [System.Collections.Generic.List[string]]::new()
    foreach ($m in [regex]::Matches($text, '\(|\)|[-+*/=<>!&|~]+|\b(if|else|cond|and|or|not|while|repeat|for|break|continue|return|switch|case)\b')) {
        $tokens.Add($m.Value)
    }
    return ($tokens -join ' ')
}

# Split a file into a map of method key -> body text. A method is
# "(method (name ..." or "(procedure (name ...", keyed by the name; the body
# runs to the next top-level method/procedure. Everything before the first one
# (the header, locals, class properties) is one entry keyed "<header>".
function Split-Methods([string]$text) {
    $text = $text -replace "`r", ""
    $result = [ordered]@{}
    $matches = [regex]::Matches($text, '(?m)^\t?\((?:procedure|method)\s+\(([A-Za-z0-9_]+)')
    if ($matches.Count -eq 0) {
        $result["<header>"] = $text
        return $result
    }
    $result["<header>"] = $text.Substring(0, $matches[0].Index)
    for ($i = 0; $i -lt $matches.Count; $i++) {
        $start = $matches[$i].Index
        $end = if ($i + 1 -lt $matches.Count) { $matches[$i + 1].Index } else { $text.Length }
        $name = $matches[$i].Groups[1].Value
        # A name can repeat (an overridden method in two classes); suffix to keep both.
        $key = $name
        $n = 2
        while ($result.Contains($key)) { $key = "${name}#${n}"; $n++ }
        $result[$key] = $text.Substring($start, $end - $start)
    }
    return $result
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
    if ($Mode -eq "Exact") {
        if ((Normalize-Exact $e) -ne (Normalize-Exact $a)) { $changed.Add($name) }
    }
    else {
        # Compare per method, so one changed or reordered method does not
        # cascade across the whole file.
        $em = Split-Methods $e
        $am = Split-Methods $a
        $methodKeys = @($em.Keys) + @($am.Keys) | Sort-Object -Unique
        foreach ($key in $methodKeys) {
            $eBody = if ($em.Contains($key)) { Normalize-Structural $em[$key] } else { "<absent>" }
            $aBody = if ($am.Contains($key)) { Normalize-Structural $am[$key] } else { "<absent>" }
            if ($eBody -ne $aBody) { $changed.Add("$name :: $key") }
        }
    }
}

Write-Host "Mode: $Mode"
Write-Host "Common files: $($common.Count), Changed: $($changed.Count), OnlyExpected: $($onlyExpected.Count), OnlyActual: $($onlyActual.Count)"
if ($changed.Count) { Write-Host "`nChanged:"; $changed | ForEach-Object { Write-Host "  $_" } }
if ($onlyExpected.Count) { Write-Host "`nOnly in expected:"; $onlyExpected | ForEach-Object { Write-Host "  $_" } }
if ($onlyActual.Count) { Write-Host "`nOnly in actual:"; $onlyActual | ForEach-Object { Write-Host "  $_" } }

exit ($changed.Count + $onlyExpected.Count + $onlyActual.Count)
