# `scic` CLI work: open items

Branch `feat/scic-cli`, from `master` at `0dc1fef5`; not pushed. This file
lists only the open items: the next work, the review findings that are not
fixed yet, the known gaps, the questions for you, and the work outside the
branch. The commit messages record each step, each review and its fixes;
the design is in `plan.md`. A commit that closes an item removes it here.

## Next

- The review of the fixes since `338ee1a7`: the nits of the C1 fixes,
  and the fixes of the reviews of `85ac9717`, `338ee1a7`, the vcpkg
  change and `103b8b23`.
- Your request (2026-09-24): de-duplicate the test helpers and the
  asserts, with one shared test-support file.
- Then the other review findings below, then the optional E1 (plan
  section 9), or stop before it.
- Push and pull requests only with your approval.

## Review findings that are not fixed

### The C2 fixes (`3cf3e33f`): 4 nits

- A decompile dry run does not check that the run can write. A read-only
  `.sc`, `.sco`, main's `.sco` or `game.ini`, or a file named `src`, makes
  the run give 9 or 3, and the dry run 0.
- A "wrote" or "would write" line names a `.sco` that keeps its bytes
  (`SaveSCOFile` does not write it; 26 of 63 files for SCI0 `--all`).
- Tests: the crash item test reads only the last item; no test gives exit
  6 for a decompiler error (a probe: add 1 to byte 7 of script 974 in a
  patch file); the dump test uses `-q`, so "a dump is not a warning" cannot
  fail; no test for the "Invalid branch target." gate.
- Texts: `DecompileRun.h` says that `files` lists "the src folder"; plan
  4.6 says that `sco --all` lists a `game.ini` name with no source (it
  also lists a name from a `.sco`).

### `ccadff0c` (the compile batch): 7 nits

- Two scripts with one compiled number: only one `.sco` goes back
  (`_changedObjectFiles` keeps one script for each number).
- The length check (`FileWrite.cpp`) measures the path as it is given, and
  counts bytes, not characters. The CLI gives absolute paths; a relative
  path of another caller passes the check, and the write fails after the
  tables.
- The refusal says "The .sco files are back" before the restore runs; the
  `objectFiles` text repeats "[io]"; after an abort, "Correct the scripts
  that failed" has no failed script; the GUI says "Put back" for a file
  that it removed.
- A dry run into the game's patch files checks nothing: with a read-only
  `script.904`, the run fails and the dry run passes (older).
- No test: the restore of the destructor, `objectFiles` in the exit code
  and in `Succeeded()`, the GUI lines, a restore that fails.
- A write that fails after the check can keep files, and `Finish` then
  puts back the `.sco` of every script (inspection only).
- An abort in `OnPassStart` loses the pass that finished (older).

### `8fc8e984` (the decompile batch): 7 nits, 1 question

- A batch that throws after a naming, outside the exception boundary of a
  script (for example at "Updating global variables in script 0"), does
  not write main's `.sco`. A written script then uses a name that main's
  `.sco` does not have, and the summary still prints "Globals named".
- No test covers the batch part of `Succeeded()` or of the exit code.
- The owner rule of a file title depends on its spelling: `owned` is
  case-sensitive, so `MENUBAR.sc` gives `MenuBar_979`.
- The stale check after a group skips the failed scripts of the group. A
  failed script can change main's `.sco`; its old file then uses the old
  name and is not listed.
- A throw in any namer loses names, not only in the naming rounds.
- `--stdout` after Ctrl+C says "Decompiled 1 of 1 scripts", and prints no
  source.
- Texts: the comment of `stale` does not name a batch that threw;
  "(Internal)" is wrong for a `DataError`, which keeps its code.
- Question: with `--stdout`, a batch that throws after pass 1 prints the
  pass-1 source, with exit 1. Print nothing?

### `cd1d5ee3` (the C3 fixes): 7 nits, 1 question

- Test gaps: no test fails for the absolute `--out-dir`, for the batch
  warnings in the count, for the dry-run text "would be written", or for
  the plural texts of decompile and sco.
- The assert "a commit that failed lists no file" tests nothing: the
  shadow check fails in `Start`, before any outcome.
- A dry-run summary says "a run would write them" when the commit check
  fails.
- The summary does not count the warnings of the selection and of the core
  log.
- Each include error prints and counts twice: `sci.sh` includes `keys.sh`,
  so `Update` loops again and tries the failed include again.
- The include-read error has its path in lower case.
- 43 test asserts make their message before `Run` runs, so a failure shows
  no output.
- Texts: the plan's C3 row lists `objectFileChanged`; a dry run gives no
  pass warning.
- Question: should "The else clause must be the last clause in a cond."
  be a warning? The parser drops the clauses before it; today it is
  `info`, and `-q` hides it.

## Known gaps

### Command line

- A damaged or empty `resource.map` opens: `list` shows no script, with
  warnings, and exits with 0 (the format detection is permissive).
- A hard link outside the game folder to a file of the game passes the
  `--log` check, which compares folders.
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
  Beamish is not supported.

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
  error line, the pure-call handler, a second `abort()` on another thread,
  the lock of `CliOutput`.
- The compile dialog, the shadow question, the Decompile dialog,
  `MainFrm.cpp` and `ScriptDocument.cpp`; 7 of the 8 parser sites that
  give a 1-based line.
- The exception boundaries around `batch.Run` and the stale check, the
  guard of 100 groups, the `NoDbugStr` path.
- The tables-first order of an output-folder write, and the change count
  of a compile that throws.

## Questions for you

- A damaged `resource.map`: should `list` exit with 3 ("cannot open the
  game") and not 0?
- The `info` line for "The else clause must be the last clause in a
  cond." (see `cd1d5ee3`).
- `--stdout` after a batch that throws (see `8fc8e984`).

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
