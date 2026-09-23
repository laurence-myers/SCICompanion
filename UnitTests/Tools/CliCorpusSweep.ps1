<#
    Runs scic.exe over a library of SCI games, and writes a CSV of the
    results (docs/scic-cli/plan.md, section 10). For local use: CI does not
    run it.

    Each folder under -Source that holds a resource.map is a game. For each
    game, the script copies the files of that folder into a new run folder
    (not its subfolders, which hold DOSBox, CD audio and the like: the
    script commands read only the game folder), clears their read-only
    attribute, and runs these commands in the copy:

      scic script list <copy>
      scic script decompile <copy> --all
      scic script compile <copy> --all

    The script only reads the source folders. Do not use junctions in place
    of the copies: decompile writes src\ and game.ini, and compile writes
    patch files. Each copy is removed after its game, unless you give -Keep.

    The run folder (<Work>\<time>) gets sweep.csv and logs\. The CSV has one
    row for each game and command: the game folder, the command, the exit
    code ("timeout" when the command ran longer than -TimeoutSeconds), the
    seconds, the count of error lines ("scic: error:", "scic: crash" and
    "path(line,col): error :"), the error codes ("format", "io", ...), the
    summary of the report, and the log file (stdout, then stderr).

    The script exits with 1 when a command crashed, ended with exit code 1
    (an internal error), or timed out; these are bugs of scic. Other exit
    codes (for example 5 for compile errors) can come from the game, so
    read the CSV.

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
    [int]$TimeoutSeconds = 900,
    [switch]$Keep
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Scic) { $Scic = Join-Path $repoRoot "Release\scic.exe" }
if (-not (Test-Path -LiteralPath $Scic)) { throw "scic.exe not found: $Scic. Build the solution first, or give -Scic." }
$Scic = (Resolve-Path -LiteralPath $Scic).Path

function Get-FullPath([string]$path) {
    return [IO.Path]::GetFullPath($path).TrimEnd('\')
}

function Test-Inside([string]$path, [string]$folder) {
    return ($path -eq $folder) -or $path.StartsWith($folder + "\", [StringComparison]::OrdinalIgnoreCase)
}

# The run folder must not be inside a source folder, and no source folder
# may be inside the run folder (the script removes the copies).
$workFull = Get-FullPath $Work
$sources = @()
foreach ($folder in $Source) {
    if (-not (Test-Path -LiteralPath $folder -PathType Container)) { throw "Source folder not found: $folder" }
    $full = Get-FullPath (Resolve-Path -LiteralPath $folder).Path
    if ((Test-Inside $workFull $full) -or (Test-Inside $full $workFull)) {
        throw "The work folder ($workFull) and a source folder ($full) must not be inside each other."
    }
    $sources += $full
}

# Find the games.
$games = @()
foreach ($root in $sources) {
    $maps = Get-ChildItem -LiteralPath $root -Recurse -Depth 3 -File -Filter "resource.map" -ErrorAction SilentlyContinue
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
$rows = @()
$bugs = 0
$index = 0
foreach ($game in $games) {
    $index++
    $id = "{0:D3}" -f $index
    # A short copy folder: scic has a MAX_PATH limit.
    $copy = Join-Path $run $id
    New-Item -ItemType Directory $copy | Out-Null
    foreach ($file in @(Get-ChildItem -LiteralPath $game.Folder -File)) {
        $target = Join-Path $copy $file.Name
        Copy-Item -LiteralPath $file.FullName -Destination $target
        $copied = Get-Item -LiteralPath $target
        $copied.Attributes = $copied.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly)
    }

    $line = "[$id/$($games.Count)] $($game.Name):"
    foreach ($command in $commands) {
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
        $codes = @($all | ForEach-Object { if ($_ -match "\[($errorCodes)\]$") { $Matches[1] } } | Sort-Object -Unique) -join " "
        if ($command.Name -eq "list") {
            $summary = "$([Math]::Max(0, @($stdout | Where-Object { $_.Trim() }).Count - 1)) scripts"
        }
        else {
            $summary = @($stderr | Where-Object { $_ -match '^(Decompiled|Compiled|Wrote|Would write) ' } | Select-Object -First 1) -join ""
        }
        if ($crashed -or ($exitCode -eq "1") -or ($exitCode -eq "timeout")) { $bugs++ }

        $rows += [pscustomobject]@{
            Game = $game.Folder
            Command = $command.Name
            ExitCode = $exitCode
            Seconds = $seconds
            Errors = $errors
            Codes = $codes
            Summary = $summary
            Log = $log
        }
        $line += " $($command.Name) $exitCode ($seconds s)"
    }
    Write-Host $line
    if (-not $Keep) { Remove-Item -LiteralPath $copy -Recurse -Force }
}

$rows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8
Write-Host ""
$rows | Group-Object Command, ExitCode | Sort-Object Name | ForEach-Object { Write-Host ("{0,-20} {1}" -f $_.Name, $_.Count) }
Write-Host "CSV: $csv"
if ($bugs -gt 0) {
    Write-Host "$bugs commands crashed, ended with exit code 1, or timed out."
    exit 1
}
exit 0
