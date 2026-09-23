# `scic` CLI work: handover

Read this first in a new session. The design is in `docs/scic-cli/plan.md`.
Update this file in the same commit as each step.

## State

- Branch: `feat/scic-cli`, based on `master` at `0dc1fef5`. Not pushed.
- Current step: B3 (next). F1, A1, A2, B1 and B2 are committed. F1, A1,
  A2 and B1 are reviewed, with their review fixes. The B2 review runs in
  the background.
- 2026-09-23: at your request, the branch history was rewritten so that
  no commit adds a copyright header. Every SHA from F1 on changed; the
  SHAs in this file and in the commit messages were changed to match.
- Baseline on `0dc1fef5`: the Release build passes; the unit suite passes
  217 of 217 tests in about 4 minutes. After F1: 237. After A1: 242. After
  the F1 review fixes: 243. After the A1 review fixes: 248. After A2: 254.
  After B1: 264. After B2: 269. After the A2 review fixes: 272. After the
  B1 review fixes: 276.
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
| B2 Script text loader | done | `3be03ff1` | running |
| B3 Engine on the session | not started | | |
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
   break it), confirm it fails, then restore.
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
  `SessionOptions` has only `dataFolder`; B3 adds `warnOnUnusedInstances`
  when the compiler reads it.
- B1: `CResourceMap::SetDataFolder` replaces `SetIncludeFolderForTest` and
  also moves the Decompiler folder. `DecompilerConfig.cpp` still reads
  the static `GameFolderHelper::GetIncludeFolder()` (B3).
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
  with no `AppState`, because the B1 `LogInfo` does not touch `this`. B3
  must change the engine's `appState->LogInfo` calls to `CoreLogFormat`
  and add a check-script rule against `appState` in the engine folders.
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
- Known gap for S2 and C3: `WriteResource(entity)` gives `Cancelled` when
  `PerformChecks` says no. With no GUI, a check's yes/no question gets
  "no", and some checks are not a choice (duplicate message tuples, an
  audio map entry over 16 MB), so the CLI must not map that to exit
  code 7 without care. The Pic checks open a real dialog even with no GUI
  (`CDontShowAgainDialog::DoModal`); the script commands do not save pics.
- Known gap: a patch rename can still fail after the checks (another
  program locks the file at that moment). The renames before it stay
  done; the `.bak` files that are left are removed.

## Next action

Read the A2, B1 and B2 reviews and fix any real finding. Then B3 (plan
section 9).
