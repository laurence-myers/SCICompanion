# `scic` CLI work: handover

Read this first in a new session. The design is in `docs/scic-cli/plan.md`.
Update this file in the same commit as each step.

## State

- Branch: `feat/scic-cli`, based on `master` at `0dc1fef5`. Not pushed.
- Current step: C1. F1, A1, A2, B1, B2, B3a, B3b, F2, K1 to K6, S3, S1 and
  S2 (S2a, S2b, S2c) are committed and reviewed, with their review fixes
  (the S1 and S2 fixes: the commit after `7d26d9d6`). S4 (S4a, S4b) is
  committed; its review is running. The fixes of the reviews of
  `bc827391` and `fe02c12a` (`8a322b32`), and of `de2fb8dc` and
  `8a322b32` (`7d26d9d6`), are committed.
- 2026-09-23: at your request, the branch history was rewritten twice:
  no commit adds a copyright header, and every commit uses the term
  "exception boundary". Every SHA on the branch changed; the SHAs in this
  file and in the commit messages were changed to match.
- Baseline on `0dc1fef5`: the Release build passes; the unit suite passes
  217 of 217 tests in about 4 minutes. After F1: 237. After A1: 242. After
  the F1 review fixes: 243. After the A1 review fixes: 248. After A2: 254.
  After B1: 264. After B2: 269. After the A2 review fixes: 272. After the
  B1 review fixes: 276. After the B2 review fixes: 278. After B3a: 280. After B3b: 282. After the B3a review fixes: 285. After the B3b review fixes: 286. After F2: 295. After K1: 297. After K2: 298. After K3: 299 (its opt-in test runs only with an explicit `-Filter`). After K4: 301. After the F2 review fixes: 310. After the K1 review fixes: 311. After K5: 314. After the K4 review fixes: 315. After the K2 review fixes: 317. After the K3 review fixes: 318. After K6: 320. After S3a: 336. After S3b: 348. After the K5 and K6 review fixes: 354. After the fixes of the second review of F2 and K1 to K4: 356. After S1: 365. After S2a: 375. After the S3 review fixes: 386. After the fixes of the review of `bc827391` and `fe02c12a`: 389. After S2b: 395. After S2c: 401. After S4a: 412. After the fixes of the review of `de2fb8dc` and `8a322b32`: 418. After the fixes of the reviews of S1 and S2: 429.
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
| B3b Decompile path on the session, `appState` check rule | done | `f646dd52`, review fixes (the commit after `56b487d5`) | FIX: 1 should-fix, 6 nits, 1 question. Fixed: a test that the decompiler reads `sci.sh` from the data folder (the real-game tools now resolve enum names; see "Decisions"); `Src\Util` in the `appState` rule (13 GUI files in the allowlist); `DecompileScript` declared in `DecompileScript.h`; `GetIncludeFolder` is const; `%zu` and Warning for two log lines; corrected documents and CI comment. Outside the branch: the whole-game dump writes `.sco` files into the game folder that it dumps (a separate task was proposed). |
| F2 Engine errors as values | done | `84f58380`, review fixes `31c34e18`, `fe02c12a` and the commit after `de2fb8dc` | FIX: 2 should-fix (TryLoad rejected template script 990; `CheckResourceData` missed an unreadable header and a short read), nits and questions. Fixed, with the stream source name for a heap read in a copy. Review of the fixes: FIX (a valid empty package resource was marked "Corrupt"); fixed in the commit after `bc827391`, with a test of a short read of compressed data, the seek offset text and the text count (2,193). Review of that fix: FIX (a zeroed SCI1.1 header looked like a valid empty resource; a rebuild dropped an empty resource); fixed in the commit after `de2fb8dc`. |
| K1 `and`/`or` value semantics | done | `cdf8759d`, review fixes `76150fb2` | FIX: 1 should-fix (the README note that the plan promised), nits. Fixed: push-context and `or`-condition tests, dead `WeakSyntaxNode`, history and text. Review of the fixes: PASS; the unused `fMeaning` parameter went in the commit after `bc827391`. |
| K2 `.sco` exports from the public block | done | `c6f42341`, review fixes `4356cd87` | PASS with nits. Fixed: slot order, a template-wide `.sco` test, the real decompile effect in the docs. Review of the fixes: PASS; a test comment, and a C2 check of the public block (a question), in the commit after `bc827391`. |
| K3 Species order from compiled scripts | done | `746518e5`, review fixes `1fad4b57` | PASS with nits. Fixed: `GlobalClassTable` skips the alignment, one log line per mismatch, a no-heap test, the numbers. Review of the fixes: PASS; the vocab 996 preview and the rebuild command skip the alignment, the opt-in test fails with no input, the time text, in the commit after `bc827391`. |
| K4 `#` in selector names | done | `b7385d13`, review fixes `ebb6ff7d` | FIX: 1 should-fix (a `#` property read in a method still got `_`; KQ6 script 710). Fixed in `CleanTokenSCI`. Review of the fixes: PASS; the plan's GUI text, a README note and a wider known gap, in the commit after `bc827391`. |
| K5 `proc<N>_<M>` for a missing script | done | `78490d24`; test fix `2aee01b1`; review fixes (the commit after `108cb227`) | PASS with 6 nits; the test commit PASS. Fixed: tests for an `asm` `calle` and for the name as a value (an undeclared name again), a leading zero is not a procedure, `__proc0_<M>` compiles to `callb`, the README text, the handover and plan text. Review of the fixes (`bc827391`): PASS with nits (the byte count, the `asm` form, the plan rows), fixed in the commit after `de2fb8dc`. |
| K6 No `vocab.000`; `/` in paths | done | `dcc0fdf7`, review fixes (the commit after `108cb227`) | FIX: 1 should-fix (the handover State, fixed in `f85fe77d`), 7 nits, 1 question. Fixed: the Said test counts one error for two Said strings, a vocab 900 test, the null vocabulary in the "Add as synonym of" dialog, the `ScriptId` folder keeps only `\`, stale text. Known gaps: see "Decisions" (a synonym with no vocabulary, a name with a slash, a `ScriptId` with no folder). Review of the fixes (`bc827391`): PASS with nits, fixed in the commit after `de2fb8dc`. |
| S3 ScriptCatalog, script names without `game.ini` | done | S3a `f85fe77d`; S3b `108cb227`; review fixes (the commit after `fb14399f`) | FIX: S3a 1 should-fix (a `-` in a derived name), S3b 1 should-fix (one name conflict refused every script, with the wrong fix), nits and questions. Fixed: see "S3 review" in "Decisions". |
| S1 Compile destination | done | `a9561fcc`; review fixes (the commit after `7d26d9d6`) | FIX: 1 should-fix (a script that failed after its resources were queued was written without its tables), nits. Fixed with the S2 review fixes. |
| S2 CompileBatch | done | S2a `fb14399f`; S2b `f256c0d8`; S2c `2ca7e418`; review fixes (the commit after `7d26d9d6`) | S2a FIX (the same should-fix as S1), S2b FIX (Replace moved patch files of resources that the commit did not write), S2c FIX (the GUI reached it; the scan of src could throw). Fixed: see "S1 and S2 reviews". |
| S4 DecompileRun | done | S4a `152f4e56`; S4b: the commit after `152f4e56` | |
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
- 2026-09-24, CLI11 for C1: a download needs your permission, and you were
  away. A copy of CLI11 2.0.0 (2021, BSD-3-Clause, single header) was on
  this machine (`E:\Code\Cpp\asperite\third_party\IXWebSocket\third_party\
  cli11\CLI11.hpp`; its only change from upstream is an include guard, as
  its header says). C1 vendors that copy. A newer CLI11 (2.4 or later) is
  a later swap of one header, when you allow the download.
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
  also moves the Decompiler folder. B3b moved `DecompilerConfig.cpp` from
  the static `GameFolderHelper::GetIncludeFolder()` to the include folder
  of the resource map.
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
  the root of the drive). `ScriptId` splits a path at the last `\` or
  `/` (K6).
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
  now the module folder. The templates have no `src\Decompiler.ini`, and
  the config uses the defines only after it reads that file, so the
  in-repo suites do not change. The real-game tools (`Dump_ExistingGame`,
  `OptIn_BatchExistingGame`, `OptIn_Oracle_ExistingGame`) change for a
  game with `src\Decompiler.ini`: they now resolve enum names, as the GUI
  does (B3b review: 46 of 86 scripts of a template copy changed, for
  example `(Palette palFIND_COLOR ...)` in place of `(Palette 5 ...)`). The
  QfG4 golden dump folder has a `src\Decompiler.ini`, so its baseline must
  be taken again when this branch merges.
  `DecompilerConfig_ReadsTheHeadersFromTheDataFolder` checks the folder.
- Known gap for S4 and C2: a missing `sci.sh` or `keys.sh` is not reported;
  the enum names are then lost with no message (`GetDefinesScript`).
- B3b: `ConvertToSCISyntaxHelper` has no default for its lookups. A new
  overload takes a helper and loads the lookups from it (the three GUI
  callers use it). With null lookups, the formatter no longer loads them
  itself.
- B3b: `DecompileScript` and `FixDuplicateObjectNames` moved to
  `Src\Compile\DecompileScript.cpp`. The 3-argument `DecompileScript`
  (P15, dead code that passed a null config) is gone.
- B3b: the log calls in `DecompilerFallback`, `Disassembler`,
  `PaletteOperations`, `Sound` and `View` go to `CoreLog` (Warning for a
  failure, Info for a note). In the GUI's log file, the warnings get a
  `warning:` prefix. The "Empty loop found" call in `View.cpp` had no null
  check: with no `AppState`, it was a call through a null pointer
  (undefined behaviour; the B1 `LogInfo` does not touch `this`, so it did
  not crash). The "Empty loop found" and MIDI "tempo event" lines are
  warnings (B3b review: the view is repaired, and the import drops data),
  and they print their `size_t` values with `%zu`. The two `Sound.cpp`
  messages lost their trailing line break. The commented-out call in
  `DecompilerNew.cpp` now uses `CoreLog` too.
- B3b: the check rule `appstate-in-engine` covers `Src\Core`,
  `Src\Compile` and `Src\Resources`. The allowlist has the two audio-cache
  sites (`GameFolderHelper.cpp`, `ResourceMapOperations.cpp`: the
  `AudioCacheResourceSource` needs the `CResourceMap`) and the GUI flags of
  the pic code (`Pic.cpp` `_fDontCheckPic`, `PicDrawManager.cpp`
  `_fNoGdiPlus`, `PicOperations.cpp` the clipboard format). The script
  commands do not reach them. Since the B3b review, the rule also covers
  `Src\Util`, which holds engine code (the codecs, `Stream.cpp`,
  `ScriptText.cpp`, `util.cpp`) and GUI code; its 13 GUI files are in the
  allowlist, so a change to their `appState` count needs `-Update`. Phase
  E1 makes the build enforce the layers.
- B3b review: `DecompileScript` and `FixDuplicateObjectNames` are declared
  in `DecompileScript.h`. `CResourceMap::GetIncludeFolder` is const, and
  `CreateDecompilerConfig` takes a const resource map.
- For E1: some engine files still include `AppState.h` with no use
  (`Audio.cpp`, `AudioMap.cpp`, `Message.cpp`, `ResourceMap.cpp`,
  `Sync.cpp`, `Vocab000.cpp`). B3b removed it from the `Src\Compile` files.
- F2: the 46 `throw std::exception("...")` became `sci::DataError`:
  `Format` for bad data (the default; also a docs comment that does not
  match its function), `Unsupported` for a size limit, an audio map or
  wave format that SCI Companion does not know, or an audio operation
  that is not implemented, `Internal` for a misuse (an iterator past the
  end, a missing component, a seek outside a write stream, a script that
  loads once and not the second time), and `Io` for a short file read.
  `DataError` derives from `std::runtime_error`, so every old
  `catch (std::exception &)` still catches it, with the same text.
  `deletefile` throws `ThrowWin32` (`Io` or `NotFound`); its text changed
  from "Deleting X failed with error 5: ..." to "Deleting X: ...".
- F2: the `Result` forms (`TryCreateResourceFromResourceData`,
  `CompiledScript::TryLoad`, `GlobalCompiledScriptLookups::TryLoad`,
  `CompileTables::TryLoad`) are new functions; the old `bool` forms do
  not change, and the GUI still uses them. The services (S1 to S4) use
  the new forms. Each error puts the resource ("script 110") in
  `where.resource`; the context does not repeat it ("the script could not
  be read: Read past end of stream. (script 110) [format]").
- F2: `CompiledScript::TryLoad` reads in throw mode, so a read past the
  end is an error, not a zero. A probe over 31 GOG game folders (5,913
  scripts) found no script that `Load` reads and `TryLoad` rejects. The
  2,193 text resources of those games all end with a NUL, so the
  `TextReadFrom` change affects none of them. Willy Beamish shows no
  scripts at all (an older detection problem, not F2); `TryLoad` of its
  tables gives `NotFound` for vocab 996. (The first count, 5,944, had the
  31 scripts of the SCI0 template in it. Its text count, 2,191, was an
  addition error: the probe's lines for the 31 folders add up to 2,193,
  and the SCI0 template has no text resources; review of the F2 review
  fixes.)
- F2 review: the SCI1.1 template has one script that `TryLoad` rejected
  and `Load` read. In script 990, an object's name value is outside the
  heap, and the loader gives the object a made-up name. The name read is
  tolerant again (not in throw mode). A test checks that `TryLoad` and
  `Load` agree on every script of both templates.
- F2 review: `sci::istream::setSourceName` gives a stream the name of its
  resource. In throw mode, a read past the end then puts the name, and
  the offset of the read (or the target of a seek past the end), in the
  error location. Copies of the stream
  keep the name. `TryLoad` names the script and heap streams, so a
  damaged heap gives "heap N", also where the loader reads a copy of the
  heap stream.
- F2 review: `GlobalCompiledScriptLookups::TryLoad` and
  `CompileTables::TryLoad` load one table at a time. A failure names the
  table and its vocab resource ("the selector table is not valid", vocab
  997). A missing vocab.000 is not an error there: `GetVocab000` gives
  null, and a Said string reports it (K6).
- F2 review: `CheckResourceData` finds two more kinds of damage. The
  iterator marks a blob `Corrupted` when the map points to a header that
  is not in the volume (before: an empty blob with no flag).
  `ResourceBlob` marks it when the volume ends inside the data (before:
  bytes that were not read from the volume, with no flag). The error is
  `Format`, "the resource is damaged: its header or its data could not
  be read".
- Review of the F2 review fixes: the header reader threw one error for a
  header that is not in the volume and for a header with sizes 0, so the
  first fix also marked a valid empty resource (a text with no strings,
  saved to the package) `Corrupted`, and
  `TryCreateResourceFromResourceData` refused it. `ReadResourceHeader`
  now throws `EmptyResourceError` (a `DataError`) when both sizes are 0,
  and the iterator gives such a resource an empty blob with no flag, as
  before F2. A header with only one size of 0 stays damaged. A test also
  covers the short read of compressed data.
- Second review of the F2 fixes (`fe02c12a`, FIX): damage that zeroes an
  SCI1.1 or SCI2 package header (their stored size does not add 4) also
  gives sizes of 0, and the first `EmptyResourceError` took it for an
  empty resource. Now `EmptyResourceError` carries the header, and the
  package source (`_ReadHeader`) keeps an empty resource only when the
  header has the map entry's type and number; otherwise the header is
  damaged. A valid empty resource reads as a header with sizes 0, also in
  the size read of the rebuild and in the delete: before (older than F2),
  a rebuild dropped it with no message, and a delete failed with "the
  resource is empty" and left it in the package. The delete then compared
  the bytes and found no match, because an empty blob has no data; it now
  takes an empty resource as a match (`ResourceMapOperations.cpp`). Tests:
  a zeroed header, a header with one size of 0, and the rebuild and delete
  of an empty text. Known gap: `DeleteResource` (engine code) shows
  `AfxMessageBox("Resource not found.")` and a script dialog directly, so
  with no GUI it blocks (a test host stopped there); the CLI deletes no
  resource today. The test runs the package step of the rebuild (the
  source's `RebuildResources`). The whole GUI command (`RebuildResources`
  in `ResourceMap.cpp`) stopped with an access violation in a headless
  test on a copy of the SCI1.1 template, after or in its audio-cache step.
  The review of `8a322b32` found the likely cause by inspection:
  `ResourceMapOperations.cpp:85` uses the null `appState`, for any game
  with an audio volume. Not fixed (the CLI does not rebuild resources).
- F2 (GUI change): `TextReadFrom` no longer swallows a read failure. A
  text resource whose last string has no NUL now opens as a default
  resource marked "Resource load failed", instead of showing the strings
  read so far. The decompile worker shows an exception in the results
  pane (Error) instead of stopping with no message. The version probes
  log the reason at the Info level. The audio map probe fails on LB2
  (floppy) and Mother Goose because of an older detection problem (the
  blobs are made before the audio map number is known), and a warning at
  each open would be noise.
- F2 review (GUI change): a resource whose header or data is not in its
  volume shows "Corrupt" in the status column of the resource list.
  Before, it showed as an empty resource, or as bytes that were not read
  from the volume.
- F2 review: outside throw mode, a failed string read puts the stream
  back, so `TextReadFrom` read the same place again with no end (through
  `ResourceEntity::ReadFrom`). It now throws a `DataError`. This loop was
  also in the code before F2.
- Known gap: other empty `catch (...)` blocks stay in the allowlist
  (`AudioCacheResourceSource.cpp`, `CodeInspector.h`, `PhonemeDialog.cpp`,
  `LipSyncutil.cpp`, `TalkerToViewMap.cpp`, `Task.h`). Only the one in
  `CodeInspector.h` is on the script paths: the version detection walks
  script code with it when a game opens, and an exception there ends the
  walk with no message.
- Known gap (F2 review): in throw mode, only the word, byte and string
  reads, `seekg` and `skip` throw. The struct read (the `operator>>`
  template, which also reads an `int16_t` or a `uint32_t`) gives zeros,
  and `read_data` leaves its buffer as it was; both only set the fail
  state. A change affects every resource reader of the GUI, so F2 does
  not make it.
- Known gap: the SCI0 LZW decoder (`decompressLZW`) finds no errors. Bad
  LZW data gives wrong bytes and no `DecompressionFailed` flag.
- K1: the fix was already in the branch base (`f5f7a01b`, 2026-09-12:
  "Sierra semantics for a value-position and/or"). `f5f7a01b` guarded the
  new path with `LangSyntaxSCI`, and `b6b892d9` (2026-09-14) made the
  guard `if (true)`. The plan's check on `0dc1fef5` saw
  `_WriteFakeIfStatement` and missed the path above it. K1 removed the
  unreachable old code, and added byte-level tests: the value form
  against the Sierra shape in `asm`, and the condition form against its
  branch shape. With the old path put back, 8 unit tests fail:
  `Compiler_ValueAndOr`, `Family3_AndAsArgument`, `Family3_ValueIfReturn`,
  `Plain_CompoundConditions`, the three template tests
  (`TemplateGame_Recompiles`, `TemplateGame_BytecodeIdempotence`,
  `TemplateGame_BytecodeSnapshot`: the old path typed the value as bool,
  so `GameControls.sc` does not recompile) and the new
  `ValueAndOr_GiveTheDecidingOperand`. The older ones check a round trip
  or a snapshot; the new tests compare the bytes with Sierra's shape.
- K1 review: a push-context test (`(Abs (and a b))`: both exits join
  before the `push`) and an `or` condition with an else. The dead
  `WeakSyntaxNode` (only the old code made one) is gone, with
  `NodeTypeWeak`. The README "What's new" names the change of value. It
  came with `f5f7a01b`; the plan said that the note was there, and it
  was not.
- Known gap (K1 review): a nest of the other operator, such as
  `(or (and a b) c)`, gives Sierra's value but not Sierra's bytes. SCI
  Companion takes the inner `bnt` straight to the next operand of the
  `or`; Sierra's optimizer does not take a `bnt` through a `bt` (`a; bnt
  O1; b; O1: bt O; c; O:`). It is older than K1. The flat forms, `not`,
  call and send arguments, `return` and compare operands give Sierra's
  shape. Only a byte-exact round trip of such a nest sees the difference.
- Three other `if (true)` stubs from `b6b892d9` stay in `Compile.cpp`
  (the `if (true)` near lines 1386 and 1823, and `(simpleIndexer ||
  (true))` near line 2055). A separate task can remove them; they are
  on master too.
- K2: `SCOFromScriptAndCompiledScript` records the `(public name N ...)`
  slots as they are, a name that is in several slots included, and
  returns. In Sierra syntax, a procedure or instance is public only when
  its name is in the block (`SCISyntaxParser.cpp`, `IsExport`), so the
  block covers every export. Only a script with no block uses the old
  pairing in definition order. The compiler does not use this function
  (it builds its `.sco` from the compile); the decompiler and the planned
  `script sco` do. The case that `scicompile` found (KQ5 `Interface.sc`:
  public procedures defined out of slot order) comes from a source of
  another tool, so it matters for `script sco`. (Corrected at the K2
  review: the first text said that it changes a decompile.)
- K2 review: in a decompile, the decompiler writes the public procedures
  in slot order, so their slots do not change. What K2 changes there is
  the object exports: an instance exported out of definition order, and
  one instance in several slots. The review decompiled 12 GOG games with
  and without K2: 137 of 2,683 `.sco` files differ, and no procedure
  name differs (examples: KQ5 script 202 had `cedric@1` and now has
  `stdWalkIn@1`; SQ5 script 209 now lists `viewPortTalker` at slots 14,
  16 and 18). So the `.sco` files of a GUI decompile change.
- K2 review: the block's entries are now in slot order, as the compiler
  writes them. `GetExportIndex` gives the first entry of a name, so for
  `(public foo 5 foo 0)` the block order gave slot 5 and the compiler's
  `.sco` gives 0. `TemplateScripts_ScoExportsEqualTheCompilersSco`
  compiles every script of both templates and compares the built `.sco`
  exports with the compiler's `.sco` (fails before K2: SCI1.1 `Main`).
  No template public block is out of slot order or has a name in two
  slots, so that test does not pin the sort;
  `PublicBlock_NameInSeveralSlots_IsInSlotOrder` does.
- Review of the K2 fixes (question): the `.sco` builder accepts a public
  block that the compiler refuses (a slot listed twice, a name with no
  definition). `script sco` (C2) must report such a block (plan C2 row).
- Known gap (K2 review): the shipped SCI1.1 template is out of date. The
  compiled `Main` has 15 exports (`AddPolygonsToRoom@13`,
  `CreateNewPolygon@14`), but `Main.sc` lists 13, and the compiled
  `DebugHandler` exports `dInvD@1`, which its source does not. So "the
  block covers every export" is true only for a source and its own
  compile. `script sco` (C2) should warn when the block and the compiled
  export table disagree; before K2 such exports were also dropped with no
  message. Refreshing the template is a separate issue.
- K3: `SpeciesTable::Load` orders each script's species as the classes
  are in the script's compiled resource: first the species that the table
  gives the script, in the compiled order; then the table's other species
  for the script, in number order. A compiled class whose species the table
  gives to another script (or to none) is left out: to give its position
  that species would give a new class there the species of another
  script's class. So a script with such a leftover class keeps the old
  positional numbering (57 scripts in the 27 GOG game folders, the "- dev"
  copies left out; a known gap). SCI Companion's own compiles give each
  class its species in number order, so the templates and fan games do
  not change.
- K3: a lookup of each script (`MostRecentResource`) costs about 15 ms,
  so the first version made `SpeciesTable::Load` take 300 to 700 ms on the
  big games (the GUI loads it for each compile). The scripts and heaps are
  now found in one pass, and `CompiledScript::TryLoad` has a form that
  takes the blobs: 4 to 69 ms (the K3 review measured QfG3 69 ms and QfG1
  VGA 66 ms; the first text said 15 to 31 ms). The review of the K3 fixes
  measured 216 to 272 ms for QfG3 and 229 to 292 ms for QfG1 VGA (10 to 47
  ms with no alignment), while other builds ran on the machine: the time
  depends on the load of the machine.
- K3 review: `GlobalClassTable::Load` (inside
  `GlobalCompiledScriptLookups::Load`) needs only the script of each
  species, not its place in the script, but it ran the alignment too:
  that load was 1.3 to 1.9 times slower (QfG3 82 to 153 ms). It now calls
  `SpeciesTable::Load(helper, false)`. The compile tables keep the
  alignment. The vocab 996 preview and the "rebuild class table" command
  (and the reload after its purge) read only the script of each species,
  so they pass `false` too (review of the K3 fixes).
- K3: `OptIn_SpeciesOrder_RealGame` (`SCICOMP_SPECIES_GAME`, one folder or
  several separated by `;`) checks every script without a leftover class.
  It passes on the 27 GOG game folders (1.4 min). With the alignment off,
  it fails on 18 classes in 7 scripts of 5 games: Freddy Pharkas 0, QfG1
  VGA 0 and 15, QfG2 944 and 995, The Colonel's Bequest 999, and LB2 0.
  The first list had only LB2 0 and The Colonel's Bequest 999: the text
  of a failed assert is cut, so the test now logs one line for each
  mismatch and a line for each script (K3 review). With no
  `SCICOMP_SPECIES_GAME`, the test fails, as the opt-in rule (#79) says;
  before the review of the K3 fixes, it passed with no check.
- K3 review: a test for the `TryLoad` form that takes the blobs, with no
  heap blob for an SCI1.1 script: `NotFound`, "heap 0" (fails when
  `TryLoad` skips the heap, so that `Load` finds it by itself).
- Known gap (K3 review): the compiler still gives each class its species
  by position (`GenerateScriptResource.cpp`). A leftover class, or a
  user who moves the classes of a source, still moves species. Matching
  the classes by name would close both cases; it is not planned yet.
- K4: `SelectorP` is the base rule of every name in the parser, not only
  of selectors. So a `#` after the first character is now accepted in any
  name. `script#` stays a keyword, and a `#` at the start is still a
  selector literal (`#look`). A name written as `x#y` with no space is now
  one name; the formatter always writes a space between items.
- K4 review: the first K4 kept the `#` only in property declarations and
  send selectors (`CleanSelectorSCI`). A method that reads, sets or
  increments its own property writes the name as a token, which still
  got `_`: the decompiled KQ6 script 710 gave "Undeclared identifier
  'dungeon_'". Now `CleanTokenSCI` keeps a `#` after the first character
  in every name (a token, an assignment, `++`, the asm fallback, a class
  name), unless the name would be a keyword that ends in `#` (`class#`,
  `file#`, `script#`, `super#`, `text#`: then `_`, as before).
  `CleanSelectorSCI` is gone. The test compiles a class that uses
  `dungeon#` in a method, decompiles it and compiles the decompiled text.
- Known gaps (K4 review): a keyword followed by `#` is a name (`(if#)`
  parses as a procedure call, `else#` as a name), so such a typo gives a
  later, less clear error. The syntax colouring (`ScriptView.cpp`) shows
  `#dungeon#` as `#dungeon` and `#`. Older than K4 (review of the K4
  fixes): `KeywordP` (`ParserPrimitives.h`) ends a keyword at any
  character that is not a letter or a digit, so an instance named `if#x`
  or `if_x` does not recompile (`(if#x name: 0)` gives "Expected an
  expression"); the same for `return`, `while`, `switch`, `cond`,
  `repeat`, `break`, `argc` and `asm`.
- K5: `LookupProc` gives the new `ProcedureMissingScript` for a
  `proc<N>_<M>` that no kernel, local, main or `.sco` name resolves, when
  the game has no script N. The call is `calle N M`, with the warning
  "The game has no script N, so 'procN_M' compiles to calle N M. The call
  fails if the game runs it." (in a procedure call and in an `asm`
  `calle`). The game's script list is read once for each compile, and
  only for such a name. When the game has script N, the name stays an
  error: "Unknown procedure" in a call, and "Procedure type does not
  match call type." in an `asm` `calle`. K5 review: as a value (`(= t
  proc911_0)`) the name is an undeclared name, as before K5; and a number
  with a leading zero (`proc0911_0`) is not a procedure, because the
  decompiler writes none.
- K5: the decompiler writes `__proc<N>_<M>` for a call to an export that
  is not in the game (`_GetPossiblyMissingPublicProcedureName`), and
  `proc<N>_<M>` for an export with no name. The old parse of `__proc`
  looked for the `_` from the start of the whole name, so a decompiled
  `(__proc911_0 ...)` recompiled to `calle 0 11`. The parse of both forms
  now takes 1 to 5 digits on each side of the `_` (at most 65535); a name
  with other characters there is not a procedure. A `__proc` call gives
  no warning: the decompiler writes it on purpose. K5 review:
  `__proc0_<M>` (the decompiler's name for a `callb` to an export that
  main does not have) compiles to `callb`, as a call of a main procedure
  by its name does. Before, it gave `calle 0 M`, one byte longer (two
  when the operands are words), so the round trip was not byte-exact. An
  `asm` `calle __proc0_M, n` is now "Procedure type does not match call
  type."; the decompiler never writes it, and no GOG game has a `calle`
  to script 0 (review of the K5 and K6 fixes).
- Test lesson (K5): `Assert::IsTrue(CompileSource(..., error), W("..." +
  error).c_str())` shows an empty error when the compile fails. MSVC
  evaluates the arguments right to left, so the text is made before the
  compile runs. Call the compile first and assert on a `bool`. The sites
  that this branch added are fixed; 12 sites on master are a separate
  task.
- K6: `CompileTables` takes the main vocabulary from `GetVocab000`, which
  is null when the game has no vocabulary resource. `LookupWord` and
  `LookupWordGroupClass` read it with no check: the first Said word
  crashed the compile (access violation). They now give "not found", and
  `PreScanSaid` gives one error that names the resource: "The game has no
  vocabulary resource (vocab 0), so a Said string cannot be compiled."
  The number is the game's `MainVocabResource` (0, or 900 when the game
  has no vocab 0 at the open). A synonym in such a game gives "'x' is not
  in the vocabulary." for each word: no crash, but not one error. K6
  review: the test has two Said strings and counts one error, and an
  SCI1.1 test names vocab 900. The auto-complete word list of the script
  editor's "Add as synonym of" dialog (`WordEnumString.cpp`) read the
  null vocabulary too; it now gives no words.
- K6: `ScriptId` splits a path at the last `\` or `/`, so "src/rm110.sc"
  gives the folder "src" and the file "rm110.sc". Before, it gave no
  folder and the whole text as the file name. K6 review: the folder keeps
  only `\`, so `==` and `<` give one answer for both forms of a path.
- Known gaps (K6 review): a script name in `game.ini` with `\` or `/`
  is not supported. The GUI never writes one; for a hand-edited
  `n110=sub/rm110`, K6 changed the title to `rm110`, so its `.sco` file
  moves. `ScriptId("rm110.sc").GetFullPath()` gives `\rm110.sc`, the
  root of the drive (older than K6). The selectors of S3b give a path with
  a folder, so the CLI does not make such a `ScriptId`.
- S3 is two commits: S3a (the script-name map, the naming rule, the
  dialog) and S3b (`ListScripts`, the selectors, the shadow check).
- S3a: `ScriptNameMap` (`Src\Resources\ScriptNameMap.h/.cpp`) has plan
  section 3.4's rules. `Build` reads rules 1 to 3: `game.ini [Script]`
  (its buffer grows; the old readers gave no names for a section over
  20,000 characters), the `(script# X)` of each `src\*.sc` (X is a
  number, or a define of the file or of `src\*.sh`, as in the templates'
  `game.sh`), and the header of each `src\*.sco`. `AddDerivedNames` is
  rule 4, and `NameOf` gives `nNNN` (rule 5). `GameSession::Open` builds
  the map and installs it in the resource map's helper
  (`GameFolderHelper::ScriptNames`); every open of the resource map
  clears it first. The GUI installs none.
- S3a: the scan of a `.sc` file is a small scanner, not the parser. It
  skips `;` comments and `"..."` and `{...}` strings. A `(script# X)`
  whose define comes from an include outside `src\` is not read; the
  name then comes from the `.sco`, or is `nNNN`.
- S3a: a conflict is not an error of `Build`: two `.sc` files (or two
  `.sco` files) that would name one script, or one name for two scripts
  (their files would be one file). `Conflicts()` lists them, and the map
  leaves a script with two files out. The commands that write refuse the
  scripts in a conflict (S3 review), and `--all` leaves them out with a
  warning. When `game.ini` names a script, its other files do not matter
  (no conflict).
- S3a: `GameFolderHelper::GetScriptTitle(n)` is the map's name when the
  map is set, else the `game.ini` name, else `nNNN`.
  `GetScriptFileName(n)`, `GetScriptObjectFileName(n)`, the 2-argument
  `SaveSCOFile`, the decompiler's `(use Name)` lines and
  `CResourceMap::GetNumberToNameMap` (the compiler's number-to-name map)
  use it.
- Deviation from plan section 3.4: `FigureOutName(Script, n)` still
  reads `game.ini` only. It names the compiled blobs, and the resource
  map writes a blob's name into `game.ini` (`AssignName`, which creates
  the file) when it saves the blob. With map names, every command-line
  compile would create or extend `game.ini`. A test compiles a script in
  a copy with no `game.ini` and checks that the file stays missing (fails
  when `FigureOutName` takes the map's name).
- S3a: `SuggestScriptNames` is the decompiler's naming rule, as a pure
  function; the Decompile dialog's "Reset filenames" uses it. GUI change:
  the scripts go in number order, so the `_N` suffix of a duplicate name
  follows the number (before: the hash order of the class table). A
  suffixed name counts as used, and a name gets `_` for a character that
  a file or a `(use ...)` cannot have (before: raw object names, for
  example with a space).
- S3b: `ScriptCatalog` (`Src\Resources\ScriptCatalog.h/.cpp`).
  `ListScripts(session, alwaysDerive)` gives one row for each script
  (number, name, the rule of the name, the derived name, where the game
  has the compiled script, the `.sc` and `.sco` flags, and an error). It
  reads the compiled scripts only when a compiled script has no name from
  rules 1 to 3, or with `alwaysDerive`, as plan section 4.3 says; so an
  unreadable script shows its error only then. The place of a package
  script is "resource.NNN" (the package number, as the SCI0 to SCI1.1
  maps name their volumes); a patch script shows its file name.
- S3b: `DeriveScriptNames(session, all)` is rule 4 for the compiled
  scripts (the names of rules 1 to 3 count as used; `all` names every
  script, as a reset does). `AddDerivedScriptNames(session)` gives the
  derived names to the session's map, for the decompile of S4.
- S3b: `ResolveScriptSelectors(session, selectors, mode)` and
  `SelectAllScripts(session, mode)` give a `ScriptSelection`: the
  `ScriptId` of each script (the path of `src\<name>.sc`, with the number
  set) and warnings. The modes are List, Decompile, Compile and Sco. A
  path selector (Compile only) is taken first from the game folder, then
  from the current folder, and must be a `.sc` file in `<game>\src`. The
  modes that write refuse a selected script in a conflict (`Usage`, with
  the conflict; S3 review). Every bad selector is in one `Usage` error.
- S3b: `FindShadowingPatches(helper, resources)` scans the game folder
  with the rules of the patch file source: the type's name patterns, a
  number from the name, and the type in the first byte. It finds a
  standard name (`110.scr`) and another one (`0110.scr`).
- S3 review (S3a FIX, S3b FIX, one should-fix each): a derived name had
  `-`, which `(use ...)` cannot take (`FilenameP`; LSL6 script 1823
  `Voice-Over_Announcer`): it is `_` now. A Windows device name (`CON`,
  `NUL`, `COM1`, `LPT1`...) gets a `_` after it: Windows 10 and older open
  the device for `CON.sc` (Windows 11 makes an ordinary file; the `_` does
  no harm). Names compare as Windows file names do, also outside ASCII
  (`LCMapStringW` with `LOCALE_INVARIANT`; `Über` and `über` are one
  file). `game.ini` gives a name only with the key that the GUI reads
  (`n007`, not `n7` or `n0007`), without single or double quotes.
- S3 review: a conflict is a `NameConflict` with its scripts and a fix
  for its kind (two `.sc` files: keep one; two `.sco` files: delete the
  old ones; one name for two scripts: rename one in `game.ini [Script]` or
  rename its file). A mode that writes refuses only a selected script in a
  conflict, and gives the conflict first; `--all` leaves the script out
  with a warning. Before, one conflict refused every script, with one fix
  for all kinds. Three real projects have two `game.ini` names that differ
  only in case (Codename ICEMAN `subMarine`/`Submarine`, Camelot
  `thief`/`Thief`, a QfG1 `- dev` copy).
- S3 review: a selector that is a script name is a name, also with a `.`
  (`n993=gamefile.sh` in 9 real folders). Two paths for one script number
  are an error (before: the second replaced the first). `200-100` and
  `65536` say why. A path is normal (`lexically_normal`, `\` only).
  `FindShadowingPatches` reads the folder as the patch-file reader does:
  the name patterns of all the types, 2 bytes or more, the type from the
  first byte (`105.hep` with a script type byte is script 105).
  `DeriveScriptNames` returns a `Result`, and `ReadDeclaredScriptNumber`
  catches its exceptions (plan 6.2).
- S3 review questions: a path selector takes the number that the name map
  gives the file's name, and the compile writes the number that the file
  declares, with a warning, as the GUI does (kept). With `alwaysDerive`,
  the Name column is the name that `list` and `decompile` use now, and
  the Derived column is what a reset gives, so they can differ (`Door_10`
  and `Door`): intended. `AddDerivedScriptNames` replaces the map with no
  lock: S4 must call it before a worker thread starts.
- Review of `de2fb8dc` (FIX, three should-fix) and of `8a322b32` (FIX, two
  should-fix), fixed in the commit after `d588e499`:
  - A conflict keeps its file titles (`NameConflict::names`). `--all`,
    a range and `list` see a script that is only in a conflict (before, a
    conflict of a script that the game has not compiled was not seen), and
    a name selector finds it by the title of one of its files
    (`ConflictNumberOf`).
  - A derived name takes no file title of `src` (`FileTitles`), so a
    decompile does not write over the file of a script in a conflict, or
    of any other file.
  - A file name in `src` with a character that the ANSI code page does not
    have is skipped (`SkippedFiles`, shown with `?`); before, it stopped
    the open of the game.
  - A number, a range or a name and a path for one script are an error
    ("script N is also"); one file in two spellings is one script.
  - The package header: an empty resource numbered 32768 or more matches
    its map entry (the header's number is signed), and an empty header
    with no type mark (0x80 in SCI1 to SCI2) is damage: a zeroed header of
    view 0 matched its map entry.
  - Answers: a range with a conflicted script fails the command, as any
    selector does, while `--all` leaves the script out with a warning
    (intended: a range is an explicit selection). A script that uses a
    script in a name conflict compiles with the `.sco` that both names
    share, with no warning (not changed).
  - Known gaps: an SCI2.1 header has no type mark, so its zeroed header of
    view 0 still reads as a valid empty resource. Damage that sets only the
    two sizes to 0 reads as a valid empty resource; the header cannot show
    it. `SkippedFiles` is not shown yet (C1 prints it).
- S1: `CompileWriteOptions` (`Src\Compile\CompileWrite.h/.cpp`): `saveTo`
  (Default, Package or Patch; Default reads `game.ini`, as the GUI does),
  `outDir`, `raw`, `writeResources`, `writeObjectFile` and
  `writeDebugInfo`. `WriteCompiledResource` writes one compiled resource
  (script, heap, text, vocab 996 or vocab 997) to the destination: into the
  game through the resource map (in a batch, the write only queues), or
  into `outDir` as a patch file (`ResourceBlob::SaveToFile`), or with
  `raw` as the plain data (`script.110.bin`). `outDir` with `Package` is
  `Usage`. `GameFolderHelper::GetSaveSourceFlags(location)` resolves
  Default.
- S1: `NewCompileScript(..., options)` (the GUI passes the defaults) and
  `CompileTables::Save(resourceMap, options)` use it. The compile makes the
  text resource's data itself (before: `AppendResource` of the entity; a
  text resource has no checks). A write that fails is an error in the
  compile log ("Could not write the output of rm110.sc: ..."), and the
  compile of that script fails (a GUI change: before, a message box for a
  resource, nothing for the `.sco` and `.scd`, and a success). The GUI's
  `CompileTables::Save(resourceMap)` shows a failure in a message box, as
  before (one message, not one for each table).
- S1: `Src\Core\FileWrite.h/.cpp`: `WriteBytesToFile` and `WriteTextToFile`
  (CR LF for each LF, as the old text-mode writes) give `NotFound` or `Io`
  with the path. `SaveSCOFile` returns a `Status`, and every caller reports
  it (the compile log, the decompile results, the status line of the
  Decompile dialog, the tests). The `.scd` and the `.sc` of the batch
  decompile use them (before: `ofstream` with no check, and `MakeTextFile`,
  which gave the path also when the write failed). `MakeTextFile` stays for
  the GUI's temporary text files.
- S1: an `outDir` write goes straight to the folder, not into a batch; S2
  sets the batch rules. The `.sco` always goes to `src\` (plan section 5).
  `VocabClassTable` and `VocabSelectorNames` are `extern` in `sci.h`, as
  `VocabKernelNames`; `SpeciesTable` and `SelectorTable` have `IsDirty`
  and `MakeResourceData`.
- S2 is three commits: S2a (the batch engine: `CompileBatch`,
  `CompileScripts`, `CompileReport`), S2b (the passes, the shadow check,
  the warnings of a patch-file write), and S2c (the raw message and the
  1-based lines of a diagnostic, the GUI on the batch, and its shadow
  question).
- S2a: `Src\Compile\CompileBatch.h/.cpp`. `CompileOptions` (the S1 write
  options and `failFast`), `ScriptOutcome` (number, name, `Status`,
  diagnostics), `CompileReport` (the scripts, `tables`, `commit`,
  `cancelled`, `stopped`, the counts, `Succeeded`) and `ICompileEvents`
  (`OnScriptStart` runs inside the exception boundary of the script, so a
  test injects a fault there; `OnScriptDone`). `CompileBatch::Start` loads
  the tables (`TryLoad`) and the headers, refuses an output folder with
  Package (`Usage`) and Package in a patch-mode game (`WriteRefused`), and
  opens one `DeferResourceAppend`. `Step` compiles one script with its own
  `CompileLog`, inside `Guard`; an exception gives its error as the status
  (`Internal` when it is not a `DataError`) and a diagnostic with its
  text. `Finish` saves the tables only when a script compiled (plan section
  4.5), then commits once, also after an abort (plan section 7). A batch
  that is not finished withdraws its queued writes. `CompileScripts` runs
  the three.
- S2a: `CompileScriptFile` is `NewCompileScript` with a `Status`:
  `Compile`, the read error of the source file (before: a silent `false`;
  now also an error in the log, a GUI change), or the first write error.
  `NewCompileScript` gives `CompileScriptFile(...).has_value()`.
- S2a: `CompileLog::CalculateErrors` counts the results that the log holds.
  Before, each call added them to the counts again, so
  `ScriptDocument::OnCompile` counted every error twice after a failed
  commit (P11). The compile dialog clears its log before each script, so it
  keeps `_anyErrors` for `HasErrors` (its result for the caller, for
  example the run after a compile-all).
- S2b: the passes of plan section 4.5. `CompileOptions::passes` (default 1)
  is the largest number; a pass that changes no `.sco` file is the last.
  Each pass runs in its own nested `DeferResourceAppend` level, and a pass
  that another pass follows is withdrawn, so the commit holds the last
  pass. The report has the last pass, and `passes` counts them;
  `ICompileEvents::OnPassStart`. `SaveSCOFile` writes a `.sco` file only
  when its bytes change (`CompileResults::ObjectFileChanged`). Nothing reads
  a `.sco` file time (the class browser times only headers), so the GUI
  does not change.
- S2b: the shadow check of plan section 5. `ShadowPolicy`: Refuse (the
  default), Replace, Ignore. For a package write (the resolved destination,
  no output folder, not a dry run), `Start` checks script N, heap N
  (SCI1.1) and vocab 996 and 997 (`FindShadowingPatches`): Refuse gives
  `WriteRefused` with the files. `Finish` checks the queued package writes
  again before the commit, because a script's auto text is known only then;
  with Refuse, a new file gives a `WriteRefused` commit status, and nothing
  is written. With Replace, the files move after a successful commit to
  `<game>\replaced-patches\<yyyymmdd-hhmmss>` (`movedPatches`; a file that
  does not move is a warning).
- S2b: the warnings of a patch-file write: a patch file with another name
  for a compiled script or heap (`script.0904`: SCI Companion can load
  either file), and the tables as patch files in a package-mode game (they
  hide the GUI's later package saves of 996 and 997).
- S2c: `CompileResult::GetRawMessage` is the message with no "Error: (file)"
  and no position around it (the command line prints its own form); it is
  the message itself when nothing is around it. `_ReportThing` and the
  syntax error of `SCISyntaxParser::Parse` set it. Every line is 1-based:
  eight parser sites added 1 (P13), and the text of a syntax error had the
  0-based line too. `ScriptOutcome::stats` has the sizes of the compiled
  script (the GUI's "Object data..." line).
- S2c: `CompileOptions::askShadows` (a callback). With `Refuse`, the batch
  asks it about the patch files that would hide a package write, at the
  start and in `Finish` for the files that it finds only then; its answer
  is the policy from then on, and `Refuse` gives `Cancelled` (nothing is
  written). A command line can use it for an interactive question too.
- S2c: the GUI on the batch. `CompileABunchOfScripts` (compile-all and the
  compile before a run) gets the scripts (`ScriptsToCompile`, the old
  dialog code), starts the batch, shows `CNewCompileDialog` (now only the
  progress: one `Step` for each posted message, inside the class browser
  lock, with an atomic abort flag for Cancel), and then calls `Finish`
  outside the dialog, so the question before the commit is not inside a
  window's destruction. `ScriptDocument::OnCompile` runs a batch of one
  script. `AskAboutShadowingPatches` is the question (Yes Replace, No
  Ignore, Cancel Refuse), and `ReportCompileBatch` writes the lines of the
  table save, the commit, the moved files and the warnings. The result for
  the run after a compile: every script compiled, and the tables and the
  commit are Ok; Cancel is not an error (as before).
- S2c known gaps: no test runs the dialog or the question; the output
  lines and the scripts of a compile-all have a test (`CompileBatchGui`).
  Only one of the eight parser sites (the `else` clause of a `cond`) has a
  test.
- S1 and S2 reviews (fixed in the commit after `7d26d9d6`):
  - Each script is a savepoint (a nested `DeferResourceAppend`): a script
    that fails withdraws the resources that it queued, so the commit never
    writes a compiled script without its tables. `CompileScriptFile`
    writes no `.sco` or `.scd` after a failed resource write. `Finish`
    refuses the commit when the tables could not be saved.
  - Replace moves only the patch files that hide a queued package write,
    found before the commit (`_hidingPatches`); the list of the start is
    only for the question. A new `replaced-patches` folder for each batch
    (`-2`, `-3`... in the same second). A file that cannot move is
    `report.moves`, and `Succeeded` needs it.
  - An abort between two passes keeps the pass that finished.
    `report.passLimit`: the last allowed pass still changed a `.sco`.
  - `Start` gives `Usage` for raw files with no output folder and for the
    game folder as the output folder, and `NotFound` for an output folder
    that does not exist. A script with no number gets the number that its
    source declares (the shadow check and the report need it).
    `WriteCompiledResource` checks the size also for a dry run.
  - `WriteBytesToFile` shares the file for read and write, as the
    `ofstream` that it replaced did. It is still not atomic (a temp file
    and a rename would fail when another program has the file open with
    no delete sharing; not a regression).
  - The raw text of a syntax error with a hint is a sentence of its own.
  - GUI: the helpers are in `Src\Dialogs\CompileBatchGui.h/.cpp`
    (`ScriptsToCompile` skips a name outside the ANSI code page;
    `AskAboutShadowingPatches` asks nothing while a quit is pending, and
    the batch writes, as before; `StartFailureLine`; `ReportCompileBatch`,
    where Cancel is a message). The dependency tracker clears a script only
    after a commit that is Ok. `Finish` times the commit (the GUI's
    timers).
  - Answers: `failFast` stops at the first failure in any pass, also one
    that a later pass would fix (documented in `CompileOptions`). Columns
    are 0-based: C3 adds 1 in its MSBuild-style output. The eight parser
    messages are `CRT_Message` with a line: C3 prints them too. The report
    has no `written` list yet: C3 adds it for `--dry-run` (plan 6.5).
  - For C3: after a headless decompile of all SCI0 template scripts, the
    review saw `SysWindow` and `Obj` not recompile (1 or 5 passes).
- S2b known gaps: no test has a script that compiles in one pass and fails
  in a later one, so the withdrawal of an earlier pass is checked by
  inspection only. With an output folder, each pass writes its files at
  once, so a script that fails in the last pass keeps the file of an
  earlier pass.
- S4 is two commits: S4a (the run, `DecompileRun`, and
  `GenerateObjectFiles` for `script sco`) and S4b (the Decompile dialog on
  the run, and the removal of the library's `theApp`, P16).
- S4a: `Src\Compile\DecompileRun.h/.cpp`. `RunDecompile(session, scripts,
  options, results, output)` prepares the src folder, gives the names
  (`NameAssignment`: Missing adds the derived names, All is
  `--reset-names`, None for the GUI, which names with `game.ini`), loads
  the lookups and the config, runs the batch, then the stale loop (the
  scripts that the run did not decompile and whose `.sc` uses a renamed
  global by its old name: in `stale`, or with `updateStale` decompiled in a
  new batch, until none is stale), and writes the names into `game.ini`
  (`GameIniNames`: Update writes only into a `game.ini` that exists, Create
  also creates it, None never writes). `DecompileReport`: an outcome with a
  `Status` for each script (Cancelled when the run stopped before it), the
  renames, the stale scripts, the statistics (a counting wrapper of the
  results), `cancelled`, warnings, and the `game.ini` status.
  `PrepareDecompileFolder` is a plain copy that never overwrites a file.
  `WriteScriptNamesToGameIni` skips a default name (`nNNN`) and a name that
  `game.ini` has.
- S4a: `DecompileBatch` changes. An output (`IDecompileOutput`, for
  `--stdout`) gets the source and no file is written (not the `.sc`, the
  `.sco` or main's `.sco`); the run gives the output the last source of
  each script that decompiled, in number order. `GetFailedScripts` has the
  error of each failed script: a script that does not load (`TryLoad`;
  before, it was dropped with no message), an exception (each pass is
  inside an exception boundary with no context, because the message names
  the script), or the first `.sc` or `.sco` write error (before, a message
  only, and the script counted as written). Pass 2 replaces the write
  status of pass 1.
- S4a: `ResetScriptNames` (`ScriptCatalog`) gives every compiled script
  its derived name (`ScriptNameMap::ReplaceNames`) and returns the files
  that keep an old name (the run's warnings).
- S4a: `GenerateObjectFiles(session, scripts)` (plan section 4.6) parses
  each source file (the path of the `ScriptId`), loads the compiled script
  (its number), and writes the `.sco` next to the source with
  `SCOFromScriptAndCompiledScript`; the `.sco` gets the number of the
  compiled script (a warning when the source declares another literal
  number). No source file, or no compiled script: skipped, with the
  reason. A syntax error: `Compile`, with the diagnostics, and no `.sco`.
  A test deletes every `.sco` of the SCI0 template, makes them from the
  sources, and compiles every script with no error.
- S4b: the Decompile dialog runs `RunDecompile` on its worker thread, with
  `NameAssignment::None` and `GameIniNames::None` (the dialog names the
  scripts in `game.ini` itself, and asks about the stale scripts, as
  before), and prints the statistics of the report. It prepares the src
  folder with `PrepareDecompileFolder` when it opens. It no longer keeps
  the lookups and the config: the run loads them for each decompile, with
  `TryLoad` (a table that cannot be read stops the decompile; before, the
  dialog ignored the result of `Load`). `DecompilerDialogResults` no longer
  counts the statistics.
- S4b: the library's leftover DLL-template `CWinApp` (`theApp`, P16) is
  gone: `SCICompanionLib.cpp` and `SCICompanionLib.h` are deleted. Nothing
  referred to them, so the linker never took that object; the command line
  can now link the library with no second `CWinApp`.
- S4b known gaps: no test runs the dialog code; it was checked by
  inspection and by the build.
- S4a known gaps: with an output, the config reads the game's
  `src\Decompiler.ini` only (no copy is made), so a game with no `src`
  folder decompiles with the default config. The run loads the lookups for
  each run (the dialog kept them). The stale check reads every `.sc` of the
  game that the run did not decompile, once for each group.

## Next action

Phases K and S are done, with the fixes of their reviews, except S4, whose
review is running (an isolated worktree). Next: the fixes of the S4
review, then C1 (plan section 9), with CLI11 2.0.0 from a local copy (see
"Decisions").
