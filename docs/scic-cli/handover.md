# `scic` CLI work: open items

Branch `feat/scic-cli`, from `master` at `0dc1fef5`; not pushed. This file
lists only the open items: the next work, the review findings that are not
fixed yet, the known gaps, the questions for you, and the work outside the
branch. The commit messages record each step, each review and its fixes;
the design is in `plan.md`. A commit that closes an item removes it here.

## Next

- The work plan of plan section 9 is done, E1 included. Left: the open
  items below, most of them limits with a reason.
- Start a new session for each milestone (AGENTS.md, "Work with AI
  agents").
- Push and pull requests only with your approval.

## Review findings that are not fixed

### `2b795d63` (the open items of the handover): 1 nit

- The `.scd` capture of a batch goes by the script number, and the `.sco`
  capture by the title. Two sources that declare one number (the GUI's
  compile-all does not refuse a name conflict): after a refused commit,
  the second one puts back the `.scd` that the first one wrote.

## Known gaps

### Command line

- A compile of `--all` with more than one pass prints its diagnostics at
  the end (those of the last pass: a script that failed only in an earlier
  pass compiled in the last, so it has no error line). A run of one pass
  prints them as each script is done.
- An error in an include that is not a header names the including script
  and the line of the include (the merge moves the include's code into the
  script).
- The corpus sweep of 2026-09-24 (93 folders): script 755 of KQ5 (EGA and
  floppy) fails with "invalid map<K, T> key" `[internal]` (a task of
  another session works on it); script 995 of Hoyle 3 cannot be read;
  the decompiler reports errors in scripts that it wrote for Longbow, GK
  and the KQ4 `patch\NEW` folder (exit 6); 86 of 93 decompiled games do
  not compile back with 0 errors (limits of the decompiler); Willy
  Beamish is not supported (its map does not open: exit 3, "its lookup
  table has no end").
- A decompile warning of the naming rounds (between the passes) has no
  script in its text: the rounds give no message for each script.

### Compile and decompile

- The editor's line rule stays (the GUI and the compiler agree on the
  lines): a bare LF in a CR LF file, or a CR alone, does not start a line.
  The compile warns at the first such line.
- A `(script# X)` whose define comes from an include outside `src\` is not
  read; the name then comes from the `.sco`, or is `nNNN`. The name map
  has no data folder, so it does not read the headers of `include\`.
- An old `.sco` that lacks an export: with no slot 1 in the `Obj.sco` of
  the SCI0 template, the procedure is `proc999_1` but its calls stay
  `localproc_0022` ("Unknown procedure"); with no slot 2, two procedures
  get the name `proc999_2` (older than the branch).
- The GUI reads `game.ini` itself, so a script name there with `\` or `/`
  still gives it a path; `scic` does not use such a name, and `list` warns.
- The class-table changes of a script that fails stay in the tables of the
  batch (a species with no class; nothing uses it).
- The `.sco` files that `script sco` makes have other string property
  values than the compiler's (the compiler writes temporary string tokens
  there), so the first compile after `script sco` writes them again.
- The compiler gives each class its species by position
  (`GenerateScriptResource.cpp`): a leftover class, or classes that moved
  in a source, move species. `SpeciesTable::Load` keeps the positional
  order for a script with a leftover class (57 scripts in 27 GOG folders).
- `(or (and a b) c)` gives Sierra's value, but not Sierra's bytes.
- A keyword followed by `#` is a name (`(if#)` parses as a call). The
  syntax colouring shows `#dungeon#` as `#dungeon` and `#`. An instance
  named `if#x` or `if_x` does not compile again (`KeywordP` ends a keyword
  at any character that is not a letter or a digit).
- The decompile run loads the lookups for each run, and the stale check
  reads every `.sc` of the game, except those of the group, once for each
  group (time only).

### Build and CI

- CLI11 2.6.2 builds as a library, with the overlay triplet
  `triplets\x86-windows-static-v143` (the v143 toolset). Checked locally,
  also the ASan link of the unit tests; the CI runners are checked by
  inspection only (they have the v143 toolset; the next CI run shows it).
- The CI cache of vcpkg (`VCPKG_DOWNLOADS` and the binary cache, keyed on
  `vcpkg.json`) is checked by inspection only: the next CI run shows it.
- The CI fetch step of the vcpkg registry is checked by inspection only:
  vcpkg reads the baseline and the ports from the git objects that the
  fetch gives. A runner image that is older than the baseline was not
  tried.
- When `vcpkg.json` is gone (for example a checkout of `master`), the
  read tlog `ScicVcpkgManifest.read.1u.tlog` of each project stays and
  names it; the up-to-date check of Visual Studio may then see the
  projects as never up to date, until a Clean (not tried). The build of
  `master` does not run the target of this branch, so only the merge (or
  a Clean) ends it.

### Engine and resources

- A patch rename can fail after the checks (another program locks the file
  at that moment); the renames before it stay done.
- `WriteBytesToFile` is not atomic: it writes the file in place, with the
  sharing that the dry-run checks copy (`CheckFileCanBeReplaced`). A write
  through a temporary file and a rename would change the sharing and the
  attributes of the file.
- `WriteResource(entity)` gives `Cancelled` when `PerformChecks` says no.
  With no GUI, a yes/no check gets "no", and some checks are not a choice
  (duplicate message tuples, an audio map entry over 16 MB). The script
  commands do not reach them.
- In throw mode, a struct read (`operator>>`, also for `int16_t` and
  `uint32_t`) gives zeros, and `read_data` keeps its buffer; both only set
  the fail state. Real resources rely on it: with a throw, audio 15 of
  Hoyle Classic Card Games and audio 200 of the SQ6 demo no longer load (a
  corpus run of the review of `2b795d63`). A change needs a corpus sweep
  of every type.
- A failed write of the package after a volume moved (the map cannot be
  replaced) keeps `resource.map.bak` and the `.bak` files that did not
  move, and its error names them; a rename puts the game right.
- The SCI0 LZW decoder fails on a token that is not in its table; other
  damage (a stream that ends before its output is full) still gives no
  error.
- An SCI2.1 package header has no type mark, so a zeroed header of view 0
  reads as a valid empty resource. Damage that sets only the two sizes to
  0 reads as an empty resource.
- The Decompile dialog's "Reset filenames" protects no file (older than
  the branch). The dialog names the scripts through `game.ini`, so it
  needs a GUI helper with no name map.

### The core library (E1)

- A file of `Src\Util` is in the core or in the GUI library by its project
  (`SCICompanionCore.vcxproj`); its folder does not tell.
- The MFC guard of the core (`__ATLDBGMEM_H__` in its precompiled header)
  stops the compile with a text about `<atldbgmem.h>` (the text of
  `afx.h`; the comment in the header explains it).

### No test (checked by inspection only)

- The GUI hooks of E1: the message box, the pic-check dialog, the
  script-removal dialog of `AppState::OnLastScriptDeleted`, the pen pattern
  bitmap of `RasterView` (`CreatePatternBits`), and the output pane of the
  WAV conversion notes.
- A Ctrl+C of `list` while it reads the scripts: the tests set the flag
  before the read (nothing prints or calls back during the read).
- A Ctrl+C of a compile that comes after the second abort check of a new
  pass: the `Step` that starts a pass compiles its first script with no
  other check, so the commit has that script of the new pass, as after an
  abort at any later script (the pass before is withdrawn; no test can set
  the flag between the two checks).
- A running Visual Studio after a change of `vcpkg.json`: the build sets
  the hash at each build, and a read tlog names `vcpkg.json` for the
  up-to-date check (checked from the command line, not in Visual Studio).
- A write of `sweep.csv` that fails part of the way (the stream now has a
  buffer of 1 byte, so the write gives the error and the cut-back runs).
- The Ctrl+C handler itself (the tests set the cancel flag), the core-log
  error line, the pure-call handler, a second `abort()` on another thread
  (an abort of two threads at the same moment can still exit with 3), the
  lock of `CliOutput`. The console output of `StdConsole` (a console gets
  UTF-16 with `WriteConsoleW`; the tests use a string console).
- The compile dialog, the shadow question, the Decompile dialog,
  `MainFrm.cpp` and `ScriptDocument.cpp`; 7 of the 8 parser sites that
  give a 1-based line.
- The exception boundaries around `batch.Run` and the stale check, the
  guard of 100 groups, the `NoDbugStr` path.
- The tables-first order of an output-folder write, and the change count
  of a compile that throws.
- A write into an output folder that fails after its check (a full disk):
  the files of each script that it wrote stay, the error names them, the
  summary counts them and `-v` lists them (`report.keptWrites`). No test
  can make the write fail after the check. A patch rename that fails after
  the checks puts back the `.sco` of every script, also of the scripts
  whose renames were done.
- The core-log warnings in the count of the compile summary (the test
  covers a warning of the selection).
- A throw inside a namer keeps the global names that it found
  (`VariableNamer::GlobalRenames`): no test can make the namer throw.
- A function whose first try fails and whose second try (the tighter
  bound) works gives no "Invalid branch target.": no fixture has it (of
  the 710 bytes of script 974 of the SCI0 template, each +1, 43 give the
  message, and in each both tries fail). The test checks one message
  for a function whose two tries fail.
- The DIB section of `CreateBitmapFromResource` that goes when the copy
  buffer cannot be allocated; the audio cache source made for a read that
  refuses a write (Internal); `RemoveEntries` and `SaveOrRemoveNegatives`
  of the audio cache, which give their errors to the GUI; the empty
  `catch (...)` blocks that now log through `CoreLogCurrentException`.

## Outside this branch

- The compiled scripts of the SCI1.1 template do not match their sources
  (`Main` exports 15 slots and `Main.sc` lists 13; `DebugHandler` exports
  `dInvD`): issue laurence-myers/SCICompanion#201.
- Local branches from `master`, not pushed: `fix/test-resource-temp-folders`
  (`fdd7f780`: two TestResource tests remove their temp folder),
  `fix/test-assert-order` (`98a7bccf`) and `refactor/remove-if-true-stubs`
  (`4e0b7123`: the three `if (true)` stubs of `Compile.cpp`).
- The local branch `backup/scic-cli-before-vcpkg` has the history before
  the vcpkg change, with the copied headers. Delete it?
- The manual worktree `I:\Code\Esoteric\scic-rv1` (detached at `847e59cc`,
  with a partial copy of `vcpkg_installed`). Delete it?
- 49 files that `master` added in 2026 have a copyright header; your rule
  says that a new file has none. A task on `master` can remove them.
- The `.gitignore` of `master` does not ignore `vcpkg_installed/`: after a
  build of this branch, a checkout of `master` in the same folder shows the
  folder as untracked.
- 12 asserts on `master` make their message before the call, so a failure
  shows no text: `TestKeywordCodegen.cpp` lines 133, 146, 605, 610, 641,
  681, 711 and 745, `TestDecompile.cpp` 97 and 113,
  `TestDecompileBatch.cpp` 89 and 90.
- The first vcpkg build downloaded more than the list that you approved:
  PowerShell 7.6.3 (about 100 MB) and two msys2 packages.
- When this branch merges, take the baseline of the QfG4 golden dump
  again: the real-game tools now read `src\Decompiler.ini` and resolve the
  enum names.
- A task of another session fixes the decompile crash of script 755 of
  KQ5 (branch `fix/decompile-missing-map-key`).
