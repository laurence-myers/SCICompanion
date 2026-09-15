<#
    Runs the SCI Companion unit tests with vstest.console.exe.

    Build first, for example:
      MSBuild.exe SCICompanion.sln -m -p:Configuration=Release -p:Platform=Win32
    Then:
      .\UnitTests\RunTests.ps1

    Runs the whole suite by default, so CI (which invokes this script with no
    arguments) cannot silently skip a test. Pass -Filter "FullyQualifiedName~..."
    to run a subset locally; -All is kept as an explicit "everything" override.

    Bytecode oracle env vars (read by TestBytecodeOracle.Oracle_ExistingGame,
    which is skipped and green unless SCICOMP_ORACLE_GAME is set):
      SCICOMP_ORACLE_GAME  absolute path to a game's resource.map folder (not a
                           variant parent). The game is copied to a temp folder
                           first, so the original is never modified. SCI0..SCI1.1
                           games run the full round-trip oracle; SCI2+ games are
                           checked for load/parse only.
#>
param(
    [string]$Configuration = "Release",
    # Empty by default. A non-empty value is passed straight to vstest's
    # /TestCaseFilter for a local subset run and overrides the switches below.
    [string]$Filter = "",
    # -All runs every test, including the Integration category. With no switch,
    # the run is unit-only: the Integration category is excluded.
    [switch]$All,
    # -Integration runs ONLY the integration tests (threads, child processes,
    # windows, filesystem), separately from the fast unit run.
    [switch]$Integration,
    # -BlameHang makes vstest collect a hang dump and kill a wedged test. Pair it
    # with -Integration in CI so a real deadlock fails the job instead of hanging.
    [switch]$BlameHang,
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
# Integration test classes carry "Integration" in their name (the C++ test
# adapter filters on FullyQualifiedName, not on trait attributes). The default
# run excludes them, so the unit leg stays fast and never spawns a process.
# -Integration runs only them; -All runs everything; -Filter overrides all this.
$effectiveFilter = ""
if ($Filter) {
    $effectiveFilter = $Filter
} elseif ($Integration) {
    $effectiveFilter = "FullyQualifiedName~Integration"
} elseif (-not $All) {
    $effectiveFilter = "FullyQualifiedName!~Integration"
}
if ($effectiveFilter) {
    $vstestArgs += "/TestCaseFilter:$effectiveFilter"
}
if ($BlameHang) {
    $vstestArgs += "/Blame:CollectHangDump;TestTimeout=120000"
}

# The snapshot test writes each decompiled script to SnapshotActuals next to
# the test DLL. Clear the last run's output first, so -UpdateSnapshots cannot
# copy stale files from an earlier build.
$actuals = Join-Path $repoRoot "$Configuration\SnapshotActuals\SCI1.1"
$bytecodeActuals = Join-Path $repoRoot "$Configuration\SnapshotActuals\Bytecode\SCI1.1"
if ($UpdateSnapshots) {
    if (Test-Path $actuals) { Remove-Item -Recurse -Force $actuals }
    if (Test-Path $bytecodeActuals) { Remove-Item -Recurse -Force $bytecodeActuals }
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

    # The bytecode-snapshot test (emitted .scr/.hep hex). Only present when that
    # test ran, so guard on the actuals existing.
    if (Test-Path $bytecodeActuals) {
        $committedBytecode = Join-Path $PSScriptRoot "Files\Decompile\Snapshots\Bytecode\SCI1.1"
        New-Item -ItemType Directory -Force -Path $committedBytecode | Out-Null
        Copy-Item -Path (Join-Path $bytecodeActuals "*.hex") -Destination $committedBytecode -Force
        $deployedBytecode = Join-Path $repoRoot "$Configuration\TestFiles\Decompile\Snapshots\Bytecode\SCI1.1"
        New-Item -ItemType Directory -Force -Path $deployedBytecode | Out-Null
        Copy-Item -Path (Join-Path $bytecodeActuals "*.hex") -Destination $deployedBytecode -Force
        Write-Host "Updated bytecode snapshots in $committedBytecode"
    }
    exit 0
}

exit $testExit
