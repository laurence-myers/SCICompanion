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
    audio format, and the script commands do not use it.

    The script only reads the source folders. Do not use junctions in place
    of the copies: decompile writes src\ and game.ini, and compile writes
    patch files. -Work and -Source must not be inside each other. The
    script compares the paths as text, so do not give another name of the
    same folder (a junction, a subst drive, or a UNC name of a local
    folder). The paths must be file-system paths (not a device path such as
    \\.\C:\), and -Work must not have the characters [ ] * ?. Each run gets
    a new run folder, and each copy is removed after its game, unless you
    give -Keep.

    The run folder (<Work>\<time>-<process id>-<random part>) gets sweep.csv
    and logs\. The CSV has one row for each game and command, written as
    the sweep goes (a row that cannot be written is tried again with the
    next one; at the end, also after Ctrl+C, it goes into
    sweep-unwritten.csv, or to the console): the game folder, the command,
    the exit code ("timeout" when the command ran longer than
    -TimeoutSeconds; "skipped" for a compile after a decompile that did not
    finish), the seconds, the count of error lines ("scic: error:", "scic:
    crash" and "path(line,col): error :"), the error codes ("format", "io",
    ...), the summary of the report, whether the row is a bug of scic,
    whether the command finished ("no" after a timeout, a crash line, or an
    exit code that is not a result of scic), and the log file (stdout, then
    stderr). A game that the sweep itself could not run (for example a copy
    that failed) gets one "sweep" row with the error.

    A bug of scic: a command that timed out; that printed a crash line or
    an "internal" error; or that ended with an exit code that scic does not
    give for a result (not 0, 2, 3, 5, 6, 7, 8 or 9: for example 1, or a
    crash code such as -1073740791, 0xC0000409, from a crash that the crash
    filter did not see). The script exits with 1 when there is a bug, a
    "sweep" row, or a row that is not in sweep.csv. Other exit codes (for
    example 5 for compile errors) can come from the game, so read the CSV.

    Usage:
      .\UnitTests\Tools\CliCorpusSweep.ps1 -Source 'F:\Games\Sierra', 'F:\games\gog' -Exclude '* - dev*'
      .\UnitTests\Tools\CliCorpusSweep.ps1 -Source 'F:\games\gog' -Include 'Space Quest*' -Keep
#>
# No [Parameter(Mandatory)]: it makes the script an advanced script, and
# then an exit in a finally block after Ctrl+C has no effect.
param(
    [string[]]$Source,
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
if (-not $Source) { throw "-Source is required." }
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# A file-system path as Windows sees it: a relative path starts at the
# current PowerShell folder (Set-Location), not at the folder of the
# process; a UNC path has no provider prefix; a drive root keeps its "\".
# A device path (\\.\ or \\?\, with either slash, also after a provider
# prefix) is refused: the text check of the folders cannot compare it with
# the other forms.
function Get-FullPath([string]$path, [string]$what) {
    $devicePath = '^[\\/]{2}[.?][\\/]'
    if ($path -match $devicePath) { throw "$what is a device path, which the script cannot use: $path" }
    $provider = $null
    $drive = $null
    $full = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($path, [ref]$provider, [ref]$drive)
    if ($provider.Name -ne "FileSystem") { throw "$what is not a file-system path: $path" }
    if ($full -match $devicePath) { throw "$what is a device path, which the script cannot use: $path" }
    $full = [IO.Path]::GetFullPath($full)
    if ($full -match $devicePath) { throw "$what is a device path, which the script cannot use: $path" }
    $trimmed = $full.TrimEnd('\')
    if ($trimmed -match '^[A-Za-z]:$') { $trimmed += '\' }
    return $trimmed
}

function Test-Inside([string]$path, [string]$folder) {
    $prefix = if ($folder.EndsWith('\')) { $folder } else { $folder + '\' }
    return ($path -eq $folder) -or $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

if (-not $Scic) { $Scic = Join-Path $repoRoot "Release\scic.exe" }
$Scic = Get-FullPath $Scic "-Scic"
if (-not (Test-Path -LiteralPath $Scic -PathType Leaf)) { throw "scic.exe not found: $Scic. Build the solution first, or give -Scic." }

# The run folder must not be inside a source folder, and no source folder
# may be inside the run folder (the script removes the copies).
$workFull = Get-FullPath $Work "-Work"
# Start-Process takes the log paths as wildcards.
if ($workFull.IndexOfAny([char[]]'[]*?') -ge 0) { throw "-Work has one of the characters [ ] * ?, which the script cannot use: $workFull" }
$sources = @()
foreach ($folder in $Source) {
    $full = Get-FullPath $folder "-Source"
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

# A new run folder, named by the time, the process id and a random part,
# so that two sweeps that start together (also two runspaces of one
# process) get two folders ("-2" and so on if a folder of that name
# exists). CreateDirectory also takes a drive root.
[void][IO.Directory]::CreateDirectory($workFull)
$stamp = "{0}-{1}-{2}" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $PID, [guid]::NewGuid().ToString("N").Substring(0, 8)
$run = Join-Path $workFull $stamp
for ($n = 2; Test-Path -LiteralPath $run; $n++) { $run = Join-Path $workFull "$stamp-$n" }
New-Item -ItemType Directory $run | Out-Null
$logs = Join-Path $run "logs"
New-Item -ItemType Directory $logs | Out-Null
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
$sweepErrors = 0

function IsAre([int]$count) {
    if ($count -eq 1) { return "is" }
    return "are"
}

function RowsText([int]$count) {
    if ($count -eq 1) { return "1 row" }
    return "$count rows"
}

# Appends the rows that wait to the CSV (UTF-8 with a BOM, and the header
# row when the file is empty), in one write. When Write fails part of the
# way (a full disk), the file is cut back to its old length, so the next
# try does not write the same rows twice. Rows smaller than the stream
# buffer (4 KB) go to the file only in the flush of SetLength or Dispose;
# a failure there has no cut-back. Throws when the write fails.
function Write-PendingRows {
    $lines = @($script:pendingRows | ConvertTo-Csv -NoTypeInformation)
    $stream = New-Object System.IO.FileStream($csv, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
    try {
        $start = $stream.Length
        $encoding = New-Object System.Text.UTF8Encoding($true)
        if ($start -eq 0) {
            $bytes = $encoding.GetPreamble() + $encoding.GetBytes(($lines -join "`r`n") + "`r`n")
        }
        else {
            $bytes = $encoding.GetBytes((($lines | Select-Object -Skip 1) -join "`r`n") + "`r`n")
        }
        [void]$stream.Seek(0, [IO.SeekOrigin]::End)
        try {
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush()
        }
        catch {
            $stream.SetLength($start)
            throw
        }
    }
    finally {
        $stream.Dispose()
    }
}

# Writes each row into the CSV at once, so a sweep that stops keeps its
# rows. A row that cannot be written (for example while another program
# has the CSV open) waits, and the next write tries it again.
$pendingRows = New-Object System.Collections.ArrayList
function Add-Row($row) {
    $script:rows += $row
    [void]$script:pendingRows.Add($row)
    try {
        Write-PendingRows
        $script:pendingRows.Clear()
    }
    catch {
        Write-Warning "could not write $(RowsText $script:pendingRows.Count) to $csv (the next write tries again): $_"
    }
}

# The rows that wait: a last try into the CSV, then sweep-unwritten.csv,
# then the console. True when every row is in sweep.csv.
function Save-PendingRows {
    $count = $script:pendingRows.Count
    if ($count -eq 0) { return $true }
    $written = $false
    try {
        Write-PendingRows
        $written = $true
    }
    catch {
        Write-Warning "could not write $(RowsText $count) to ${csv}: $_"
    }
    if ($written) {
        $script:pendingRows.Clear()
        return $true
    }
    $unwritten = Join-Path $run "sweep-unwritten.csv"
    try {
        $script:pendingRows | Export-Csv -LiteralPath $unwritten -NoTypeInformation -Encoding UTF8
        Write-Host "$(RowsText $count) $(IsAre $count) not in ${csv}: see $unwritten."
    }
    catch {
        Write-Host "$(RowsText $count) $(IsAre $count) not in $csv, and $unwritten cannot be written ($_). The rows:"
        $script:pendingRows | ConvertTo-Csv -NoTypeInformation | ForEach-Object { Write-Host $_ }
    }
    $script:pendingRows.Clear()
    return $false
}

# Runs one command of scic on a copy; returns its row.
function Invoke-ScicCommand([string]$game, [string]$copy, [string]$id, $command) {
    # The game folder is the argument after the command's words.
    $arguments = @($command.Arguments[0], $command.Arguments[1], "`"$copy`"") + @($command.Arguments | Select-Object -Skip 2)
    $out = Join-Path $logs "$id.$($command.Name).out.txt"
    $err = Join-Path $logs "$id.$($command.Name).err.txt"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    # -FilePath takes a wildcard, so the path of scic is escaped.
    $process = Start-Process -FilePath ([WildcardPattern]::Escape($Scic)) -ArgumentList ($arguments -join " ") -NoNewWindow -PassThru `
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
    # A command that ended with a result (exit code 1 included) finished;
    # one that timed out, printed a crash line, or ended with another exit
    # code (a crash code, or a code that scic does not give) did not.
    $finished = ($exitCode -ne "timeout") -and -not $crashed -and ((@("1") + $resultCodes) -contains $exitCode)
    return [pscustomobject]@{
        Game = $game
        Command = $command.Name
        ExitCode = $exitCode
        Seconds = $seconds
        Errors = $errors
        Codes = ($codes -join " ")
        Summary = $summary
        Bug = $(if ($bug) { "yes" } else { "" })
        Finished = $(if ($finished) { "yes" } else { "no" })
        Log = $log
    }
}

$index = 0
$allWritten = $false
$loopDone = $false
try {
    foreach ($game in $games) {
        $index++
        $id = "{0:D3}" -f $index
        # A short copy folder: scic has a MAX_PATH limit.
        $copy = Join-Path $run $id
        $madeCopy = $false
        $line = "[$id/$($games.Count)] $($game.Name):"
        try {
            New-Item -ItemType Directory $copy | Out-Null
            $madeCopy = $true
            foreach ($file in @(Get-ChildItem -LiteralPath $game.Folder -File)) {
                $target = Join-Path $copy $file.Name
                Copy-Item -LiteralPath $file.FullName -Destination $target
                $copied = Get-Item -LiteralPath $target
                $copied.Attributes = $copied.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly)
            }
            $decompileFinished = $true
            foreach ($command in $commands) {
                if (($command.Name -eq "compile") -and -not $decompileFinished) {
                    # A compile after a decompile that did not finish tells
                    # nothing.
                    $row = [pscustomobject]@{ Game = $game.Folder; Command = "compile"; ExitCode = "skipped"; Seconds = 0; Errors = 0; Codes = ""; Summary = "the decompile did not finish"; Bug = ""; Finished = "no"; Log = "" }
                }
                else {
                    $row = Invoke-ScicCommand $game.Folder $copy $id $command
                }
                Add-Row $row
                if ($row.Bug) { $bugs++ }
                if ($command.Name -eq "decompile") { $decompileFinished = ($row.Finished -eq "yes") }
                $line += " $($command.Name) $($row.ExitCode)"
                if ($row.ExitCode -ne "skipped") { $line += " ($($row.Seconds) s)" }
            }
        }
        catch {
            # The sweep goes on with the next game; the row names the failure.
            $sweepErrors++
            Add-Row ([pscustomobject]@{ Game = $game.Folder; Command = "sweep"; ExitCode = "error"; Seconds = 0; Errors = 1; Codes = ""; Summary = "$_"; Bug = ""; Finished = "no"; Log = "" })
            $line += " sweep error: $_"
        }
        finally {
            # Only a copy that this run made; one that cannot go gives a warning.
            if ($madeCopy -and -not $Keep -and (Test-Path -LiteralPath $copy)) {
                try {
                    Remove-Item -LiteralPath $copy -Recurse -Force
                }
                catch {
                    Write-Warning "could not remove the copy ${copy}: $_"
                }
            }
        }
        Write-Host $line
    }
    $loopDone = $true
}
finally {
    # Also after Ctrl+C: PowerShell runs a finally block then.
    $allWritten = Save-PendingRows
    if (-not $loopDone) {
        Write-Host "The sweep stopped before its end. CSV: $csv"
        exit 1
    }
}

Write-Host ""
$rows | Group-Object Command, ExitCode | Sort-Object Name | ForEach-Object { Write-Host ("{0,-20} {1}" -f $_.Name, $_.Count) }
$failed = -not $allWritten
Write-Host "CSV: $csv"
if ($bugs -gt 0) {
    Write-Host "$(if ($bugs -eq 1) { '1 command was a bug' } else { "$bugs commands were bugs" }) of scic: a crash, exit code 1 or another code that scic does not give, an internal error, or a timeout. See the Bug column."
    $failed = $true
}
if ($sweepErrors -gt 0) {
    Write-Host "$(if ($sweepErrors -eq 1) { '1 game was' } else { "$sweepErrors games were" }) not swept: see the sweep rows."
    $failed = $true
}
if ($failed) { exit 1 }
exit 0
