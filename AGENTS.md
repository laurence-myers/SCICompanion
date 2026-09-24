# AGENTS.md

Guidance for AI agents and human contributors working in this repository. Read
this before making changes.

## What this is

SCI Companion — a Windows MFC IDE for Sierra SCI games (SCI0–SCI1.1): compiler,
decompiler, and resource editors. Most of the code is in `SCICompanionLib\Src`;
the `SCICompanion` project is a thin `.exe` wrapper over `SCICompanionLib`.
The `SCICompanionCli` project is a thin wrapper too: it builds `scic.exe`,
the command-line tool (`scic script list`, `decompile`, `sco` and
`compile`), whose code is in `SCICompanionLib\Src\Cli`. Its design is in
`docs\scic-cli\plan.md`. Tests are in `UnitTests`.

## Building

- Build the **whole solution** — individual `.vcxproj` files do not build in
  isolation (project and include-path dependencies). From the repo root:

  ```
  MSBuild.exe SCICompanion.sln -m -p:Configuration=Release -p:Platform=Win32
  ```

- The target is **Release | Win32** (32-bit), MSVC toolset **v143** (Visual
  Studio 2022), C++17, MBCS, signed `char`.
- **Debug | Win32 does not build locally** (a vendored dependency has no
  Debug|Win32 configuration, among other issues). Use Release for local builds
  and CI.
- The build puts `SCICompanion.exe`, `scic.exe` and `UnitTests.dll` in
  `Release\`. The app's post-build copies the data that they need next to
  them (for example `include\`, `Decompiler\`, `Objects\` and
  `TemplateGame\`). `scic.exe` reads `include\` and `Decompiler\` from its
  own folder; `--data-dir` or `SCIC_DATA_DIR` gives another folder.

## Testing

- Tests use the VSTest C++ unit-test framework. Run them with the helper script:

  ```
  .\UnitTests\RunTests.ps1                                   # unit tests (fast; excludes integration)
  .\UnitTests\RunTests.ps1 -Integration                     # integration tests (threads, processes, windows)
  .\UnitTests\RunTests.ps1 -All                             # everything
  .\UnitTests\RunTests.ps1 -Filter "FullyQualifiedName~Foo" # a subset
  ```

- Integration tests are those whose test-class name contains `Integration`; the
  default run excludes them, so it never spawns a thread or a process.
- The command-line tool: `TestCli` runs `cli::RunCli` in the test process,
  with a console that keeps the output, on a temp copy of a template game.
  `TestCliIntegration` (an integration test) starts `Release\scic.exe`. CI
  also has a smoke step that runs `scic script list`, `script decompile
  --all` and `script compile --all` on copies of the SCI1.1 template.
  `UnitTests\Tools\CliCorpusSweep.ps1` runs the same commands on copies of
  a local game library (local use only). See `UnitTests\README.md`.
- Read pass/fail counts from `TestResults\UnitTests.trx` (or
  `IntegrationTests.trx`) — the `<Counters>` element under
  `TestRun/ResultSummary`. The full unit suite takes a few minutes.
- Prefer a test that **fails before the fix and passes after it** (a "negative
  check"): confirm it actually catches the bug, then confirm the fix makes it
  pass. Be honest in the PR about anything that is only inspection-verified.
- If you add a `.cpp` test file, register it in both `UnitTests\UnitTests.vcxproj`
  (`ClCompile`) and `UnitTests\UnitTests.vcxproj.filters`.

## Gotchas

- **All files use CRLF line endings.** Keep them. A multi-line find/replace that
  was authored with LF will not match a CRLF file — anchor on a single line, or
  splice with PowerShell. To rewrite a file as CRLF:

  ```
  $p = 'path\to\file'
  $c = [IO.File]::ReadAllText($p)
  $c = ($c -replace "`r`n","`n") -replace "`n","`r`n"
  [IO.File]::WriteAllText($p, $c, (New-Object System.Text.UTF8Encoding($false)))
  ```

- **Use PowerShell for shell scripts.** The primary shell on this project is
  Windows PowerShell. Mind its quirks: no `&&` / `||` chaining (use `;` and
  `if ($?) { ... }`), no ternary / null-coalescing operators, and redirecting a
  native executable's stderr is fiddly. A POSIX `sh` (Git Bash) is also
  available for portable scripts.
- **The app version lives in the `.rc` files.** Update both
  `SCICompanion\SCICompanion.rc` and `SCICompanionLib\SCICompanionLib.rc`
  (`FILEVERSION` / `PRODUCTVERSION` and their string values), and the About-box
  version text in `SCICompanionLib\SCICompanionLib.rc`. The command line
  `scic.exe` takes its version from `SCICompanionLib\Src\Cli\CliVersion.h`;
  update it too (a unit test checks that it equals `SCICompanionLib.rc`).
- **The About-box credits** are built in `AppState::GetAboutText()`
  (`SCICompanionLib\Src\Util\AppState.cpp`), not in the `.rc`.

## Failure handling

The engine and the command-line tool follow the failure-handling model of
`docs\scic-cli\plan.md` (section 6). In short:

- Return an error as a value: `sci::Result<T>`, or `sci::Status` when there
  is no value (`SCICompanionLib\Src\Core\Result.h`). Make it with
  `sci::Fail(code, message)`, and pass it on with `SCI_TRY` and
  `SCI_TRY_ASSIGN`. A discarded `Result` is a build error (`/we4834`, in
  `Directory.Build.props`). Do not call `.value()`: test the result first.
- Compile errors and decompiler warnings are diagnostics: data about the
  input, not a failure of the call.
- `sci::Guard(context, fn)` is the exception boundary. It turns an exception
  that escapes `fn` into an error: the code of a `sci::DataError`, `Io` for
  an MFC `CFileException`, else `Internal`. So a batch goes on with the next
  script. Call it the "exception boundary".
- Do not add an empty `catch (...)`, or a `throw std::exception(...)` (throw
  `sci::DataError`, or return a `Result`). In `Src\Core`, `Src\Compile` and
  `Src\Resources`, do not add an `AfxMessageBox`; in those folders and in
  `Src\Util`, do not use the GUI object `appState` (take the session, the
  resource map or the helper as a parameter). The CI check
  `UnitTests\Tools\CheckFailureHandling.ps1` fails on a new site. Its
  allowlist holds the old sites; when you remove old sites, run the check
  with `-Update` and commit the allowlist.
- The exit codes of `scic.exe` are in plan section 8 (`ExitCodeForReport` in
  `SCICompanionLib\Src\Cli\ExitCodes.cpp`).

## Vendored code

- `SCICompanionLib\Src\GIFLIB` is a **vendored, modified** copy of giflib. Read
  its `README.md` before touching it — it lists the local fork modifications and
  how to re-vendor without losing them.

## Documentation

- **Keep `README.md` updated with a change summary.** When you make user-visible
  changes, add to or update the broad, user-friendly highlights in the
  "What's new" section (categories, not individual fixes).
- On a release, bump the version in both `.rc` files and in
  `SCICompanionLib\Src\Cli\CliVersion.h`, and update the About box.

## Pull requests

- Keep each PR atomic: one logical change per branch/PR.
- Describe the change plainly — what it was before, what it is after, and how it
  was tested.
