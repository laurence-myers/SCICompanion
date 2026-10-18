# `scic` CLI work: open items

Branch `feat/scic-cli`, from `master` at `958048a9`. This file
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
  another session works on it); the decompiler reports errors in scripts that it wrote for Longbow, GK
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
- Patch headers (`GetPatchDataGap`): an SCI2 pic patch that starts with
  `81 80` is read at 26 + byte 3, and Sierra's SCI32 interpreter reads it
  at 4 (`ResPic::SeekToData`). Sierra's SCI2 pic patches start with
  `81 81 00 00` (read at 4), and SCI Companion writes that form; no `81 80`
  file of an SCI2 game is known.
- SCI Companion writes a palette patch as `8B 00` and the data (read at 2).
  Sierra's interpreters take a palette patch at 4 + byte 3
  (`ResPal::SeekToData` of SCI32, `VOLLOAD.C` of SCI16), so they would read
  such a file at another offset. Found by inspection; not run in an
  interpreter or in ScummVM.
- The Decompile dialog's "Reset filenames" protects no file (older than
  the branch). The dialog names the scripts through `game.ini`, so it
  needs a GUI helper with no name map.

### Pic commands

Numbers from `UnitTests\Tools\PicRoundTripSweep.ps1` of 2026-10-11 on
copies of 8 games (the hi-res and the lo-res LSL6, SQ5, SQ6, GK1 CD, QfG4
CD, PQ4, KQ7). Each image that `import` took came back byte for byte.

- A cel with run-length data in an SCI2 pic gets no export and no import
  (`Unsupported`): 133 of the 148 pics of PQ4, 46 of the 97 of GK1, 1 of
  KQ7. No code reads or writes the run-length data of such a cel for the
  pic commands.
- The palette of a pic cannot grow in place: `--palette replace` changes
  only entries that the pic stores, and it does not change a flag byte.
- The pic data has no form for three commands with a negative point, and
  the pic writer gives bad bytes for them: a line or a pattern at a
  negative point that is not a short or medium step from the point before
  it in the same command (a longer step, or another command between the
  two); a line that starts at a negative point and is longer than a medium
  step; a pattern at a negative point with other flags or another size
  than the pattern before it. The pic reader gives no such command; an
  edit in the pic editor can.
- The pic reader takes two things that the pic writer does not keep. No
  pic of 30 games (SCI0 to SCI1.1, 2174 pics) has one of them. The first:
  a pen pattern with a pattern number of 120 or more (the byte of the
  number is `0xf0` or more). The reader takes it as the first pattern of a
  pattern command; the writer can put it after another pattern, where the
  byte reads as an opcode. The second: a line that ends at an x of 3840 or
  more, after a line with absolute coordinates. The writer gives it an
  absolute coordinate, whose first byte (`0xf0`) reads as an opcode;
  `ReplaceCelPixels` refuses such a pic.
- Pics 100 and 14000 of KQ7 give `[format]` in `list` and `export`: the
  pixels of cel 0 are not all in the resource.
- `import` takes each `.toml` file of `--in-dir`, also one that another
  tool wrote: there is no option that selects pics of the folder.
- The palette readers take each stored 3-byte entry of a palette as used
  (`ReadPalette` gives it the flag 1), and they read a header with 1
  palette. The SCI32 interpreter gives the entries the flag byte of the
  header, and a header can declare no palette. `scic dev palette-use`
  reads the flag byte of the header; `scic pic list` and the `.toml` file
  of `export` do not. For the views and the palette resources of the
  hi-res LSL6, the two rules give the same table. `scic view list` and the
  `flags` of the `.toml` file of `scic view export` read the flag byte of
  the header; the `used` key and the colour table of `view export` come
  from `ReadPalette`. No view of 7 games (Eco2, GK1 CD, KQ6 CD, LB2, QfG4
  CD, SQ6, the hi-res LSL6; 1015 palettes of 3-byte entries) has the flag
  0 in such a header.
- `--out` of `scic dev compare-structure` and of `scic dev compare-meaning`
  can be a file of the game: the table then takes its place. `scic dev
  palette-use` refuses such a file.

### View commands

- `scic view palette --clear-used` cannot take the mark from an entry in
  the middle of a palette with 3-byte entries: such a palette has one flag
  for all its entries, so only its last entries can go (a smaller count).
  A view that stores entries after the range fails. Of the 42 views of the
  hi-res LSL6 that mark the entries 80 to 235 and have no pixel there, 8
  store more: 339, 375, 376, 378, 379 and 3312 store 0 to 247, and 861 and
  862 store 0 to 255. The tool does not change for them: they get the new
  colours of their pic (`--colors-from`). No code changes such a palette
  to 4-byte entries (the resource would grow).
- The check that a view reads back with the same cels (`SameViewCels`)
  sees what the reader of the views reads. Palette bytes that are also
  bytes of the row table of an SCI2 cel, or of a header field that the
  reader skips, are not found.
- The reader of the views gives the padding bytes of the rows of a cel of
  a mirrored loop no value (`SyncCelMirrorState`), so two reads of one view
  can have other bytes there (30 of the 548 views of the hi-res LSL6 in
  one run). The pixels are the same. `scic view palette` and `scic dev
  palette-use` read only the pixels.

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
- The pic commands on a game with SCI2 pics, from the command line to the
  files: CI has only the SCI1.1 template. The tests call the parts that
  need no game (`Src\Cli\CliPic.h`) with the SCI2 pic files, and the
  round-trip sweep (local) runs the commands on a game. Two mappings of
  options have no test and no sweep step: `--cel all` to "every cel" in
  `RunPicExport` has the sweep only, and `--cel <index>` of `import` for a
  cel that is not cel 0 has neither (the sweep uses `--in-dir`).
- A patch file of `import` on the screen of a game: Sierra's interpreter
  was not run with one, and ScummVM was not run with one of an SCI1.1
  game, or with a patch of `--palette replace`. ScummVM 2026.3 draws an
  SCI2 pic patch with the palette kept correctly (the hi-res LSL6: the
  background cel and an overlay cel).
- A patch file of `import` that fails to write after its check (a full
  disk; `ResourceBlob::SaveToFile` gives the error, exit 9), a pic that is
  too big for a resource after the import (`CheckResourceSize`), an
  internal error of one pic (exit 1), and an input folder that cannot be
  listed (`Io`).
- The branches of `ReplaceCelPixels` that refuse an SCI1.1 pic whose size,
  priority bars or palette change in the write: the tests have a pic
  whose commands change.
- The exception boundary of a resource in `scic dev palette-use` for an
  exception: the damaged view of the test gives an error value. The
  command on a game with SCI2 views has a local run only (the hi-res LSL6:
  548 views, no error); CI has the SCI1.1 template. A view of SCI1 (its
  used entries come from the palette as the reader gives it) has no test.
- `scic view list` and `scic view export`: a repair that a reader logs
  while the resource of a view is found and checked (`ReadViewSource`,
  before the view is read; `RowOf` and `ExportView` give it as the error
  of the view). No test can make a reader log there: the tests have the
  repairs of the view reader, which `DescribeView` gives.
- `scic view palette`: the error for a view of SCI1.1 that has the form of
  an SCI1 view, an internal error of one view (exit 1), an exception while
  a view is read (the patch file of an earlier run then goes too), a
  repair that a reader logs while the resource of the view is found and
  checked (the tests have a repair of the view reader), and a patch file
  that fails to write after its check (no file with a part of the view
  stays). The command on a game with SCI2 views has a local run only (4
  views of the hi-res LSL6: the patch files have other bytes only in the
  palette, and `scic dev palette-use` reads the game copy with them as
  expected; a dry run of each action on the 548 views gave no internal
  error); CI has the SCI1.1 template, and the tests call the functions
  under the command with views of SCI2 that the view writer makes. No test
  has a view with a mirrored loop.
- A patch file of `view palette` on the screen of a game: ScummVM and
  Sierra's interpreter were not run with one. That a view gives its marked
  entries to the screen comes from the ScummVM source
  (`GfxPalette32::mergePalette`, `HunkPalette::toPalette`).
- A patch file of `view import` or `view add` on the screen of a game:
  Sierra's interpreter was not run with one, and ScummVM was not run with
  one of an SCI1.1 game. ScummVM 2026.3 draws them correctly for the hi-res
  LSL6 (SCI2): a changed cel, a mirror loop, raw cels and a raw mirror
  loop. A mirror of a mirror (the `headerCels` rule) was not on the
  screen. The mirror headers of `view add` come from the ScummVM source
  (`GfxView::initData`, `CelObjView`): each mirror gets the cel offset of
  the loop at the end of its chain, and as its cel count the `cels` key of
  a mirror that mirrors it (`headerCels`), else the count of that loop.
- `view import` and `view add`: a view too large for a resource
  (`CheckResourceSize`, before the patch file of an earlier run goes) can
  happen only in an SCI2 game (in SCI1.1 the 64 KB check comes first), and
  the tests have no SCI2 game.
- `view remove`: three `Internal` errors (the second read of the map does
  not give the entry; an entry that the patch source did not list; a
  removal that leaves as many copies of the view, in `RemoveView`), the
  message box text of the GUI forms (`ShowRemoveError`), and that the GUI
  delete does not tell its syncs after a removal that failed.

## Questions for you

- `scic pic import` refuses a pixel whose palette entry neither the pic
  nor palette 999 marks as used (your rule; `--allow-unused-indices` makes
  it a warning). Some games have such pixels in their own pics, so the
  import of an image with no change fails: 90 of the 165 exported pics of
  KQ7, 5 of 51 of GK1, 3 of 59 of QfG4, 1 of 119 of SQ6 (none in LSL6 and
  SQ5). Should the check also accept an entry that the cel has already?
  Then only an entry that is new to the cel is an error. `view import`
  and `view add` have the same rule: the round-trip sweep of Eco2 needs
  `-AllowUnusedIndices` (the hi-res LSL6 does not; the sweeps of GK1, KQ6,
  LB2, QfG4 and SQ6 ran with it).

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
