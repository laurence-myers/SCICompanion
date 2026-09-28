# `scic` CLI work: open items

Branch `feat/scic-cli`, from `master` at `0dc1fef5`; not pushed. This file
lists only the open items: the next work, the review findings that are not
fixed yet, the known gaps, the questions for you, and the work outside the
branch. The commit messages record each step, each review and its fixes;
the design is in `plan.md`. A commit that closes an item removes it here.

## Next

- The other review findings below, one milestone at a time: the nits of
  the tests and of the build (the de-duplication batch, `e95c6e72` and
  the others, `630e27b6` and `e3a4cf9a`). Then the optional E1 (plan
  section 9), or stop before it.
- Start a new session for each milestone (AGENTS.md, "Work with AI
  agents").
- Push and pull requests only with your approval.

## Review findings that are not fixed

### `0577fab0` (the nits of the decompile batch): 1 nit

- Test gaps: no test covers three parts of the decompile run. A
  negative check that broke all three together passed TestCli and
  TestDecompileRun.
  - A dry run gives the stale check only the source of a `.sc` that
    passed its write check. A dry-run form of
    `StaleCheck_AFailedScriptOfTheGroup` would cover it.
  - Pass 2 records a `.sco` that it changed (`_changedObjectFiles`).
    Without it, `decompile 0 994 -v` on the SCI0 template, after
    `decompile 0` with no `.sco` files, prints no line for `Main.sco`,
    which changed.
  - A `.sco` that an earlier group changed stays changed
    (`objectFileChanged` across groups).

### `5cf8c32e` (the nits of the compile batch): 3 nits

- Ctrl+C of scic (another thread) that comes after the second abort check
  of a new pass, while `Step` withdraws the finished pass, is seen at the
  check before the first script of the new pass: the finished pass is
  lost, the commit writes nothing, and `-v` has printed "Pass 2" while
  `report.passes` is 1. The window is as small as the one that was fixed.
  A fix: no abort check in the `Step` that has just started a pass.
- A write into an output folder that fails part of the way keeps its
  files, and its error names them, but the summary says "wrote none" and
  no "wrote" line lists them.
- A dry run with `--to package` does not check the package files: a
  read-only `resource.001` fails the run (exit 9), and the dry run gives
  0.

### `847e59cc` (the answers to the questions, `--log`): 2 nits

- A good map whose volume file is missing (a CD install with the volumes
  on the CD) gives "resource.map is damaged: no volume file holds any of
  its first N entries": the text blames the map. A map with only a
  terminator says "its first 1 entries".
- In the GUI output pane, the warning "The else clause must be the last
  clause in a cond; ..." has only its raw text (no "Warning:", file or
  line), as the "not implemented" warnings of `SCISyntaxParser.cpp`; the
  warnings of `CompileContext::_ReportThing` have that form.

### The de-duplication batch (`4a5cdfaa` to `54d92c25`): 3 nits

- `AbsolutePath` removes the separator of a UNC root (`\\server\share\`,
  `\\?\UNC\server\share\`); no test covers a UNC root.
- The licence copy excludes `License.txt`, `COPYING` and `COPYING2` from
  the mirror of `Files\Licenses`: a notice of that name there would not
  be copied.
- Six tests of TestDecompileRun leave a read-only file for `GameCopy` to
  delete (`RemoveFolder` clears the attribute).

### `e95c6e72`, `e1b791e9` and `7dd20f62`: 1 nit

- Test gaps: Ctrl+C just before the read of `list`; a `.sco` name that an
  earlier index took (`RenameContext`).

### `630e27b6` and `e3a4cf9a` (vcpkg, the sweep script): 4 nits

- A write of several rows that fails part of the way (a full disk) puts
  those rows into `sweep.csv` again at the next write.
- A running Visual Studio does not see a change of `vcpkg.json` until the
  solution reloads (the design-time build skips `InitializeBuildStatus`).
- With no `vcpkg.json`, the `ProjectStateLine` read fails the load of the
  projects (MSB4184); add an `Exists` condition.
- "A vcpkg clone in `VCPKG_ROOT` avoids the fetch" (AGENTS.md) is true
  only for a full clone that has the baseline commit.

## Known gaps

### Command line

- Names go to the console as bytes of the ANSI code page, with no
  conversion.
- A compile prints its diagnostics at the end, not as they come. A script
  that failed only in an earlier pass has no error line.
- One error in `game.sh` prints once for each script of `--all` (the
  header cache keeps only the headers that parsed). An error in an include
  that is not a header names the including script and the line of the
  include.
- The debug files (`.scd`) do not go back after a refused commit.
- The decompile summary names the failed scripts by number (their errors
  printed when they happened). A decompile warning has no script in its
  text (`-v` shows the progress line).
- The corpus sweep of 2026-09-24 (93 folders): script 755 of KQ5 (EGA and
  floppy) fails with "invalid map<K, T> key" `[internal]` (a task of
  another session works on it); script 995 of Hoyle 3 cannot be read;
  the decompiler reports errors in scripts that it wrote for Longbow, GK
  and the KQ4 `patch\NEW` folder (exit 6); 86 of 93 decompiled games do
  not compile back with 0 errors (limits of the decompiler); Willy
  Beamish is not supported (its map does not open: exit 3, "its lookup
  table has no end").

### Compile and decompile

- The decompiler's config does not report a missing or broken `sci.sh`
  or `keys.sh`: the enum names are lost with no message
  (`GetDefinesScript`).
- The editor's line rule stays: a bare LF in a CR LF file does not start a
  line, so the line of a diagnostic can differ from other editors. A
  CR-only file is one line; one that starts with `;` compiles to an empty
  script with no error. No warning tells about such a file.
- A `(script# X)` whose define comes from an include outside `src\` is not
  read; the name then comes from the `.sco`, or is `nNNN`.
- A script that uses a script in a name conflict compiles with the `.sco`
  that the two names share, with no warning.
- An old `.sco` that lacks an export: with no slot 1 in the `Obj.sco` of
  the SCI0 template, the procedure is `proc999_1` but its calls stay
  `localproc_0022` ("Unknown procedure"); with no slot 2, two procedures
  get the name `proc999_2` (older than the branch).
- A script name in `game.ini` with `\` or `/` is not supported.
  `ScriptId("rm110.sc").GetFullPath()` gives `\rm110.sc`.
- The class-table changes of a script that fails stay in the tables of the
  batch (a species with no class; nothing uses it).
- The `.sco` files that `script sco` makes have other string property
  values than the compiler's, so the first compile after `script sco`
  writes them again.
- The compiler gives each class its species by position
  (`GenerateScriptResource.cpp`): a leftover class, or classes that moved
  in a source, move species. `SpeciesTable::Load` keeps the positional
  order for a script with a leftover class (57 scripts in 27 GOG folders).
- `(or (and a b) c)` gives Sierra's value, but not Sierra's bytes.
- A keyword followed by `#` is a name (`(if#)` parses as a call). The
  syntax colouring shows `#dungeon#` as `#dungeon` and `#`. An instance
  named `if#x` or `if_x` does not compile again (`KeywordP` ends a keyword
  at any character that is not a letter or a digit).
- A synonym in a game with no vocabulary gives one error for each word.
- The shipped SCI1.1 template is out of date: the compiled `Main` has 15
  exports and `Main.sc` lists 13; `DebugHandler` exports `dInvD@1`, which
  its source does not have (`script sco` warns).
- The decompile run loads the lookups for each run, and the stale check
  reads every `.sc` of the game, except those of the group, once for each
  group (time only).

### Build and CI

- CI has no vcpkg cache: each job downloads the tools and the sources of
  vcpkg (about 175 MB) and builds the two ports.
- The CI fetch step of the vcpkg registry is checked by inspection only:
  vcpkg reads the baseline and the ports from the git objects that the
  fetch gives. A runner image that is older than the baseline was not
  tried.

### Engine and resources

- The package writer leaves `resource.map.bak` and `resource.00N.bak` when
  it cannot replace a volume or the map (the game stays consistent).
- A patch rename can fail after the checks (another program locks the file
  at that moment); the renames before it stay done.
- `WriteBytesToFile` is not atomic.
- `AudioCacheResourceSource::RemoveEntries` swallows its errors, and saves
  its audio map through the GUI `AppendResource`.
- `WriteResource(entity)` gives `Cancelled` when `PerformChecks` says no.
  With no GUI, a yes/no check gets "no", and some checks are not a choice
  (duplicate message tuples, an audio map entry over 16 MB). The pic
  checks open a real dialog (`CDontShowAgainDialog::DoModal`). The script
  commands do not reach them.
- `DeleteResource` shows `AfxMessageBox` and a script dialog directly, so
  it blocks with no GUI. The GUI command `RebuildResources` crashed in a
  headless test: `ResourceMapOperations.cpp` uses the null `appState` for
  a game with an audio volume. The CLI uses neither.
- In throw mode, a struct read (`operator>>`, also for `int16_t` and
  `uint32_t`) gives zeros, and `read_data` keeps its buffer; both only set
  the fail state.
- The SCI0 LZW decoder finds no errors in bad data.
- An SCI2.1 package header has no type mark, so a zeroed header of view 0
  reads as a valid empty resource. Damage that sets only the two sizes to
  0 reads as an empty resource.
- Empty `catch (...)` blocks stay in the allowlist:
  `AudioCacheResourceSource.cpp`, `CodeInspector.h` (on the script paths:
  an exception ends the walk of the version detection with no message),
  `PhonemeDialog.cpp`, `LipSyncutil.cpp`, `TalkerToViewMap.cpp`, `Task.h`.
- For E1: `Audio.cpp`, `AudioMap.cpp`, `Message.cpp`, `ResourceMap.cpp`,
  `Sync.cpp` and `Vocab000.cpp` include `AppState.h` and do not use it.
- The Decompile dialog's "Reset filenames" protects no file (older than
  the branch). The dialog names the scripts through `game.ini`, so it
  needs a GUI helper with no name map.

### No test (checked by inspection only)

- The Ctrl+C handler itself (the tests set the cancel flag), the core-log
  error line, the pure-call handler, a second `abort()` on another thread
  (an abort of two threads at the same moment can still exit with 3), the
  lock of `CliOutput`.
- The compile dialog, the shadow question, the Decompile dialog,
  `MainFrm.cpp` and `ScriptDocument.cpp`; 7 of the 8 parser sites that
  give a 1-based line.
- The exception boundaries around `batch.Run` and the stale check, the
  guard of 100 groups, the `NoDbugStr` path.
- The tables-first order of an output-folder write, and the change count
  of a compile that throws.
- A write into an output folder that fails after its check (a full disk):
  the `.sco` of each script whose files it wrote stays, and the error
  names the files that stay. No test can make the write fail after the
  check. A patch rename that fails after the checks puts back the `.sco`
  of every script, also of the scripts whose renames were done.
- The core-log warnings in the count of the compile summary (the test
  covers a warning of the selection).
- A throw inside a namer keeps the global names that it found
  (`VariableNamer::GlobalRenames`): no test can make the namer throw.
- A function whose first try fails and whose second try (the tighter
  bound) works gives no "Invalid branch target.": no fixture has it (of
  the 710 bytes of script 974 of the SCI0 template, each +1, 43 give the
  message, and in each both tries fail). The test checks one message
  for a function whose two tries fail.

## Outside this branch

- Local branches from `master`, not pushed: `fix/test-resource-temp-folders`
  (`fdd7f780`: two TestResource tests remove their temp folder),
  `fix/test-assert-order` (`98a7bccf`) and `refactor/remove-if-true-stubs`
  (`4e0b7123`: the three `if (true)` stubs of `Compile.cpp`).
- `%TEMP%` has 811 empty `SCI*.tmp` folders that TestResource left before
  its fix. Delete them?
- The local branch `backup/scic-cli-before-vcpkg` has the history before
  the vcpkg change, with the copied headers. Delete it?
- 49 files that `master` added in 2026 have a copyright header; your rule
  says that a new file has none. A task on `master` can remove them.
- The `.gitignore` of `master` does not ignore `vcpkg_installed/`: after a
  build of this branch, a checkout of `master` in the same folder shows the
  folder as untracked.
- 12 asserts on `master` make their message before the call, so a failure
  shows no text: `TestKeywordCodegen.cpp` lines 133, 146, 605, 610, 641,
  681, 711 and 745, `TestDecompile.cpp` 97 and 113,
  `TestDecompileBatch.cpp` 89 and 90.
- CLI11 is pinned at 2.0.0 (2021). A newer version is a download, which
  needs your permission.
- The first vcpkg build downloaded more than the list that you approved:
  PowerShell 7.6.3 (about 100 MB) and two msys2 packages.
- When this branch merges, take the baseline of the QfG4 golden dump
  again: the real-game tools now read `src\Decompiler.ini` and resolve the
  enum names.
- A task of another session fixes the decompile crash of script 755 of
  KQ5 (branch `fix/decompile-missing-map-key`).
