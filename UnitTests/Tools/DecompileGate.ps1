<#
    The corpus gate of the decompiler (docs/decompiler-scope-parser/plan.md,
    section 6). For local use: the corpus is a library of Sierra games that
    CI does not have.

    For each game of the library (one folder for each MD5 of resource.map),
    the script:
      1. decompiles the scripts of the sample (default) or every script
         (-Full) with scic, on a copy of the game: scic script decompile
         --engine <Engine> --game-ini none --function-report;
      2. decompiles the whole game with sluicebox's Snuffer, once: the
         output stays in the cache;
      3. compares the two with scic dev compare-structure, and with the
         scic output of an earlier run when -BaselineRun is given.
    The corpus folders stay read-only: scic runs on the copy, and Snuffer
    reads the game folder (a copy when it must leave out a volume file).

    The sample (-Sample, UnitTests\Files\Corpus\gate-sample.json) lists for
    each game (by MD5) the scripts with functions that fell back to asm in
    a sweep, and a fixed random choice of other scripts. -MakeSample makes
    it again, from the CSV of a sweep (-Failures, with the columns Source
    and Script) and the script list of each game.

    The run folder (<Work>\<time>-<process id>) gets, for each game,
    games\<md5>\: src\ (the .sc files of scic), functions.tsv (the function
    report), compare.tsv (the table of compare-structure), and the logs; and
    gate.json (the counts of each game and the totals) and rows.tsv (every
    row of the compare, with its game).

    The cache (-Cache) keeps a copy of the files of each game (games\<md5>,
    about 4 GB for the whole library) and the Snuffer output
    (snuffer\<md5>\src, and status.txt). Delete it to start again.

    -Record writes the counts of the run into gate-baseline.json (-Baseline;
    counts only, no text of a game). -Check compares the run with it, as
    section 6 of the plan says, and exits with 1 when a rule fails:
      1. no function that was source and is now asm (REGRESSED; needs
         -BaselineRun);
      2. no game with more asm functions (with -RequireFewer: also fewer
         in total);
      3. no crash, internal error, timeout or exit code that scic does not
         give;
      5. (with -Allowlist) each function that is source on both sides is
         SAME or NAMES against Snuffer, or its game, script and function
         are in the allowlist (tab-separated: md5, script, function,
         category);
      6. (needs -BaselineRun) a function whose text changed has a verdict
         against Snuffer that is not worse than before (SAME < NAMES <
         SHAPE < DIFF < ASM).
    Rule 4 (the unit tests with SCIC_DECOMPILE_ENGINE) is not here.

    Usage:
      .\UnitTests\Tools\DecompileGate.ps1 -Library F:\Games\Sierra,F:\games\gog -Exclude '_vgm*' -Snuffer <Snuffer.exe>
      .\UnitTests\Tools\DecompileGate.ps1 -Library ... -Snuffer ... -Engine scope -BaselineRun <run folder of classic> -Check
      .\UnitTests\Tools\DecompileGate.ps1 -Library ... -Snuffer ... -Record
      .\UnitTests\Tools\DecompileGate.ps1 -Library ... -MakeSample -Failures <sweep>\functions-verified.csv
#>
param(
    [string[]]$Library,
    [string[]]$Include = @("*"),
    [string[]]$Exclude = @(),
    [int]$Depth = 5,
    [string]$Snuffer = "",
    [ValidateSet("classic", "scope", "auto")]
    [string]$Engine = "classic",
    [switch]$Full,
    [string]$Sample = "",
    [string]$Work = (Join-Path ([IO.Path]::GetTempPath()) "scic-gate"),
    [string]$Cache = "",
    [string]$BaselineRun = "",
    [string]$Baseline = "",
    [switch]$Record,
    [switch]$Check,
    [switch]$RequireFewer,
    [string]$Allowlist = "",
    [string]$Scic = "",
    [int]$Throttle = 4,
    [int]$TimeoutSeconds = 1800,
    [switch]$MakeSample,
    [string]$Failures = "",
    [int]$PerGame = 5,
    [int]$Seed = 20260930
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot 'Corpus.Common.ps1')
if (-not $Library) { throw "-Library is required." }
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Sample) { $Sample = Join-Path $repoRoot "UnitTests\Files\Corpus\gate-sample.json" }
if (-not $Baseline) { $Baseline = Join-Path $repoRoot "UnitTests\Files\Corpus\gate-baseline.json" }
if (-not $Scic) { $Scic = Join-Path $repoRoot "Release\scic.exe" }
$Scic = Get-FullPath $Scic "-Scic"
if (-not (Test-Path -LiteralPath $Scic -PathType Leaf)) { throw "scic.exe not found: $Scic. Build the solution first, or give -Scic." }
$workFull = Get-FullPath $Work "-Work"
if (-not $Cache) { $Cache = Join-Path $workFull "cache" }
$cacheFull = Get-FullPath $Cache "-Cache"
# Start-Process takes the log paths as wildcards.
foreach ($folder in @($workFull, $cacheFull)) {
    if ($folder.IndexOfAny([char[]]'[]*?') -ge 0) { throw "A folder has one of the characters [ ] * ?, which the script cannot use: $folder" }
}
$sources = @()
foreach ($folder in $Library) {
    $libraryPath = Get-FullPath $folder "-Library"
    if (-not (Test-Path -LiteralPath $libraryPath -PathType Container)) { throw "Library folder not found: $folder" }
    foreach ($written in @($workFull, $cacheFull)) {
        if ((Test-Inside $written $libraryPath) -or (Test-Inside $libraryPath $written)) {
            throw "The folder $written and the library folder $libraryPath must not be inside each other."
        }
    }
    $sources += $libraryPath
}

Write-Host "Finding the games..."
$games = @(Find-CorpusGames -Sources $sources -Depth $Depth -Include $Include -Exclude $Exclude -Unique)
if ($games.Count -eq 0) { throw "No game (a folder with resource.map) matched under: $($sources -join ', ')" }

function Write-Utf8([string]$path, [string]$text) {
    [IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
}

# The compiled scripts of a game (scic script list, which writes nothing).
function Get-CompiledScripts([string]$folder) {
    $out = [IO.Path]::GetTempFileName()
    $err = [IO.Path]::GetTempFileName()
    try {
        $code = Invoke-Logged $Scic @("script", "list", $folder, "--format", "tsv", "--data-dir", (Split-Path -Parent $Scic)) $out $err 300
        # 6: a script could not be read; the others are in the list.
        if (@("0", "6") -notcontains $code) { return @() }
        $rows = @(Get-Content -LiteralPath $out | ConvertFrom-Csv -Delimiter "`t")
        return @($rows | Where-Object { $_.in_game -and ($_.in_game -ne "(not compiled)") -and (-not $_.error) } | ForEach-Object { [int]$_.number })
    }
    finally {
        Remove-Item -LiteralPath $out, $err -ErrorAction SilentlyContinue
    }
}

if ($MakeSample) {
    if (-not $Failures) { throw "-MakeSample needs -Failures (the CSV of a sweep, with the columns Source and Script)." }
    $failureScripts = @{}
    foreach ($row in @(Import-Csv -LiteralPath $Failures)) {
        $map = Join-Path $row.Source "resource.map"
        if (-not (Test-Path -LiteralPath $map)) { continue }
        $md5 = (Get-FileHash -LiteralPath $map -Algorithm MD5).Hash.ToLowerInvariant()
        if (-not $failureScripts.ContainsKey($md5)) { $failureScripts[$md5] = @{} }
        $failureScripts[$md5][[int]$row.Script] = $true
    }
    $entries = @()
    foreach ($game in ($games | Sort-Object Name)) {
        $failed = @()
        if ($failureScripts.ContainsKey($game.Md5)) { $failed = @($failureScripts[$game.Md5].Keys | Sort-Object) }
        $compiled = @(Get-CompiledScripts $game.Folder | Where-Object { $failed -notcontains $_ } | Sort-Object)
        # A choice that depends only on the seed and the game.
        $random = New-Object System.Random ($Seed -bxor [Convert]::ToInt32($game.Md5.Substring(0, 7), 16))
        $picked = @($compiled | Sort-Object { $random.Next() } | Select-Object -First $PerGame | Sort-Object)
        $entries += [ordered]@{ name = $game.Name; md5 = $game.Md5; failures = $failed; random = $picked }
        Write-Host ("{0}: {1} with failures, {2} random" -f $game.Name, $failed.Count, $picked.Count)
    }
    $sampleObject = [ordered]@{ seed = $Seed; perGame = $PerGame; failures = (Split-Path -Leaf $Failures); games = $entries }
    New-Item -ItemType Directory (Split-Path -Parent $Sample) -Force | Out-Null
    Write-Utf8 $Sample (($sampleObject | ConvertTo-Json -Depth 5) + "`r`n")
    Write-Host "Wrote $Sample ($($entries.Count) games)."
    exit 0
}

# The scripts of each game: a list of numbers, or "--all".
$scriptsOf = @{}
if ($Full) {
    foreach ($game in $games) { $scriptsOf[$game.Md5] = @("--all") }
}
else {
    if (-not (Test-Path -LiteralPath $Sample)) { throw "No sample: $Sample. Give -Full, or make it with -MakeSample." }
    $sampleObject = Get-Content -LiteralPath $Sample -Raw | ConvertFrom-Json
    foreach ($entry in $sampleObject.games) {
        $numbers = @(@($entry.failures) + @($entry.random) | Where-Object { $_ -ne $null } | ForEach-Object { "$_" } | Sort-Object { [int]$_ } -Unique)
        if ($numbers.Count -gt 0) { $scriptsOf[$entry.md5] = $numbers }
    }
    $missing = @($sampleObject.games | Where-Object { -not ($games | Where-Object Md5 -eq $_.md5) } | ForEach-Object { $_.name })
    if ($missing.Count -gt 0) { Write-Warning "The library has no game of these sample entries: $($missing -join ', ')" }
    $games = @($games | Where-Object { $scriptsOf.ContainsKey($_.Md5) })
}

$stamp = "{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $PID
$run = Join-Path $workFull $stamp
New-Item -ItemType Directory (Join-Path $run "games") -Force | Out-Null
New-Item -ItemType Directory (Join-Path $cacheFull "games"), (Join-Path $cacheFull "snuffer") -Force | Out-Null
Write-Host "scic: $Scic ($Engine, $(if ($Full) { 'every script' } else { 'the sample' }))"
Write-Host "Games: $($games.Count). Run folder: $run. Cache: $cacheFull"

# One game: the work of a runspace. Returns the facts of the game.
$gameWork = {
    param($game, $scripts, $settings)
    $ErrorActionPreference = "Stop"
    . (Join-Path $settings.Tools 'Corpus.Common.ps1')
    $result = [ordered]@{ name = $game.Name; md5 = $game.Md5; exit = ""; bug = $false; snuffer = ""; error = "" }
    try {
        $gameRun = Join-Path $settings.Run "games\$($game.Md5)"
        New-Item -ItemType Directory $gameRun -Force | Out-Null
        # The copy of the game, made once.
        $copy = Join-Path $settings.Cache "games\$($game.Md5)"
        if (-not (Test-Path -LiteralPath (Join-Path $copy "resource.map"))) {
            $partial = "$copy.partial"
            if (Test-Path -LiteralPath $partial) { [IO.Directory]::Delete($partial, $true) }
            Copy-GameFiles $game.Folder $partial
            if (Test-Path -LiteralPath $copy) { [IO.Directory]::Delete($copy, $true) }
            [IO.Directory]::Move($partial, $copy)
        }
        $src = Join-Path $copy "src"
        if (Test-Path -LiteralPath $src) { [IO.Directory]::Delete($src, $true) }

        $arguments = @("script", "decompile", $copy) + @($scripts) + @("--game-ini", "none", "-q", "--engine", $settings.Engine,
            "--function-report", (Join-Path $gameRun "functions.tsv"), "--data-dir", $settings.DataDir)
        $result.exit = Invoke-Logged $settings.Scic $arguments (Join-Path $gameRun "decompile.out.txt") (Join-Path $gameRun "decompile.err.txt") $settings.Timeout
        $log = @(Get-Content -LiteralPath (Join-Path $gameRun "decompile.err.txt"))
        $crashed = @($log | Where-Object { $_ -match '^scic: crash\b' }).Count -gt 0
        $internal = @($log | Where-Object { $_ -match '\[internal\]$' }).Count -gt 0
        # The exit codes of scic for a result (plan section 8); 1 is an internal error.
        $result.bug = ($result.exit -eq "timeout") -or $crashed -or $internal -or (@("0", "2", "3", "5", "6", "7", "8", "9") -notcontains $result.exit)
        New-Item -ItemType Directory (Join-Path $gameRun "src") -Force | Out-Null
        if (Test-Path -LiteralPath $src) {
            Get-ChildItem -LiteralPath $src -Filter "*.sc" -File | ForEach-Object { Move-Item -LiteralPath $_.FullName -Destination (Join-Path $gameRun "src") }
            [IO.Directory]::Delete($src, $true)
        }

        # Snuffer, once for each game. It refuses a game whose map has no
        # entries for a volume file that is present: it then runs on a copy
        # without that file.
        $snufferRoot = Join-Path $settings.Cache "snuffer\$($game.Md5)"
        $status = Join-Path $snufferRoot "status.txt"
        if (-not (Test-Path -LiteralPath $status) -and $settings.Snuffer) {
            if (Test-Path -LiteralPath $snufferRoot) { [IO.Directory]::Delete($snufferRoot, $true) }
            New-Item -ItemType Directory $snufferRoot -Force | Out-Null
            $snufferInput = $game.Folder
            $dropped = @()
            $state = "failed"
            for ($try = 0; $try -lt 12; $try++) {
                $out = Join-Path $snufferRoot "snuffer.out.txt"
                $err = Join-Path $snufferRoot "snuffer.err.txt"
                $code = Invoke-Logged $settings.Snuffer @("-d", $snufferInput, $snufferRoot) $out $err $settings.Timeout
                $text = @(Get-Content -LiteralPath $out) + @(Get-Content -LiteralPath $err)
                $volume = $text | Where-Object { $_ -match 'No map entries for volume (\d+)' } | Select-Object -First 1
                if ($volume -and ($volume -match 'No map entries for volume (\d+)')) {
                    $name = 'resource.{0:000}' -f [int]$Matches[1]
                    if ($snufferInput -eq $game.Folder) {
                        $snufferInput = Join-Path $snufferRoot "game"
                        Copy-GameFiles $game.Folder $snufferInput
                    }
                    $file = Join-Path $snufferInput $name
                    if (-not (Test-Path -LiteralPath $file)) { break }
                    [IO.File]::Delete($file)
                    $dropped += $name
                    continue
                }
                if ((Test-Path -LiteralPath (Join-Path $snufferRoot "src")) -and @(Get-ChildItem -LiteralPath (Join-Path $snufferRoot "src") -Filter *.sc).Count -gt 0) {
                    $state = "ok"
                }
                else {
                    $state = "failed: exit $code"
                }
                break
            }
            if ($snufferInput -ne $game.Folder) { [IO.Directory]::Delete($snufferInput, $true) }
            if ($dropped.Count -gt 0) { $state += " (without $($dropped -join ', '))" }
            Set-Content -LiteralPath $status -Value $state -Encoding UTF8
        }
        $result.snuffer = if (Test-Path -LiteralPath $status) { (Get-Content -LiteralPath $status -TotalCount 1) } else { "not run" }
        $expected = Join-Path $snufferRoot "src"
        if (-not (Test-Path -LiteralPath $expected)) {
            # No Snuffer output: every function is ONLY-ACTUAL.
            $expected = Join-Path $gameRun "no-snuffer"
            New-Item -ItemType Directory $expected -Force | Out-Null
        }

        $compare = @("dev", "compare-structure", $expected, (Join-Path $gameRun "src"), "--out", (Join-Path $gameRun "compare.tsv"))
        if ($settings.BaselineRun) {
            $baselineSrc = Join-Path $settings.BaselineRun "games\$($game.Md5)\src"
            if (Test-Path -LiteralPath $baselineSrc) { $compare += @("--baseline", $baselineSrc) }
        }
        $compareExit = Invoke-Logged $settings.Scic $compare (Join-Path $gameRun "compare.out.txt") (Join-Path $gameRun "compare.err.txt") $settings.Timeout
        if (@("0", "6") -notcontains $compareExit) { $result.error = "compare-structure: exit $compareExit" }
    }
    catch {
        $result.error = "$_"
    }
    return [pscustomobject]$result
}

$settings = @{
    Tools = $PSScriptRoot; Run = $run; Cache = $cacheFull; Scic = $Scic; DataDir = (Split-Path -Parent $Scic); Engine = $Engine
    Snuffer = $(if ($Snuffer) { Get-FullPath $Snuffer "-Snuffer" } else { "" }); BaselineRun = $(if ($BaselineRun) { Get-FullPath $BaselineRun "-BaselineRun" } else { "" })
    Timeout = $TimeoutSeconds
}
$pool = [runspacefactory]::CreateRunspacePool(1, [Math]::Max(1, $Throttle))
$pool.Open()
$jobs = @()
foreach ($game in $games) {
    $shell = [powershell]::Create()
    $shell.RunspacePool = $pool
    [void]$shell.AddScript($gameWork).AddArgument($game).AddArgument($scriptsOf[$game.Md5]).AddArgument($settings)
    $jobs += [pscustomobject]@{ Shell = $shell; Handle = $shell.BeginInvoke(); Game = $game }
}
$facts = @()
$done = 0
foreach ($job in $jobs) {
    $output = $job.Shell.EndInvoke($job.Handle)
    $job.Shell.Dispose()
    $fact = $output | Select-Object -Last 1
    if (-not $fact) { $fact = [pscustomobject]@{ name = $job.Game.Name; md5 = $job.Game.Md5; exit = ""; bug = $false; snuffer = ""; error = "no result" } }
    $facts += $fact
    $done++
    Write-Host ("[{0}/{1}] {2}: decompile {3}{4}; snuffer {5}{6}" -f $done, $jobs.Count, $fact.name, $fact.exit, $(if ($fact.bug) { " (BUG)" } else { "" }), $fact.snuffer, $(if ($fact.error) { "; error: $($fact.error)" } else { "" }))
}
$pool.Close()

# The counts of each game, from its function report and its compare table.
$verdictNames = @("SAME", "NAMES", "SHAPE", "DIFF", "ASM", "SOURCE", "BOTH-ASM", "ONLY-EXPECTED", "ONLY-ACTUAL")
$changeNames = @("FIXED", "CHANGED", "REGRESSED", "ADDED", "REMOVED")
$rank = @{ "SAME" = 0; "NAMES" = 1; "SHAPE" = 2; "DIFF" = 3; "ASM" = 4 }
$allowed = @{}
if ($Allowlist) {
    foreach ($line in @(Get-Content -LiteralPath $Allowlist | Where-Object { $_ -and -not $_.StartsWith("#") })) {
        $fields = $line -split "`t"
        if ($fields.Count -ge 3) { $allowed["$($fields[0])`t$($fields[1])`t$($fields[2])"] = $true }
    }
}
$gameCounts = @()
$allRows = New-Object System.Collections.Generic.List[string]
$allRows.Add("game`tmd5`tscript`tkey`tfunction`tverdict`tbaseline`tchange")
$ruleFailures = New-Object System.Collections.Generic.List[string]
foreach ($fact in ($facts | Sort-Object name)) {
    $gameRun = Join-Path $run "games\$($fact.md5)"
    $functions = @()
    $report = Join-Path $gameRun "functions.tsv"
    if (Test-Path -LiteralPath $report) { $functions = @(Get-Content -LiteralPath $report | ConvertFrom-Csv -Delimiter "`t") }
    $decompiled = @{}
    foreach ($f in $functions) { $decompiled[[int]$f.script] = $true }
    $rows = @()
    $table = Join-Path $gameRun "compare.tsv"
    if (Test-Path -LiteralPath $table) {
        # Only the scripts that scic decompiled: Snuffer decompiled every
        # script of the game.
        $rows = @(Get-Content -LiteralPath $table | ConvertFrom-Csv -Delimiter "`t" | Where-Object { $decompiled.ContainsKey([int]$_.script) })
    }
    $verdicts = [ordered]@{}
    foreach ($name in $verdictNames) { $verdicts[$name] = @($rows | Where-Object verdict -eq $name).Count }
    $changes = [ordered]@{}
    foreach ($name in $changeNames) { $changes[$name] = @($rows | Where-Object change -eq $name).Count }
    foreach ($row in $rows) {
        $allRows.Add("$($fact.name)`t$($fact.md5)`t$($row.script)`t$($row.key)`t$($row.function)`t$($row.verdict)`t$($row.baseline)`t$($row.change)")
        if ($row.change -eq "REGRESSED") { $ruleFailures.Add("rule 1: $($fact.name) script $($row.script) $($row.function) was source and is now asm") }
        if (($row.change -eq "CHANGED") -and $rank.ContainsKey($row.verdict) -and $rank.ContainsKey($row.baseline) -and ($rank[$row.verdict] -gt $rank[$row.baseline])) {
            $ruleFailures.Add("rule 6: $($fact.name) script $($row.script) $($row.function) changed from $($row.baseline) to $($row.verdict)")
        }
        if ($Allowlist -and (@("SHAPE", "DIFF") -contains $row.verdict) -and -not $allowed.ContainsKey("$($fact.md5)`t$($row.script)`t$($row.function)")) {
            $ruleFailures.Add("rule 5: $($fact.name) script $($row.script) $($row.function) is $($row.verdict) and not in the allowlist")
        }
    }
    if ($fact.bug -or $fact.error) { $ruleFailures.Add("rule 3: $($fact.name): decompile exit $($fact.exit)$(if ($fact.error) { ", $($fact.error)" })") }
    $gameCounts += [ordered]@{
        name = $fact.name; md5 = $fact.md5; exit = $fact.exit; snuffer = $fact.snuffer
        functions = $functions.Count
        asm = @($functions | Where-Object output -eq "asm").Count
        corrupt = @($functions | Where-Object output -eq "corrupt").Count
        scopeOk = @($functions | Where-Object scope -eq "ok").Count
        verdicts = $verdicts; changes = $changes
    }
}
$totals = [ordered]@{ games = $gameCounts.Count }
foreach ($name in @("functions", "asm", "corrupt", "scopeOk")) { $totals[$name] = ($gameCounts | ForEach-Object { $_[$name] } | Measure-Object -Sum).Sum }
$totals.verdicts = [ordered]@{}
foreach ($name in $verdictNames) { $totals.verdicts[$name] = ($gameCounts | ForEach-Object { $_.verdicts[$name] } | Measure-Object -Sum).Sum }
$totals.changes = [ordered]@{}
foreach ($name in $changeNames) { $totals.changes[$name] = ($gameCounts | ForEach-Object { $_.changes[$name] } | Measure-Object -Sum).Sum }
$mode = if ($Full) { "full" } else { "sample" }
$gate = [ordered]@{ engine = $Engine; mode = $mode; date = (Get-Date -Format "yyyy-MM-dd"); totals = $totals; games = $gameCounts }
Write-Utf8 (Join-Path $run "gate.json") (($gate | ConvertTo-Json -Depth 6) + "`r`n")
Write-Utf8 (Join-Path $run "rows.tsv") (($allRows -join "`r`n") + "`r`n")

Write-Host ""
Write-Host ("Functions: {0}; asm: {1}; corrupt: {2}; scope ok: {3}" -f $totals.functions, $totals.asm, $totals.corrupt, $totals.scopeOk)
Write-Host ("Verdicts: " + (($verdictNames | ForEach-Object { "$_ $($totals.verdicts[$_])" }) -join ", "))
if ($BaselineRun) { Write-Host ("Changes: " + (($changeNames | ForEach-Object { "$_ $($totals.changes[$_])" }) -join ", ")) }
Write-Host "Run folder: $run"

if ($Record) {
    # Counts only: no text of a game.
    $recorded = [ordered]@{ engine = $Engine; mode = $mode; date = $gate.date; totals = $totals; games = @($gameCounts | ForEach-Object {
        [ordered]@{ name = $_.name; md5 = $_.md5; functions = $_.functions; asm = $_.asm; corrupt = $_.corrupt; verdicts = $_.verdicts } }) }
    New-Item -ItemType Directory (Split-Path -Parent $Baseline) -Force | Out-Null
    Write-Utf8 $Baseline (($recorded | ConvertTo-Json -Depth 6) + "`r`n")
    Write-Host "Recorded $Baseline."
}

if ($Check) {
    if (-not (Test-Path -LiteralPath $Baseline)) { throw "No baseline to check against: $Baseline" }
    $base = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json
    if ($base.mode -ne $mode) { throw "The baseline is of the $($base.mode) mode, and this run of the $mode mode." }
    foreach ($game in $gameCounts) {
        $before = $base.games | Where-Object md5 -eq $game.md5 | Select-Object -First 1
        if ($before -and ($game.asm -gt $before.asm)) { $ruleFailures.Add("rule 2: $($game.name) has $($game.asm) asm functions (baseline $($before.asm))") }
    }
    # The total of the baseline over the games of this run (-Include can choose some).
    $runMd5 = @($gameCounts | ForEach-Object { $_.md5 })
    $baseAsm = (@($base.games | Where-Object { $runMd5 -contains $_.md5 }) | Measure-Object -Property asm -Sum).Sum
    if ($RequireFewer -and ($totals.asm -ge $baseAsm)) { $ruleFailures.Add("rule 2: $($totals.asm) asm functions in total (baseline $baseAsm); the gate needs fewer") }
    if (-not $BaselineRun) { Write-Host "Rules 1 and 6 need -BaselineRun: not checked." }
    if ($ruleFailures.Count -gt 0) {
        Write-Host ""
        Write-Host "The gate fails:"
        $ruleFailures | Select-Object -First 200 | ForEach-Object { Write-Host "  $_" }
        if ($ruleFailures.Count -gt 200) { Write-Host "  ... $($ruleFailures.Count - 200) more" }
        exit 1
    }
    Write-Host "The gate passes."
}
exit 0
