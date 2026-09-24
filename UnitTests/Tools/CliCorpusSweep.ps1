<#
    Runs scic.exe over a library of SCI games, and writes a CSV of the
    results (docs/scic-cli/plan.md, section 10). For local use: CI does not
    run it.

    Each folder under -Source (to -Depth levels down) that holds a
    resource.map is a game. For each game, the script copies the files of
    that folder into a new run folder, clears their read-only attribute,
    and runs these commands in the copy:

      scic script list <copy>
      scic script decompile <copy> --all
      scic script compile <copy> --all

    It copies no subfolder: they hold DOSBox, ScummVM, saves, CD audio and
    the user's src\. GameSession::Open reads AUDIO\ and AUD\ to find the
    audio format, but the script commands do not use it (an A/B test on
    LSL6 gave the same output and the same files with and without AUD\).

    The script only reads the source folders. Do not use junctions in place
    of the copies: decompile writes src\ and game.ini, and compile writes
    patch files. -Work and -Source must not be inside each other; the
    script compares the paths as text, so do not give a junction or a
    subst drive that points into the other. Each copy is removed after its
    game, unless you give -Keep.

    The run folder (<Work>\<time>) gets sweep.csv and logs\. The CSV has one
    row for each game and command, written as the sweep goes: the game
    folder, the command, the exit code ("timeout" when the command ran
    longer than -TimeoutSeconds; "skipped" for a compile after a decompile
    that was a bug), the seconds, the count of error lines ("scic: error:",
    "scic: crash" and "path(line,col): error :"), the error codes ("format",
    "io", ...), the summary of the report, whether the row is a bug of
    scic, and the log file (stdout, then stderr).

    A bug of scic: a command that timed out; that printed a crash line or
    an "internal" error; or that ended with an exit code that scic does not
    give for a result (not 0, 2, 3, 5, 6, 7, 8 or 9: for example 1, or a
    crash code such as -1073740791, 0xC0000409, from a crash that the crash
    filter did not see). The script then exits with 1. Other exit codes
    (for example 5 for compile errors) can come from the game, so read the
    CSV.

    Usage:
      .\UnitTests\Tools\CliCorpusSweep.ps1 -Source 'F:\Games\Sierra', 'F:\games\gog' -Exclude '* - dev*'
      .\UnitTests\Tools\CliCorpusSweep.ps1 -Source 'F:\games\gog' -Include 'Space Quest*' -Keep
#>
param(
    [Parameter(Mandatory = $true)][string[]]$Source,
    # The folder for the run folders. Default: the temp folder.
    [string]$Work = (Join-Path ([IO.Path]::GetTempPath()) "scic-sweep"),
    # Default: Release\scic.exe of this repository.
    [string]$Scic = "",
    # Wildcards for the game folder, as a path under its source folder
    # (for example "Police Quest 3\ega").
    [string[]]$Include = @("*"),
    [string[]]$Exclude = @(),
    # How many folder levels under a source folder to search for games.
    [int]$Depth = 5,
    [int]$TimeoutSeconds = 900,
    [switch]$Keep
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# A path as the file system sees it: a relative path starts at the current
# PowerShell folder (Set-Location), not at the folder of the process, and a
# UNC path has no "Microsoft.PowerShell.Core\FileSystem::" in front.
function Get-FullPath([string]$path) {
    $full = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($path)
    return [IO.Path]::GetFullPath($full).TrimEnd('\')
}

function Test-Inside([string]$path, [string]$folder) {
    return ($path -eq $folder) -or $path.StartsWith($folder + "\", [StringComparison]::OrdinalIgnoreCase)
}

if (-not $Scic) { $Scic = Join-Path $repoRoot "Release\scic.exe" }
$Scic = Get-FullPath $Scic
if (-not (Test-Path -LiteralPath $Scic -PathType Leaf)) { throw "scic.exe not found: $Scic. Build the solution first, or give -Scic." }

# The run folder must not be inside a source folder, and no source folder
# may be inside the run folder (the script removes the copies).
$workFull = Get-FullPath $Work
$sources = @()
foreach ($folder in $Source) {
    $full = Get-FullPath $folder
    if (-not (Test-Path -LiteralPath $full -PathType Container)) { throw "Source folder not found: $folder" }
    if ((Test-Inside $workFull $full) -or (Test-Inside $full $workFull)) {
        throw "The work folder ($workFull) and a source folder ($full) must not be inside each other."
    }
    $sources += $full
}

# Find the games.
$games = @()
foreach ($root in $sources) {
    $maps = Get-ChildItem -LiteralPath $root -Recurse -Depth $Depth -File -Filter "resource.map" -ErrorAction SilentlyContinue
    foreach ($folder in @($maps | ForEach-Object { $_.Directory.FullName } | Sort-Object -Unique)) {
        $name = $folder.Substring($root.Length).TrimStart('\')
        if (-not $name) { $name = Split-Path -Leaf $root }
        $included = @($Include | Where-Object { $name -like $_ }).Count -gt 0
        $excluded = @($Exclude | Where-Object { $name -like $_ }).Count -gt 0
        if ($included -and -not $excluded) {
            $games += [pscustomobject]@{ Name = $name; Folder = $folder }
        }
    }
}
if ($games.Count -eq 0) { throw "No game (a folder with resource.map) matched under: $($sources -join ', ')" }

$run = Join-Path $workFull (Get-Date -Format "yyyyMMdd-HHmmss")
$logs = Join-Path $run "logs"
New-Item -ItemType Directory -Force $logs | Out-Null
$csv = Join-Path $run "sweep.csv"
Write-Host "scic: $Scic"
Write-Host "Games: $($games.Count). Run folder: $run"

$commands = @(
    @{ Name = "list"; Arguments = @("script", "list") },
    @{ Name = "decompile"; Arguments = @("script", "decompile", "--all") },
    @{ Name = "compile"; Arguments = @("script", "compile", "--all") }
)
$errorCodes = "format|unsupported|not-found|io|compile|write-refused|usage|cancelled|internal"
# The exit codes of scic for a result (plan section 8). 1 is an internal
# error, so it is not here.
$resultCodes = @("0", "2", "3", "5", "6", "7", "8", "9")
$rows = @()
$bugs = 0

# Writes one row of the CSV at once, so a sweep that stops keeps its rows.
function Add-Row($row) {
    $script:rows += $row
    $row | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8 -Append
}

# Runs one command of scic on a copy; returns its row.
function Invoke-ScicCommand([string]$game, [string]$copy, [string]$id, $command) {
    # The game folder is the argument after the command's words.
    $arguments = @($command.Arguments[0], $command.Arguments[1], "`"$copy`"") + @($command.Arguments | Select-Object -Skip 2)
    $out = Join-Path $logs "$id.$($command.Name).out.txt"
    $err = Join-Path $logs "$id.$($command.Name).err.txt"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $Scic -ArgumentList ($arguments -join " ") -NoNewWindow -PassThru `
        -RedirectStandardOutput $out -RedirectStandardError $err
    # Windows PowerShell gives no ExitCode unless the handle was read.
    $null = $process.Handle
    if ($process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.WaitForExit()
        $exitCode = "$($process.ExitCode)"
    }
    else {
        $process.Kill()
        $process.WaitForExit()
        $exitCode = "timeout"
    }
    $seconds = [Math]::Round($watch.Elapsed.TotalSeconds, 1)

    $stdout = @(Get-Content -LiteralPath $out)
    $stderr = @(Get-Content -LiteralPath $err)
    $all = $stdout + $stderr
    $log = Join-Path $logs "$id.$($command.Name).log"
    Set-Content -LiteralPath $log -Value $all -Encoding UTF8
    Remove-Item -LiteralPath $out, $err

    $errors = @($all | Where-Object { $_ -match '^scic: (error|crash)\b|\): error : ' }).Count
    $crashed = @($all | Where-Object { $_ -match '^scic: crash\b' }).Count -gt 0
    $codes = @($all | ForEach-Object { if ($_ -match "\[($errorCodes)\]$") { $Matches[1] } } | Sort-Object -Unique)
    if ($command.Name -eq "list") {
        $summary = "$(@($stdout | Where-Object { $_ -match '^\s*\d+\s' }).Count) scripts"
    }
    else {
        $summary = @($stderr | Where-Object { $_ -match '^(Decompiled|Compiled|Wrote|Would write) ' } | Select-Object -First 1) -join ""
    }
    $bug = ($exitCode -eq "timeout") -or $crashed -or ($codes -contains "internal") -or ($resultCodes -notcontains $exitCode)
    return [pscustomobject]@{
        Game = $game
        Command = $command.Name
        ExitCode = $exitCode
        Seconds = $seconds
        Errors = $errors
        Codes = ($codes -join " ")
        Summary = $summary
        Bug = $(if ($bug) { "yes" } else { "" })
        Log = $log
    }
}

$index = 0
foreach ($game in $games) {
    $index++
    $id = "{0:D3}" -f $index
    # A short copy folder: scic has a MAX_PATH limit.
    $copy = Join-Path $run $id
    $line = "[$id/$($games.Count)] $($game.Name):"
    try {
        New-Item -ItemType Directory $copy | Out-Null
        foreach ($file in @(Get-ChildItem -LiteralPath $game.Folder -File)) {
            $target = Join-Path $copy $file.Name
            Copy-Item -LiteralPath $file.FullName -Destination $target
            $copied = Get-Item -LiteralPath $target
            $copied.Attributes = $copied.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly)
        }
        $decompileBug = $false
        foreach ($command in $commands) {
            if (($command.Name -eq "compile") -and $decompileBug) {
                # A compile of a decompile that crashed or timed out tells
                # nothing.
                $row = [pscustomobject]@{ Game = $game.Folder; Command = "compile"; ExitCode = "skipped"; Seconds = 0; Errors = 0; Codes = ""; Summary = "the decompile was a bug"; Bug = ""; Log = "" }
            }
            else {
                $row = Invoke-ScicCommand $game.Folder $copy $id $command
            }
            Add-Row $row
            if ($row.Bug) {
                $bugs++
                if ($command.Name -eq "decompile") { $decompileBug = $true }
            }
            $line += " $($command.Name) $($row.ExitCode)"
            if ($row.ExitCode -ne "skipped") { $line += " ($($row.Seconds) s)" }
        }
    }
    catch {
        # The sweep goes on with the next game; the row names the failure.
        Add-Row ([pscustomobject]@{ Game = $game.Folder; Command = "sweep"; ExitCode = "error"; Seconds = 0; Errors = 1; Codes = ""; Summary = "$_"; Bug = ""; Log = "" })
        $line += " sweep error: $_"
    }
    finally {
        if (-not $Keep -and (Test-Path -LiteralPath $copy)) { Remove-Item -LiteralPath $copy -Recurse -Force }
    }
    Write-Host $line
}

Write-Host ""
$rows | Group-Object Command, ExitCode | Sort-Object Name | ForEach-Object { Write-Host ("{0,-20} {1}" -f $_.Name, $_.Count) }
Write-Host "CSV: $csv"
if ($bugs -gt 0) {
    Write-Host "$bugs commands were bugs of scic: a crash, exit code 1 or another code that scic does not give, an internal error, or a timeout. See the Bug column."
    exit 1
}
exit 0
