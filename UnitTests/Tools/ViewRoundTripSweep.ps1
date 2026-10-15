<#
    The round-trip check of scic view export, scic view import and scic view
    add on a game (docs/scic-cli/plan.md, sections 4.11 to 4.13). For local
    use: CI does not run it (CI has only the SCI1.1 template; the check is
    for a game with many views, for example the hi-res Leisure Suit Larry
    6).

    The script only reads the game folder. It copies the files of the game
    folder (not its subfolders) into a new run folder under -Work, and all
    commands run on copies there:

      1. scic view list <copy>                      the views and their CRC-32
      2. scic view export <copy> --all              -> export1
      3. scic view import <copy> --in-dir export1   -> patch1: no file (each
         view has no change)
      4. swapped: the files of export1, with two palette entries that the
         image has (not the skip colour of the cel) swapped in each BMP.
      5. scic view import <copy> --in-dir swapped   -> patch2
      6. A second copy of the game gets the patch files of patch2.
         scic view list of it: each view with a patch file has the source
         "patch", the loops, the cels and the resolution of step 1, and the
         patch file starts with 80 80 00 00.
      7. scic view export of the second copy -> export2: each BMP file is
         the file of swapped, byte for byte.
      8. scic view add <copy> --view N --from export1\N.toml --replace, for
         each view of export1                       -> patch3
      9. A third copy of the game gets the patch files of patch3. scic view
         export of it -> export3: each BMP file is the file of export1, byte
         for byte, and each .toml file has the facts of the file of export1
         (all but the comment, [source] and the file names).

    The run folder (<Work>\<time>-<process id>) gets roundtrip.csv, with one
    row for each view (number, format, cels, exported, cel_files,
    no_change, imported, list_same, swap_same, added, add_same, note), and
    logs\ (stdout and stderr of each command). A cell is "yes", "no", or
    empty when the check does not apply. cel_files is "yes" when export1
    has one BMP file for each stored cel of the view. imported is "no" for
    a view that export took and import did not: that is a failure, and the
    note of the row has the error of import. added is "no" for a view that
    add refused; that is a failure, but for an Unsupported refusal (an
    SCI1.1 view of more than 64 KB), which is only a note.

    Some games have pixels with a palette entry that the view and palette
    999 do not mark as used, and import refuses them. -AllowUnusedIndices
    gives --allow-unused-indices to import and to add, so that these views
    are in the check.

    The script exits with 1 when a check gives "no", when scic gives an exit
    code that is not 0 or 6 (6: a view failed, which the rows show), or when
    a command prints an internal error. -Keep keeps the copies and the image
    folders; without it only the CSV and the logs stay.

    Usage:
      .\UnitTests\Tools\ViewRoundTripSweep.ps1 -Game 'F:\Games\Sierra\Leisure Suit Larry 6 - Shape Up or Slip Out (1993)\LSL6CD'
      .\UnitTests\Tools\ViewRoundTripSweep.ps1 -Game <folder> -Work I:\tmp\view-sweep -Keep -AllowUnusedIndices
#>
# No [Parameter(Mandatory)]: it makes the script an advanced script, and
# then an exit in a finally block after Ctrl+C has no effect.
param(
    [string]$Game,
    # The folder for the run folders. Default: the temp folder.
    [string]$Work = (Join-Path ([IO.Path]::GetTempPath()) "scic-view-sweep"),
    # Default: Release\scic.exe of this repository.
    [string]$Scic = "",
    [int]$TimeoutSeconds = 900,
    [switch]$Keep,
    [switch]$AllowUnusedIndices
)

$ErrorActionPreference = "Stop"
if (-not $Game) { throw "-Game is required." }
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

. (Join-Path $PSScriptRoot 'Corpus.Common.ps1')

if (-not $Scic) { $Scic = Join-Path $repoRoot "Release\scic.exe" }
$Scic = Get-FullPath $Scic "-Scic"
if (-not (Test-Path -LiteralPath $Scic -PathType Leaf)) { throw "scic.exe not found: $Scic. Build the solution first, or give -Scic." }
$gameFull = Get-FullPath $Game "-Game"
if (-not (Test-Path -LiteralPath (Join-Path $gameFull "resource.map") -PathType Leaf)) { throw "No resource.map in the game folder: $gameFull" }
$workFull = Get-FullPath $Work "-Work"
if ($workFull.IndexOfAny([char[]]'[]*?') -ge 0) { throw "-Work has one of the characters [ ] * ?, which the script cannot use: $workFull" }
if ((Test-Inside $workFull $gameFull) -or (Test-Inside $gameFull $workFull)) {
    throw "The work folder ($workFull) and the game folder ($gameFull) must not be inside each other."
}

[void][IO.Directory]::CreateDirectory($workFull)
$stamp = "{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $PID
$run = Join-Path $workFull $stamp
for ($suffix = 2; Test-Path -LiteralPath $run; $suffix++) { $run = Join-Path $workFull "$stamp-$suffix" }
New-Item -ItemType Directory $run | Out-Null
$logs = Join-Path $run "logs"
New-Item -ItemType Directory $logs | Out-Null
Write-Host "scic: $Scic"
Write-Host "Game: $gameFull. Run folder: $run"

$failures = New-Object System.Collections.Generic.List[string]

# Runs scic with its output in the logs folder, and returns its stdout
# lines. An exit code that is not 0 or 6, and an internal error, are
# failures of the sweep.
function Invoke-Scic([string]$name, [string[]]$arguments) {
    $out = Join-Path $logs "$name.out.txt"
    $err = Join-Path $logs "$name.err.txt"
    $code = Invoke-Logged $Scic $arguments $out $err $TimeoutSeconds
    $errText = if (Test-Path -LiteralPath $err) { [IO.File]::ReadAllText($err) } else { "" }
    if (@("0", "6") -notcontains $code) { $failures.Add("scic $name ended with the exit code $code (see $err)") }
    if ($errText -match '\[internal\]|scic: crash') { $failures.Add("scic $name printed an internal error or a crash (see $err)") }
    Write-Host ("  {0}: exit code {1}" -f $name, $code)
    return @([IO.File]::ReadAllLines($out))
}

# The rows of scic view list --format tsv, by view number.
function Get-ViewRows([string]$name, [string]$gameCopy) {
    $rows = @{}
    foreach ($line in (Invoke-Scic $name @("view", "list", $gameCopy, "--format", "tsv"))) {
        if ($line -notmatch '^\d+\t') { continue }
        $fields = $line -split "`t"
        $rows[[int]$fields[0]] = [pscustomobject]@{ Format = $fields[1]; Loops = $fields[2]; Cels = $fields[4]; Resolution = $fields[6]; Source = $fields[7]; Crc32 = $fields[9]; Error = $fields[16] }
    }
    return $rows
}

# The lines of a scic command for its views ("scic: error: view 110: ...",
# "scic: warning: view 110: ..."), by view number: the text after the view.
function Get-ViewLines([string]$name, [string]$kind) {
    $lines = @{}
    $err = Join-Path $logs "$name.err.txt"
    if (Test-Path -LiteralPath $err) {
        foreach ($line in [IO.File]::ReadAllLines($err)) {
            if ($line -match ('^scic: {0}: view (\d+): (.*)$' -f $kind)) { $lines[[int]$Matches[1]] = $Matches[2] }
        }
    }
    return $lines
}

function Test-SameBytes([string]$one, [string]$other) {
    if (-not (Test-Path -LiteralPath $one -PathType Leaf) -or -not (Test-Path -LiteralPath $other -PathType Leaf)) { return $false }
    return [System.Linq.Enumerable]::SequenceEqual([byte[]][IO.File]::ReadAllBytes($one), [byte[]][IO.File]::ReadAllBytes($other))
}

# The names of the BMP files of each view in an image folder
# (<view>.l<loop>.c<cel>.bmp): view number -> list of names. The folder is
# listed once.
$imageNames = @{}
function Get-ViewImages([string]$folder, [int]$number) {
    if (-not $imageNames.ContainsKey($folder)) {
        $byView = @{}
        foreach ($file in [IO.Directory]::EnumerateFiles($folder, "*.bmp")) {
            $name = [IO.Path]::GetFileName($file)
            if ($name -match '^(\d+)\.l\d+\.c\d+\.bmp$') {
                $view = [int]$Matches[1]
                if (-not $byView.ContainsKey($view)) { $byView[$view] = New-Object System.Collections.Generic.List[string] }
                $byView[$view].Add($name)
            }
        }
        $imageNames[$folder] = $byView
    }
    $names = $imageNames[$folder][$number]
    if (-not $names) { return @() }
    return @($names | ForEach-Object { [pscustomobject]@{ Name = $_; FullName = (Join-Path $folder $_) } })
}

# The skip colour of each BMP file of a sidecar file: file name -> colour.
function Get-SkipColors([string]$sidecarPath) {
    $colors = @{}
    $text = [IO.File]::ReadAllText($sidecarPath)
    foreach ($block in ($text -split '\[\[cel\]\]' | Select-Object -Skip 1)) {
        if (($block -match "(?m)^file = '([^']*)'") -or ($block -match '(?m)^file = "([^"]*)"')) {
            $file = $Matches[1]
            if ($block -match '(?m)^skip_color = (\d+)') { $colors[$file] = [int]$Matches[1] }
        }
    }
    return $colors
}

# Writes a copy of an 8-bit BMP file with two palette entries of its
# pixels swapped: the first two values of the image that are not the skip
# colour. The padding bytes of the rows stay. False when the image has
# fewer than two such values (the copy is then the file as it is).
function Write-SwappedImage([string]$from, [string]$to, [int]$skipColor) {
    $bytes = [IO.File]::ReadAllBytes($from)
    $pixelOffset = [BitConverter]::ToInt32($bytes, 10)
    $width = [BitConverter]::ToInt32($bytes, 18)
    $height = [Math]::Abs([BitConverter]::ToInt32($bytes, 22))
    $stride = [int](($width + 3) -band (-bnot 3))
    $first = -1
    $second = -1
    for ($y = 0; ($y -lt $height) -and ($second -lt 0); $y++) {
        $row = $pixelOffset + ($y * $stride)
        for ($x = 0; $x -lt $width; $x++) {
            $value = $bytes[$row + $x]
            if ($value -eq $skipColor) { continue }
            if ($first -lt 0) { $first = $value }
            elseif ($value -ne $first) { $second = $value; break }
        }
    }
    $swapped = ($second -ge 0)
    if ($swapped) {
        for ($y = 0; $y -lt $height; $y++) {
            $row = $pixelOffset + ($y * $stride)
            for ($x = 0; $x -lt $width; $x++) {
                $value = $bytes[$row + $x]
                if ($value -eq $first) { $bytes[$row + $x] = [byte]$second }
                elseif ($value -eq $second) { $bytes[$row + $x] = [byte]$first }
            }
        }
    }
    [IO.File]::WriteAllBytes($to, $bytes)
    return $swapped
}

# The text of a .toml file of export with no comment, no [source] table and
# no file names: the facts of the view.
function Get-ViewFacts([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return "" }
    $text = [IO.File]::ReadAllText($path) -replace "`r`n", "`n"
    $text = $text -replace '(?m)^#.*\n', ''
    $text = $text -replace '(?s)\[source\]\n.*?\n\n', ''
    return ($text -replace '(?m)^file = .*\n', '')
}

# The first 4 bytes of a file, as 8 hex digits.
function Get-FileHeader([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    try {
        $bytes = New-Object byte[] 4
        $read = $stream.Read($bytes, 0, 4)
        return (($bytes[0..($read - 1)] | ForEach-Object { $_.ToString("x2") }) -join "")
    }
    finally { $stream.Dispose() }
}

$copy = Join-Path $run "game"
$export1 = Join-Path $run "export1"
$patch1 = Join-Path $run "patch1"
$swappedFolder = Join-Path $run "swapped"
$patch2 = Join-Path $run "patch2"
$export2 = Join-Path $run "export2"
$patch3 = Join-Path $run "patch3"
$export3 = Join-Path $run "export3"
foreach ($folder in @($export1, $patch1, $swappedFolder, $patch2, $export2, $patch3, $export3)) { New-Item -ItemType Directory $folder | Out-Null }

Copy-GameFiles $gameFull $copy
$original = Get-ViewRows "1-list" $copy
if ($original.Count -eq 0) { throw "scic view list found no view in $copy (see $logs)." }
[void](Invoke-Scic "2-export" @("view", "export", $copy, "--all", "--out-dir", $export1))
$importOptions = @()
if ($AllowUnusedIndices) { $importOptions = @("--allow-unused-indices") }
[void](Invoke-Scic "3-import" (@("view", "import", $copy, "--in-dir", $export1, "--out-dir", $patch1) + $importOptions))
$noChangeErrors = Get-ViewLines "3-import" "error"
$noChangeWarnings = Get-ViewLines "3-import" "warning"

Write-Host "Swapping two entries in each image ..."
$swappedImages = 0
foreach ($sidecar in @(Get-ChildItem -LiteralPath $export1 -File -Filter "*.toml")) {
    Copy-Item -LiteralPath $sidecar.FullName -Destination (Join-Path $swappedFolder $sidecar.Name)
    $skipColors = Get-SkipColors $sidecar.FullName
    foreach ($file in $skipColors.Keys) {
        if (Write-SwappedImage (Join-Path $export1 $file) (Join-Path $swappedFolder $file) $skipColors[$file]) { $swappedImages++ }
    }
}
[void](Invoke-Scic "4-import-swapped" (@("view", "import", $copy, "--in-dir", $swappedFolder, "--out-dir", $patch2) + $importOptions))
$swapErrors = Get-ViewLines "4-import-swapped" "error"

$copy2 = Join-Path $run "game-patch2"
Copy-GameFiles $gameFull $copy2
foreach ($patch in @(Get-ChildItem -LiteralPath $patch2 -File)) {
    Copy-Item -LiteralPath $patch.FullName -Destination (Join-Path $copy2 $patch.Name)
}
$patched = Get-ViewRows "5-list-patched" $copy2
[void](Invoke-Scic "6-export-patched" @("view", "export", $copy2, "--all", "--out-dir", $export2))

Write-Host "Adding each exported view again ..."
$addErrors = @{}
foreach ($sidecar in @(Get-ChildItem -LiteralPath $export1 -File -Filter "*.toml")) {
    $number = [int][IO.Path]::GetFileNameWithoutExtension($sidecar.Name)
    $name = "7-add-$number"
    $code = Invoke-Logged $Scic (@("view", "add", $copy, "--view", "$number", "--from", $sidecar.FullName, "--replace", "--out-dir", $patch3) + $importOptions) (Join-Path $logs "$name.out.txt") (Join-Path $logs "$name.err.txt") $TimeoutSeconds
    $errText = [IO.File]::ReadAllText((Join-Path $logs "$name.err.txt"))
    if (($errText -match '\[internal\]|scic: crash') -or (@("0", "6") -notcontains $code)) { $failures.Add("scic $name ended with the exit code $code or an internal error (see $logs)") }
    if ($code -ne "0") {
        $lines = Get-ViewLines $name "error"
        $addErrors[$number] = if ($lines.ContainsKey($number)) { $lines[$number] } else { "scic view add ended with the exit code $code" }
    }
}
$copy3 = Join-Path $run "game-patch3"
Copy-GameFiles $gameFull $copy3
foreach ($patch in @(Get-ChildItem -LiteralPath $patch3 -File)) {
    Copy-Item -LiteralPath $patch.FullName -Destination (Join-Path $copy3 $patch.Name)
}
[void](Invoke-Scic "8-export-added" @("view", "export", $copy3, "--all", "--out-dir", $export3))

function Get-YesNo($value) { if ($value) { "yes" } else { "no" } }

$rows = New-Object System.Collections.Generic.List[object]
foreach ($number in ($original.Keys | Sort-Object)) {
    $view = $original[$number]
    $exported = Test-Path -LiteralPath (Join-Path $export1 "$number.toml")
    $row = [ordered]@{ number = $number; format = $view.Format; cels = $view.Cels; exported = (Get-YesNo $exported); cel_files = ""; no_change = ""; imported = ""; list_same = ""; swap_same = ""; added = ""; add_same = ""; note = $view.Error }
    if ($exported) {
        # @(): a function gives one object, not an array of one.
        $images = @(Get-ViewImages $export1 $number)
        $row.cel_files = Get-YesNo ("$($images.Count)" -eq $view.Cels)
        if ("$($images.Count)" -ne $view.Cels) { $failures.Add("view ${number}: export wrote $($images.Count) BMP file(s), and the view stores $($view.Cels) cel(s)") }
        $noChange = (-not $noChangeErrors.ContainsKey($number)) -and ($noChangeWarnings[$number] -match 'does not change') -and -not (Test-Path -LiteralPath (Join-Path $patch1 "$number.v56"))
        $row.no_change = Get-YesNo $noChange
        if (-not $noChange) { $failures.Add("view ${number}: the import of the exported images is not a view with no change: $($noChangeErrors[$number])") }
        $patchFile = Join-Path $patch2 "$number.v56"
        $swappedHere = @(Get-ViewImages $export1 $number | Where-Object { -not (Test-SameBytes $_.FullName (Join-Path $swappedFolder $_.Name)) }).Count -gt 0
        $imported = Test-Path -LiteralPath $patchFile
        $row.imported = Get-YesNo ($imported -or -not $swappedHere)
        if ($imported) {
            $after = $patched[$number]
            $sameList = $after -and ($after.Source -eq "patch") -and ($after.Loops -eq $view.Loops) -and ($after.Cels -eq $view.Cels) -and ($after.Resolution -eq $view.Resolution) -and (-not $after.Error)
            $row.list_same = Get-YesNo $sameList
            if (-not $sameList) { $failures.Add("view ${number}: the game copy with patch2 does not list the view as before, from its patch file") }
            $header = Get-FileHeader $patchFile
            if ($header -ne "80800000") {
                $row.note = "the patch file starts with $header"
                $failures.Add("view ${number}: the patch file starts with $header, and the patch file of a view starts with 80800000")
            }
            $swapSame = @(Get-ViewImages $swappedFolder $number | Where-Object { -not (Test-SameBytes $_.FullName (Join-Path $export2 $_.Name)) }).Count -eq 0
            $row.swap_same = Get-YesNo $swapSame
            if (-not $swapSame) { $failures.Add("view ${number}: an image of the game copy with patch2 is not the swapped image") }
        }
        elseif ($swappedHere) {
            $reason = $swapErrors[$number]
            if (-not $reason) { $reason = "import wrote no patch file and gave no error for the view" }
            $row.note = $reason
            $failures.Add("view ${number}: export took the view and import of the swapped images did not: $reason")
        }
        # The view added from the files of export.
        $added = Test-Path -LiteralPath (Join-Path $patch3 "$number.v56")
        $row.added = Get-YesNo $added
        if ($added) {
            $addSame = ((Get-ViewFacts (Join-Path $export1 "$number.toml")) -eq (Get-ViewFacts (Join-Path $export3 "$number.toml"))) -and
                (@($images | Where-Object { -not (Test-SameBytes $_.FullName (Join-Path $export3 $_.Name)) }).Count -eq 0)
            $row.add_same = Get-YesNo $addSame
            if (-not $addSame) { $failures.Add("view ${number}: the view added from the files of export does not export the same .toml facts and images") }
        }
        else {
            $reason = $addErrors[$number]
            $row.note = "add: $reason"
            # A refusal (a view of more than 64 KB in SCI1.1) is a note; any
            # other reason is a failure.
            if ($reason -notmatch "Sierra's interpreter of SCI1\.1 reads a view of 65535 bytes or fewer \[unsupported\]$") { $failures.Add("view ${number}: export took the view and add did not: $reason") }
        }
    }
    $rows.Add([pscustomobject]$row)
}
$csv = Join-Path $run "roundtrip.csv"
$rows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8

if (-not $Keep) {
    foreach ($folder in @($copy, $copy2, $copy3, $export1, $patch1, $swappedFolder, $patch2, $export2, $patch3, $export3)) {
        [IO.Directory]::Delete($folder, $true)
    }
}

$exportedCount = @($rows | Where-Object { $_.exported -eq "yes" }).Count
$importedCount = @($rows | Where-Object { $_.swap_same -ne "" }).Count
Write-Host ("Views: {0}. Exported: {1} ({2} with one BMP file for each stored cel). No change: {3}. Images with two entries swapped: {4}." -f $rows.Count, $exportedCount,
    @($rows | Where-Object { $_.cel_files -eq "yes" }).Count, @($rows | Where-Object { $_.no_change -eq "yes" }).Count, $swappedImages)
Write-Host ("Imported with swapped images: {0}. Listed as before: {1}. Swapped images the same: {2}." -f $importedCount,
    @($rows | Where-Object { $_.list_same -eq "yes" }).Count, @($rows | Where-Object { $_.swap_same -eq "yes" }).Count)
Write-Host ("Added from the files of export: {0}. The same facts and images: {1}." -f @($rows | Where-Object { $_.added -eq "yes" }).Count, @($rows | Where-Object { $_.add_same -eq "yes" }).Count)
Write-Host "CSV: $csv"
if ($importedCount -eq 0) { $failures.Add("no view went through export and import, so the sweep checked no image") }
if ($failures.Count -gt 0) {
    Write-Host ("{0} failure(s):" -f $failures.Count)
    $failures | Select-Object -First 30 | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "The round trip is exact."
exit 0
