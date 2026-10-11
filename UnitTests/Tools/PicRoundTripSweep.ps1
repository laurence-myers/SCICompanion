<#
    The round-trip check of scic pic export and scic pic import on a game
    (docs/scic-cli/plan.md, section 4.7). For local use: CI does not run it
    (CI has only the SCI1.1 template; the check is for a game with SCI2
    pics, for example the hi-res Leisure Suit Larry 6).

    The script only reads the game folder. It copies the files of the game
    folder (not its subfolders) into a new run folder under -Work, and all
    commands run on copies there:

      1. scic pic list <copy>                       the pics and their CRC-32
      2. scic pic export <copy> --all --cel all     -> export1
      3. scic pic import <copy> --in-dir export1    -> patch1 (no change)
      4. A second copy of the game gets the patch files of patch1.
         scic pic list of it: each pic with a patch file has the source
         "patch", and an SCI2 pic (it changes in place) has the CRC-32 of
         the pic of step 1. The patch file of an SCI2 pic starts with
         81 81 00 00.
      5. scic pic export of the second copy -> export2: each BMP file is the
         file of export1, byte for byte.
      6. swapped: the files of export1, with two palette entries that the
         image has (not the skip colour of the cel) swapped in each BMP.
      7. scic pic import <copy> --in-dir swapped    -> patch2
      8. A third copy of the game gets the patch files of patch2.
         scic pic export of it -> export3: each BMP file is the file of
         swapped, byte for byte.

    The run folder (<Work>\<time>-<process id>) gets roundtrip.csv, with one
    row for each pic (number, format, cels, exported, cel_files, imported,
    crc_same, images_same, swap_same, note), and logs\ (stdout and stderr of
    each command). A cell is "yes", "no", or empty when the check does not
    apply (a pic that export or import did not take, for example a pic with a
    compressed cel; crc_same of an SCI1.1 pic, which import writes again).
    cel_files is "yes" when export1 has one BMP file for each cel of the pic.

    The script exits with 1 when a check gives "no", when scic gives an exit
    code that is not 0 or 6 (6: a pic failed, which the rows show), or when a
    command prints an internal error. -Keep keeps the copies and the image
    folders; without it only the CSV and the logs stay.

    Usage:
      .\UnitTests\Tools\PicRoundTripSweep.ps1 -Game 'F:\Games\Sierra\Leisure Suit Larry 6 - Shape Up or Slip Out (1993)\LSL6CD'
      .\UnitTests\Tools\PicRoundTripSweep.ps1 -Game <folder> -Work I:\tmp\pic-sweep -Keep
#>
# No [Parameter(Mandatory)]: it makes the script an advanced script, and
# then an exit in a finally block after Ctrl+C has no effect.
param(
    [string]$Game,
    # The folder for the run folders. Default: the temp folder.
    [string]$Work = (Join-Path ([IO.Path]::GetTempPath()) "scic-pic-sweep"),
    # Default: Release\scic.exe of this repository.
    [string]$Scic = "",
    [int]$TimeoutSeconds = 900,
    [switch]$Keep
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

# The rows of scic pic list --format tsv, by pic number: Format, Cels,
# Source, Crc32, Error.
function Get-PicRows([string]$name, [string]$gameCopy) {
    $rows = @{}
    foreach ($line in (Invoke-Scic $name @("pic", "list", $gameCopy, "--format", "tsv"))) {
        if ($line -notmatch '^\d+\t') { continue }
        $fields = $line -split "`t"
        $rows[[int]$fields[0]] = [pscustomobject]@{ Format = $fields[1]; Cels = $fields[4]; Source = $fields[12]; Crc32 = $fields[14]; Error = $fields[18] }
    }
    return $rows
}

function Test-SameBytes([string]$one, [string]$other) {
    if (-not (Test-Path -LiteralPath $one -PathType Leaf) -or -not (Test-Path -LiteralPath $other -PathType Leaf)) { return $false }
    return [System.Linq.Enumerable]::SequenceEqual([byte[]][IO.File]::ReadAllBytes($one), [byte[]][IO.File]::ReadAllBytes($other))
}

# The BMP files of a pic in an image folder: <pic>.bmp and <pic>.cel<n>.bmp.
function Get-PicImages([string]$folder, [int]$number) {
    return @(Get-ChildItem -LiteralPath $folder -File | Where-Object { $_.Name -match ("^{0}(\.cel\d+)?\.bmp$" -f $number) })
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

# A copy of the game with the pic patch files of a folder in it.
function New-PatchedCopy([string]$name, [string]$patchFolder) {
    $copy = Join-Path $run $name
    Copy-GameFiles $gameFull $copy
    foreach ($patch in @(Get-ChildItem -LiteralPath $patchFolder -File)) {
        Copy-Item -LiteralPath $patch.FullName -Destination (Join-Path $copy $patch.Name)
    }
    return $copy
}

$copy = Join-Path $run "game"
$export1 = Join-Path $run "export1"
$patch1 = Join-Path $run "patch1"
$export2 = Join-Path $run "export2"
$swappedFolder = Join-Path $run "swapped"
$patch2 = Join-Path $run "patch2"
$export3 = Join-Path $run "export3"
foreach ($folder in @($export1, $patch1, $export2, $swappedFolder, $patch2, $export3)) { New-Item -ItemType Directory $folder | Out-Null }

Copy-GameFiles $gameFull $copy
$original = Get-PicRows "1-list" $copy
if ($original.Count -eq 0) { throw "scic pic list found no pic in $copy (see $logs)." }
[void](Invoke-Scic "2-export" @("pic", "export", $copy, "--all", "--cel", "all", "--out-dir", $export1))
[void](Invoke-Scic "3-import" @("pic", "import", $copy, "--in-dir", $export1, "--out-dir", $patch1))

$copy2 = New-PatchedCopy "game-patch1" $patch1
$patched = Get-PicRows "4-list-patched" $copy2
[void](Invoke-Scic "5-export-patched" @("pic", "export", $copy2, "--all", "--cel", "all", "--out-dir", $export2))

# The patch file of a pic: the name that scic gave it (<pic>.p56 for SCI1.1
# and SCI2).
function Test-HasPatch([string]$folder, [int]$number) {
    return @(Get-ChildItem -LiteralPath $folder -File | Where-Object { $_.BaseName -eq "$number" }).Count -gt 0
}

# The first 4 bytes of the patch file of a pic, as 8 hex digits.
function Get-PatchHeader([string]$folder, [int]$number) {
    $patch = @(Get-ChildItem -LiteralPath $folder -File | Where-Object { $_.BaseName -eq "$number" })[0]
    $stream = [IO.File]::OpenRead($patch.FullName)
    try {
        $bytes = New-Object byte[] 4
        $read = $stream.Read($bytes, 0, 4)
        return (($bytes[0..($read - 1)] | ForEach-Object { $_.ToString("x2") }) -join "")
    }
    finally { $stream.Dispose() }
}

Write-Host "Swapping two entries in each image ..."
$swappedImages = 0
foreach ($sidecar in @(Get-ChildItem -LiteralPath $export1 -File -Filter "*.toml")) {
    Copy-Item -LiteralPath $sidecar.FullName -Destination (Join-Path $swappedFolder $sidecar.Name)
    $skipColors = Get-SkipColors $sidecar.FullName
    foreach ($file in $skipColors.Keys) {
        if (Write-SwappedImage (Join-Path $export1 $file) (Join-Path $swappedFolder $file) $skipColors[$file]) { $swappedImages++ }
    }
}
[void](Invoke-Scic "7-import-swapped" @("pic", "import", $copy, "--in-dir", $swappedFolder, "--out-dir", $patch2))
$copy3 = New-PatchedCopy "game-patch2" $patch2
[void](Invoke-Scic "8-export-swapped" @("pic", "export", $copy3, "--all", "--cel", "all", "--out-dir", $export3))

function Get-YesNo($value) { if ($value) { "yes" } else { "no" } }

$rows = @()
foreach ($number in ($original.Keys | Sort-Object)) {
    $pic = $original[$number]
    $exported = Test-Path -LiteralPath (Join-Path $export1 "$number.toml")
    $imported = $exported -and (Test-HasPatch $patch1 $number)
    $row = [ordered]@{ number = $number; format = $pic.Format; cels = $pic.Cels; exported = (Get-YesNo $exported); cel_files = ""; imported = ""; crc_same = ""; images_same = ""; swap_same = ""; note = $pic.Error }
    if ($exported) {
        $celFiles = @(Get-PicImages $export1 $number).Count
        $row.cel_files = Get-YesNo ("$celFiles" -eq $pic.Cels)
        if ("$celFiles" -ne $pic.Cels) { $failures.Add("pic ${number}: the export of every cel wrote $celFiles BMP file(s), and the pic has $($pic.Cels) cel(s)") }
        $row.imported = Get-YesNo $imported
        if ($imported) {
            $after = $patched[$number]
            if (-not $after -or ($after.Source -ne "patch")) {
                $row.note = "the copy with the patch file does not read the pic from it"
                $failures.Add("pic ${number}: the game copy with patch1 does not read the pic from the patch file")
            }
            elseif ($pic.Format -eq "vga2") {
                $row.crc_same = Get-YesNo ($after.Crc32 -eq $pic.Crc32)
                if ($after.Crc32 -ne $pic.Crc32) { $failures.Add("pic ${number}: the patch file with no change has the CRC-32 $($after.Crc32), and the pic has $($pic.Crc32)") }
                # The header of an SCI2 pic patch: 81 81 00 00.
                $header = Get-PatchHeader $patch1 $number
                if ($header -ne "81810000") {
                    $row.note = "the patch file starts with $header"
                    $failures.Add("pic ${number}: the patch file starts with $header, and the patch file of an SCI2 pic starts with 81810000")
                }
            }
            $images = Get-PicImages $export1 $number
            $same = ($images.Count -gt 0) -and (@($images | Where-Object { -not (Test-SameBytes $_.FullName (Join-Path $export2 $_.Name)) }).Count -eq 0)
            $row.images_same = Get-YesNo $same
            if (-not $same) { $failures.Add("pic ${number}: an image of the game copy with patch1 is not the image of the first export") }
            if (Test-HasPatch $patch2 $number) {
                $swapSame = @(Get-PicImages $swappedFolder $number | Where-Object { -not (Test-SameBytes $_.FullName (Join-Path $export3 $_.Name)) }).Count -eq 0
                $row.swap_same = Get-YesNo $swapSame
                if (-not $swapSame) { $failures.Add("pic ${number}: an image of the game copy with patch2 is not the swapped image") }
            }
            else {
                $row.swap_same = "no"
                $failures.Add("pic ${number}: the import of the swapped images wrote no patch file")
            }
        }
    }
    $rows += [pscustomobject]$row
}
$csv = Join-Path $run "roundtrip.csv"
$rows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8

if (-not $Keep) {
    foreach ($folder in @($copy, $copy2, $copy3, $export1, $patch1, $export2, $swappedFolder, $patch2, $export3)) {
        [IO.Directory]::Delete($folder, $true)
    }
}

$exportedCount = @($rows | Where-Object { $_.exported -eq "yes" }).Count
$importedCount = @($rows | Where-Object { $_.imported -eq "yes" }).Count
Write-Host ("Pics: {0}. Exported: {1} ({2} with one BMP file for each cel). Imported with no change: {3}. Images with two entries swapped: {4}." -f $rows.Count, $exportedCount,
    @($rows | Where-Object { $_.cel_files -eq "yes" }).Count, $importedCount, $swappedImages)
Write-Host ("CRC-32 the same (SCI2): {0}. Images the same: {1}. Swapped images the same: {2}." -f @($rows | Where-Object { $_.crc_same -eq "yes" }).Count,
    @($rows | Where-Object { $_.images_same -eq "yes" }).Count, @($rows | Where-Object { $_.swap_same -eq "yes" }).Count)
Write-Host "CSV: $csv"
if ($failures.Count -gt 0) {
    Write-Host ("{0} failure(s):" -f $failures.Count)
    $failures | Select-Object -First 30 | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "The round trip is exact."
exit 0
