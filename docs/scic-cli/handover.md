# `scic` CLI work: handover

Read this first in a new session. The design is in `docs/scic-cli/plan.md`.
Update this file in the same commit as each step.

## State

- Branch: `feat/scic-cli`, based on `master` at `0dc1fef5`. Not pushed.
- Current step: the B3b review, then F2. F1, A1, A2, B1, B2 and B3a are
  committed and reviewed, with their review fixes. B3b (the decompile
  path) is committed; its review is running.
- 2026-09-23: at your request, the branch history was rewritten twice:
  no commit adds a copyright header, and every commit uses the term
  "exception boundary". Every SHA on the branch changed; the SHAs in this
  file and in the commit messages were changed to match.
- Baseline on `0dc1fef5`: the Release build passes; the unit suite passes
  217 of 217 tests in about 4 minutes. After F1: 237. After A1: 242. After
  the F1 review fixes: 243. After the A1 review fixes: 248. After A2: 254.
  After B1: 264. After B2: 269. After the A2 review fixes: 272. After the
  B1 review fixes: 276. After the B2 review fixes: 278. After B3a: 280. After B3b: 282. After the B3a review fixes: 285.
  The integration suite has 20 tests.
- A full rebuild shows about 49 old warnings: C4840 in Prof-UIS, C5033 and
  C4018 in GIFLIB and CrystalEdit, one in a Windows SDK header, and C4996
  (`getenv`) and C4267 in the UnitTests helpers (`DecompileHelper.cpp`,
  `TestDecompile.cpp`, `TestBytecodeOracle.cpp`, `TestDecompileBatch.cpp`).
  A warning in a file that a step changed, which the parent commit did not
  have, is a regression.

## Progress

The steps are the PRs of plan section 9, in this order. There is one commit
for each step, and a follow-up commit if the review finds a problem.

| Step | Status | Commit | Review |
|---|---|---|---|
| F1 Result foundation | done | `04361133`, review fixes `b499f9ac` | FIX: 2 should-fix, 8 nits; fixed except the Gdiplus `Status`/`Ok` name overlap in `RoomExplorerView.cpp` (latent, not used) |
| A1 Deferred writes | done | `1c4d1c6f`, review fixes `258ce43c` | FIX: 3 should-fix, 5 nits. Fixed: savepoints (an abandoned inner batch withdraws its resources and puts back what it replaced), the audio repackage stops on a failed map save, every queued type reloads, guards around the context text and the notifications, tests on SCI1.1 and for mixed destinations and a read-only volume, the plan row. Moved to A2: the audio cache writer swallows its errors and saves its audio map through the GUI wrapper. Already fixed by `b499f9ac`: the last-error capture in `util.cpp`. |
| A2 Patch writer, size check | done | `7432a479`, review fixes (the commit after `3be03ff1`) | FIX: 1 should-fix, 6 nits (it also reviewed `258ce43c`). Fixed: the patch writer checks every existing target (read-only, locked) before the first rename and removes the `.bak` files left after a failed rename; a repackage inside an open batch is refused; the audio cache is marked out of date before its map save; `PerformChecks` runs inside the exception boundary; the size error names the resource and no longer says "A Audio"; the plan's statements on atomic commits and on the old audio cache behaviour; README "What's new". Left as known gaps (below): `Cancelled` for check failures that are not a choice, and a rename that fails after the checks. |
| B1 GameSession, core log | done | `13a786ac`, review fixes (the commit after `d1221472`) | FIX: 2 should-fix, 8 nits. Fixed: only a GUI `AppState` installs itself as the log sink, and it removes itself with a compare-exchange (`RemoveCoreLogSink`); `Open("")` is a Usage error; `TryOpen` is inside the exception boundary as a whole; `AppState::Write` deletes MFC exceptions; `LogInfo` uses `CoreLogFormatV`, with `_Printf_format_string_`; three format-string bugs (`Vocab99x.cpp` `%d` for a name, two dialogs that used the error text as the format, and leaked their `COleException`); the grammar load uses `std::call_once`; the headless `SafeMessageBox` gives the safe answer for every button set; test hygiene. Left: see "Decisions" (the GUI open path and the B3 guard test). |
| B2 Script text loader | done | `3be03ff1`, review fixes (the commit after `af7cdb5f`) | FIX: 1 should-fix, 1 nit. No difference from the editor in about 46,000 files (a differential probe), 723 parses and 5 compiles. Fixed: `LoadScriptText` names the file for a thrown failure and refuses a file over 64 MB (`Unsupported`); tests at the exact 32 KB edge of the style rule, and a stream walk; the exception boundary reports an MFC `CMemoryException` as "out of memory" (an F1 gap the review found). |
| B3a Compile path on the session | done | `8fe055d0`, review fixes (the commit after `f646dd52`) | FIX: 1 should-fix, 6 nits, 1 question. Fixed: the compile-all tests fail on a `&getpoly` message (a missing polygon is only a message, so a wrong polygon folder passed every test); tests for the codepage set by Game Properties, for the table saves, and for a compile error with no class hints; no polygon file read when the script has no game folder; `CompileLog::SummarizeAndReportErrors` moved to the engine, and the GUI plays the error sound; "Ignoring class" is Info; `OutputScriptStrings.h` hygiene; stale plan references. |
| B3b Decompile path on the session, `appState` check rule | done | the commit after `8fe055d0` | next |
| F2 Engine errors as values | not started | | |
| K1 `and`/`or` value semantics | not started | | |
| K2 `.sco` exports from the public block | not started | | |
| K3 Species order from compiled scripts | not started | | |
| K4 `#` in selector names | not started | | |
| K5 `proc<N>_<M>` for a missing script | not started | | |
| K6 No `vocab.000`; `/` in paths | not started | | |
| S3 ScriptCatalog, script names without `game.ini` | not started | | |
| S1 Compile destination | not started | | |
| S2 CompileBatch | not started | | |
| S4 DecompileRun | not started | | |
| C1 CLI project, `script list` | not started | | |
| C2 `script decompile`, `script sco` | not started | | |
| C3 `script compile` | not started | | |
| C4 CI and documents | not started | | |
| E1 Core library (optional) | not started | | |

## How to work a step

1. Implement the step. Keep CRLF line endings (AGENTS.md).
2. Build: `MSBuild.exe SCICompanion.sln -m -p:Configuration=Release -p:Platform=Win32`
   (full path: `E:\Apps\Dev\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`).
   Use `-t:Rebuild` after a change to a widely included header, or the stale
   precompiled header gives access violations in unrelated tests.
3. Test: `.\UnitTests\RunTests.ps1` (unit), `-Integration`, or `-Filter`.
   Read the counts in `TestResults\UnitTests.trx`.
4. Negative check: make the new test fail on purpose (revert the fix or
   break it), confirm it fails, then restore. Put the break on a path that
   the test runs. After you restore a file from a copy, set its write time
   to now: `Copy-Item` keeps the old time, so MSBuild does not rebuild,
   and the binary keeps the break. The break must use the value that it
   reads (for example, in a condition): the Release build removes a read
   with no effect, such as `appState->GetResourceMap().Helper();`.
5. Run `.\UnitTests\Tools\CheckFailureHandling.ps1` (after F1).
6. Commit the step with this file updated. Message: `<type>(scic): <step> <summary>`.
7. Adversarial review: one subagent in an isolated git worktree, told the
   exact commit SHA, told to refute the step, to build with warnings
   visible, and to run the tests. Fix real findings in a follow-up commit.
   Then remove the worktree (`git worktree remove --force`, `git worktree
   prune`) and its branch.

## Gotchas

- Do not put a copyright notice (or the GPL header block) in a new file.
  This is your rule (2026-09-23). Vendored third-party sources keep their
  own notices.
- PowerShell unrolls `@(@('old', 'new'))` into `@('old', 'new')`. An
  edit loop over such a pair list then replaces single characters (on
  2026-09-23 this replaced every tab in two files). Build the edit list
  from objects (`[pscustomobject]@{ Old = ...; New = ... }`), check that
  each anchor matches exactly once before the first write, and keep a
  backup.
- `sed -i` in Git Bash rewrites a CRLF file with LF endings. Edit project
  files with the Edit tool or with PowerShell (`[IO.File]::ReadAllText`,
  keep the BOM). A new file from a tool that writes LF needs the CRLF
  conversion in AGENTS.md.
- Every project reads `Directory.Build.props`. `/we4834` (a discarded
  `Result` is an error) applies to our projects, not to Prof-UIS.
- `tl-expected\tl\expected.hpp` is vendored unchanged (LF endings, SHA-256
  in `tl-expected\README.md`). Settings such as `TL_ASSERT` go in
  `Src\Core\Result.h`.

## Decisions and deviations from the plan

- The open questions Q4 to Q13 use the plan's recommendations.
- Revision 4 of the plan (2026-09-23) added phase K and the `scicompile`
  features (plan section 14), at your request.
- 2026-09-23, your change of plan: the CLI must work on a game that SCI
  Companion never opened, so it must not rely on `game.ini`. Plan revision
  5 (plan section 3.4) records how: a script-name map from `game.ini` if
  present, `src\*.sc`, `src\*.sco`, derived names, then `nNNN`. S3 now
  comes first in phase S. The CLI creates `game.ini` only with
  `script decompile --game-ini create`.
- `util.cpp` on `master` starts with a stray code fragment (from commit
  `136c9ba1`) that only compiles because MSVC skips everything before
  `#include "stdafx.h"`. It was meant to free the buffer in
  `GetMessageFromLastError`. Commit `cb6f55a7` moves it there.
- A1 keeps `HRESULT CResourceMap::AppendResource(const ResourceBlob&)` for
  the GUI (it shows the error text) and adds `sci::Status WriteResource`
  (no UI) for the engine and the services.
- A1 review: the deferred queue holds `std::unique_ptr<ResourceBlob>`, so
  that an abandon (in a destructor) can put a replaced copy back without a
  copy that could throw. `Pending()` returns pointers.
- Known gap, not in any step yet: when the package writer cannot replace a
  volume or the map, it leaves `resource.map.bak` and `resource.00N.bak`
  behind. The game stays consistent, because the writer only appends to a
  volume and replaces the map last.
- A2: `sci::DataError` can carry a whole `sci::Error`, and the exception
  boundary (`sci::Guard`) gives it back unchanged. Code inside an
  exception boundary that cannot return a `Result` (for example
  `AppendResources`, which returns `AppendBehavior`) throws
  `DataError(status.error())`.
- Terminology: `sci::Guard` is the "exception boundary" (your correction,
  2026-09-23). Use that term in code, documents and commit messages.
- Terminology: code broken on purpose to show that a test catches the
  fault is a "negative check" (AGENTS.md). Do not use the word "mutant";
  write this rule into every review-subagent prompt.
- A2: `ResourceBlob.h` now uses `sci::Status`, so it includes `Result.h`
  itself: two precompiled headers include `ResourceBlob.h` before
  `Result.h`.
- Known gap, not in any step yet: `AudioCacheResourceSource::RemoveEntries`
  (the delete path) still swallows its errors and saves its audio map
  through the GUI `AppendResource`.
- B1: `GameSession` takes no log sink. The host installs the one global
  sink: `AppState` in the GUI (it writes to the log file from the GUI's
  command line; before B1, `LogInfo` wrote there only, and cut lines at
  260 characters), `ScopedCoreLogSink` in the CLI and the tests.
  B3a added `SessionOptions::warnOnUnusedInstances`, where the compiler
  reads it.
- B1: `CResourceMap::SetDataFolder` replaces `SetIncludeFolderForTest` and
  also moves the Decompiler folder. `DecompilerConfig.cpp` still reads
  the static `GameFolderHelper::GetIncludeFolder()` (B3b).
- B1: P17 was worse than the plan said: `DependencyTracker` took the
  setting by value and kept a reference to that parameter.
- B1: the "corrupt map" test of the plan became a "missing map" test. A
  garbage `resource.map` still opens, because the format detection is
  permissive; F2 can add a check.
- The test helper `CopyGameFromModuleFolder` copies a template without an
  `AppState`, for tests that must run with `appState == nullptr`.
- B1 review: `SetGameFolder` (the GUI path) now also turns a non-standard
  exception from the version sniff (for example a `CException`) into its
  "Unable to open resource map" box and a `CUserException`; before, such
  an exception left the function as it was. No current code throws one
  there.
- For B3: the guard test cannot rely on a crash from `appState->LogInfo`
  with no `AppState`, because the B1 `LogInfo` does not touch `this`. B3a
  and B3b changed the engine's calls to `CoreLog`, and B3b added the check
  rule against `appState` in the engine folders.
- B2: the editor's line rule is odder than the plan said (plan section
  2.8 now has it): a CR-only file is one line, "LF CR" is a style, and a
  NUL ends a line. `SplitScriptText` copies it exactly.
- B2 moved the engine's loads (`NewCompileScript`, `SimpleCompile`,
  `_ParseScript`, the header loads, `DecompilerConfig`) to
  `LoadScriptText`. The GUI (`InsertObject`, `MainFrm`, `ClassBrowser`,
  the editor, autocomplete) keeps `CCrystalTextBuffer`.
  `CrystalScriptStream.h` still includes `CCrystalTextBuffer.h` for the
  GUI constructors; phase E can split it.
- Known gap for S2: `NewCompileScript` returns false with no log line
  when it cannot read the script file (as before B2).
- For C3: the editor's line rule is kept by design, so a CLI user can see
  its effects. A bare LF in a CR LF file does not start a line, so a
  diagnostic's line number can differ from other editors. A CR-only file
  is one line; if it starts with a `;` comment, it compiles to an empty
  script with no error. C3 can warn about such files.
- Known gap for S2 and C3: `WriteResource(entity)` gives `Cancelled` when
  `PerformChecks` says no. With no GUI, a check's yes/no question gets
  "no", and some checks are not a choice (duplicate message tuples, an
  audio map entry over 16 MB), so the CLI must not map that to exit
  code 7 without care. The Pic checks open a real dialog even with no GUI
  (`CDontShowAgainDialog::DoModal`); the script commands do not save pics.
- Known gap: a patch rename can still fail after the checks (another
  program locks the file at that moment). The renames before it stay
  done; the `.bak` files that are left are removed.

- B3 is split into two commits (plan section 9): B3a, the compile path;
  B3b, the decompile path, the check rule against `appState` in the
  engine folders, and the decompile-all guard test.
- B3a: the text codepage is a process-wide setting (`SetTextCodepage` and
  `GetTextCodepage` in `Text.h`), like the log sink. The game open sets it
  from `game.ini` (437 when there is no `game.ini` or no `Codepage` key),
  and `GameFolderHelper::SetCodepage` (the Game Properties dialog) sets it
  too. Before, each conversion read `game.ini`, so a hand edit of
  `game.ini` took effect at once; now it takes effect at the next open.
  With two games open in one process, the last open wins; the GUI and the
  CLI open one game at a time.
- B3a: the class browser is the session's optional `IClassHints`
  (`Src\Core\ClassHints.h`). `SCIClassBrowser::ScriptThatExports` takes
  the browser's lock itself (the mutex is recursive). The GUI callers and
  the test helpers (`CompileFixture`, `TestCompile::_DoItHelper`) still
  hold the browser's lock around `NewCompileScript`, as the old
  `NewCompileScript` did: the browser's background reload parses the same
  scripts and reads the game. The CLI has no browser and takes no lock.
- B3a: `DependencyTracker::ClearScript` moved from `NewCompileScript` to
  the GUI callers, inside the same lock. It keys on the script title, so
  the result is the same.
- B3a: `AppState::GetSession()` copies the GUI's "warn on unused
  instances" setting into the session's options at each call. A change in
  the Preferences dialog takes effect at the next compile, as before.
- B3a: the compile gives the script its polygon folder
  (`Script::SetPolyFolder`). Other parses (the class browser,
  `SimpleCompile`) use `poly` next to the script's folder. For a script in
  `<game>\src`, that is the same folder as before. A script path with no
  parent folder reads no polygon file (B3a review; before the fix, it read
  the root of the drive). `ScriptId` splits a path only on `\` (K6).
- B3a: the polygon loader parses a `.shp` file with no defines (before:
  the defines of the game's version). The defines only choose `#if` and
  `#ifdef` code, which the polygon writer never writes, so the result is
  the same.
- B3a: `SimpleCompile` takes the version (or the defines),
  `ExtractScriptStrings` the version, and `ValidateSaids` the resource
  map. `SelectorTable::Save`, `SpeciesTable::Save` and
  `SpeciesTable::PurgeOldClasses` take the resource map. `GlobalClassTable`
  uses the helper that its `Load` gets.
- B3a: four of the five `Vocab99x.cpp` log lines are warnings now
  (`CoreLogFormat(LogLevel::Warning, ...)`), as the codec lines are since
  B1. In the GUI's log file, they get a `warning:` prefix. "Ignoring
  class" is Info (B3a review): it runs at each open of a game with
  leftover classes (KQ5CD), once for each class.
- B3a review: `CompileLog::SummarizeAndReportErrors` is in the engine
  (`CompileScript.cpp`). The GUI's `_DoErrorSummary` plays the error
  sound.
- B3a review: a compile that cannot find a polygon (`&getpoly`) reports a
  message, not an error. The compile-all tests fail on such a message.
- B3a: the `NoDbugStr` path (`CompileContext::Helper()`, used only for a
  `DbugStr` kernel call in `Compile.cpp`) has no test; it is
  inspection-verified. The templates do not call `DbugStr`.

- B3b: `DecompileBatch`, `DecompileScript` and `CreateDecompilerConfig`
  take the resource map, and get the helper from it. The decompile dialog
  passes `appState->GetResourceMap()`; before, the batch read it through
  `appState` on the same worker thread.
- B3b: `CreateDecompilerConfig` reads `sci.sh` and `keys.sh` from the
  include folder of the resource map (the data folder). Before, it used
  the static `GameFolderHelper::GetIncludeFolder()` (the folder of the
  running exe). In the GUI, that is the same folder. In the tests, it is
  now the module folder; the templates have no `src\Decompiler.ini`, and
  the config uses the defines only after it reads that file, so the test
  results do not change.
- B3b: `ConvertToSCISyntaxHelper` has no default for its lookups. A new
  overload takes a helper and loads the lookups from it (the three GUI
  callers use it). With null lookups, the formatter no longer loads them
  itself.
- B3b: `DecompileScript` and `FixDuplicateObjectNames` moved to
  `Src\Compile\DecompileScript.cpp`. The 3-argument `DecompileScript`
  (P15, dead code that passed a null config) is gone.
- B3b: the log calls in `DecompilerFallback`, `Disassembler`,
  `PaletteOperations`, `Sound` and `View` go to `CoreLog` (Warning for a
  failure, Info for a note). The "Empty loop found" call in `View.cpp` had
  no null check, so a view with an empty loop crashed a load with no
  `AppState`. The two `Sound.cpp` messages lost their trailing line break.
  The commented-out call in `DecompilerNew.cpp` now uses `CoreLog` too.
- B3b: the check rule `appstate-in-engine` covers `Src\Core`,
  `Src\Compile` and `Src\Resources`. The allowlist has the two audio-cache
  sites (`GameFolderHelper.cpp`, `ResourceMapOperations.cpp`: the
  `AudioCacheResourceSource` needs the `CResourceMap`) and the GUI flags of
  the pic code (`Pic.cpp` `_fDontCheckPic`, `PicDrawManager.cpp`
  `_fNoGdiPlus`, `PicOperations.cpp` the clipboard format). The script
  commands do not reach them. `Src\Util` is not in the rule, because it
  also holds GUI code; its codecs and script-text code do not use
  `appState`. Phase E1 makes the build enforce the layers.
- For E1: some engine files still include `AppState.h` with no use
  (`Audio.cpp`, `AudioMap.cpp`, `Message.cpp`, `ResourceMap.cpp`,
  `Sync.cpp`, `Vocab000.cpp`). B3b removed it from the `Src\Compile` files.
## Next action

Read the B3b review (running) and fix any real finding. Then F2 (plan
section 9).
