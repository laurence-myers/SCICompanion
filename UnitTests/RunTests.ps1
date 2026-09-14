<#
    Runs the SCI Companion unit tests with vstest.console.exe.

    Build first, for example:
      MSBuild.exe SCICompanion.sln -m -p:Configuration=Kawa -p:Platform=Win32
    Then:
      .\UnitTests\RunTests.ps1

    Defaults to the decompiler suite. The wider suite has pre-existing failures
    (SCI0 compile, pic pixel diffs) that are unrelated to the decompiler, so
    pass -All to run everything.
#>
param(
    [string]$Configuration = "Kawa",
    [string]$Filter = "FullyQualifiedName~TestDecompile|FullyQualifiedName~TestAstPasses|FullyQualifiedName~TestShippedFiles",
    [switch]$All,
    # After the run, copy the decompiled template snapshots the test wrote into
    # the source tree, so an intended output change is committed with the code.
    [switch]$UpdateSnapshots
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $repoRoot "$Configuration\UnitTests.dll"
if (-not (Test-Path $dll)) {
    throw "Test DLL not found: $dll. Build the solution in $Configuration first."
}

# Find vstest.console.exe in the installed Visual Studio.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found: $vswhere"
}
$vsPath = & $vswhere -latest -products '*' -property installationPath
if (-not $vsPath) {
    throw "Visual Studio not found by vswhere."
}
$vstest = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe"
if (-not (Test-Path $vstest)) {
    throw "vstest.console.exe not found: $vstest"
}

$resultsDir = Join-Path $repoRoot "TestResults"
$vstestArgs = @(
    $dll,
    "/Platform:x86",
    "/logger:trx;LogFileName=UnitTests.trx",
    "/ResultsDirectory:$resultsDir"
)
if (-not $All) {
    $vstestArgs += "/TestCaseFilter:$Filter"
}

# The snapshot test writes each decompiled script to SnapshotActuals next to
# the test DLL. Clear the last run's output first, so -UpdateSnapshots cannot
# copy stale files from an earlier build.
$actuals = Join-Path $repoRoot "$Configuration\SnapshotActuals\SCI1.1"
if ($UpdateSnapshots -and (Test-Path $actuals)) {
    Remove-Item -Recurse -Force $actuals
}

Write-Host "Running: $vstest $($vstestArgs -join ' ')"
& $vstest @vstestArgs
$testExit = $LASTEXITCODE

if ($UpdateSnapshots) {
    if ($testExit -ne 0) {
        Write-Host "Tests failed (exit $testExit); snapshots not updated."
        exit $testExit
    }
    # Copy the actuals into the committed snapshot folder.
    $committed = Join-Path $PSScriptRoot "Files\Decompile\Snapshots\SCI1.1"
    if (-not (Test-Path $actuals)) {
        throw "No snapshot actuals at $actuals. Run the snapshot test (no -Filter that excludes it)."
    }
    New-Item -ItemType Directory -Force -Path $committed | Out-Null
    Copy-Item -Path (Join-Path $actuals "*.sc") -Destination $committed -Force
    # The test reads the copy next to the DLL, which the build refreshes. Update
    # it too, so the next run passes without a rebuild.
    $deployed = Join-Path $repoRoot "$Configuration\TestFiles\Decompile\Snapshots\SCI1.1"
    New-Item -ItemType Directory -Force -Path $deployed | Out-Null
    Copy-Item -Path (Join-Path $actuals "*.sc") -Destination $deployed -Force
    Write-Host "Updated snapshots in $committed"
    exit 0
}

exit $testExit
