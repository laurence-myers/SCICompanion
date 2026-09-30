<#
    The functions that the corpus scripts share (CliCorpusSweep.ps1 and
    DecompileGate.ps1). Dot-source it: . (Join-Path $PSScriptRoot 'Corpus.Common.ps1')

    The corpus folders are read-only for these scripts: a command that
    writes (decompile, compile) runs on a copy (Copy-GameFiles).
#>

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

# The games under the source folders: each folder (to -Depth levels down)
# that holds a resource.map, as { Name; Folder; Md5 }. Name is the path
# under its source folder (the source folder's own name for the source
# folder itself), for -Include and -Exclude (wildcards). Md5 is the MD5 of
# resource.map. With -Unique, one game for each MD5: the first in the order
# of the sources, then of the folders.
function Find-CorpusGames([string[]]$Sources, [int]$Depth = 5, [string[]]$Include = @("*"), [string[]]$Exclude = @(), [switch]$Unique) {
    $games = @()
    $seen = @{}
    foreach ($root in $Sources) {
        $maps = Get-ChildItem -LiteralPath $root -Recurse -Depth $Depth -File -Filter "resource.map" -ErrorAction SilentlyContinue
        foreach ($folder in @($maps | ForEach-Object { $_.Directory.FullName } | Sort-Object -Unique)) {
            $name = $folder.Substring($root.Length).TrimStart('\')
            if (-not $name) { $name = Split-Path -Leaf $root }
            $included = @($Include | Where-Object { $name -like $_ }).Count -gt 0
            $excluded = @($Exclude | Where-Object { $name -like $_ }).Count -gt 0
            if (-not $included -or $excluded) { continue }
            $md5 = (Get-FileHash -LiteralPath (Join-Path $folder "resource.map") -Algorithm MD5).Hash.ToLowerInvariant()
            if ($Unique) {
                if ($seen.ContainsKey($md5)) { continue }
                $seen[$md5] = $true
            }
            $games += [pscustomobject]@{ Name = $name; Folder = $folder; Md5 = $md5 }
        }
    }
    return $games
}

# Copies the files of a game folder (not its subfolders: they hold DOSBox,
# ScummVM, saves, CD audio and the user's src\) into a new folder, and
# clears their read-only attribute. GameSession::Open reads AUDIO\ and AUD\
# only to find the audio format, which the script commands do not use.
function Copy-GameFiles([string]$From, [string]$To) {
    New-Item -ItemType Directory $To -Force | Out-Null
    foreach ($file in @(Get-ChildItem -LiteralPath $From -File)) {
        $target = Join-Path $To $file.Name
        Copy-Item -LiteralPath $file.FullName -Destination $target
        $copied = Get-Item -LiteralPath $target
        $copied.Attributes = $copied.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly)
    }
}

# Runs a program with its output in two files, and waits at most
# TimeoutSeconds. Returns the exit code as text, or "timeout" (the process
# is stopped).
function Invoke-Logged([string]$FilePath, [string[]]$Arguments, [string]$Out, [string]$Err, [int]$TimeoutSeconds) {
    $quoted = @($Arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } })
    # -FilePath takes a wildcard, so the path is escaped.
    $process = Start-Process -FilePath ([WildcardPattern]::Escape($FilePath)) -ArgumentList ($quoted -join " ") -NoNewWindow -PassThru `
        -RedirectStandardOutput $Out -RedirectStandardError $Err
    # Windows PowerShell gives no ExitCode unless the handle was read.
    $null = $process.Handle
    if ($process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.WaitForExit()
        return "$($process.ExitCode)"
    }
    $process.Kill()
    $process.WaitForExit()
    return "timeout"
}
