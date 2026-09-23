# SCI Companion command-line tool (`scic`): plan

- Date: 2026-09-23 (revision 3)
- Written against branch `master` at `0dc1fef5`. The work is on branch
  `feat/scic-cli`.
- Status: in progress. `docs/scic-cli/handover.md` has the current state
  and how to resume. The line numbers in this plan are for `0dc1fef5`; the
  code moves as the work goes on.
- Basis: code reading, three research passes (GUI coupling, resource saving,
  headless test harness), a coupling inventory, a failure-handling
  inventory, a check of the two candidate libraries, and a check in the code
  of each key claim.
- Path prefix: `Src\` means `SCICompanionLib\Src\`.
- Revision 2 added your decisions: the exe is `scic.exe`; compiled output
  goes to patch files by default, with explicit options for patch files or
  the resource package; `decompile` overwrites `.sc` files as the GUI does;
  script commands are in a `script` command group; refactoring is allowed
  to decouple the code from MFC and the GUI.
- Revision 3 adds failure handling: failures are values (`Result`), not
  exceptions, at every boundary that the CLI and the services use, and
  batches report partial success (section 6).
- Revision 4 adopts what applies from lucasartsifier's `scicompile`, an
  earlier headless port of this compiler (section 14): six compiler fixes
  (phase K), `scic script sco`, a compile-all that repeats until the `.sco`
  files are stable, and compile output to another folder.
- Revision 5 (your change of plan): the CLI works on a game that SCI
  Companion never opened, so it does not need `game.ini`. A script-name map
  replaces the `[Script]` section (section 3.4).

## 0. Summary

- Add a console program, `scic.exe`. It runs script operations with no GUI.
- The script commands are in one command group:
  - `scic script list <game>` shows the number (ID) and the name of each
    script. The name comes from `game.ini`. When `game.ini` has no name,
    `scic` derives the name from the compiled script. `list` writes nothing.
  - `scic script decompile <game> (<scripts> | --all)` decompiles some
    scripts or all scripts. It overwrites existing `.sc` files, as the GUI does.
  - `scic script compile <game> (<scripts> | --all)` compiles some scripts
    or all scripts. By default it writes loose patch files (`110.scr`,
    `script.110`). `--to package` writes into the resource package
    (`resource.map` and a volume). `--to patch` selects patch files explicitly.
- Failures are values. A new `sci::Result<T>` (on tl::expected) carries a
  structured `sci::Error` across every boundary that the CLI and the
  services use. An exception boundary (`sci::Guard`) turns any exception
  that escapes the old code into an `Error`. No exception leaves a service. Batches
  return a report with a status for each script, so partial success is
  explicit.
- The CLI does not need `game.ini`. It takes script names from `game.ini`
  when the file exists, else from the files in `src\`, else from the
  compiled scripts. It never creates `game.ini` unless you ask.
- A new `GameSession` object replaces the GUI object `AppState` in the
  script engine. The CLI then runs with no `AppState`, no GUI object and no
  modal dialog. The GUI keeps `AppState`, and `AppState` owns a `GameSession`.
- New library services (`ScriptCatalog`, `CompileBatch`, `DecompileRun`)
  have MFC-free interfaces. The CLI and the GUI dialogs both call them, so
  they share the code and the tests.
- Phase K makes the compiler agree with Sierra's compiler on six points
  that `scicompile` found (section 14). Five are fixes. The sixth, `(and a
  b)` in a value position giving the deciding operand and not 1 or 0, was
  already fixed in the branch base (`f5f7a01b`); K1 pins its bytes.
- `scic script sco` makes the `.sco` files from existing source and the
  game's compiled scripts, so a source tree from another decompiler (for
  example sluicebox's sci-tools) can be compiled.
- The work is 21 PRs in six phases: failure-handling foundation, safety
  fixes, decoupling, compiler fixes, services, CLI. An optional seventh
  phase splits a `SCICompanionCore` library with no MFC GUI headers.

## 1. Requirements

From you:

| # | Requirement |
|---|---|
| R1 | Make the resource operations available as a CLI. Start with scripts: compile and decompile. |
| R2 | Save compiled output into the monolithic resource package(s), or as loose patch files. Patch files are the default. Explicit options select either one. |
| R3 | Compile and decompile all game scripts in bulk, or individual scripts. |
| R4 | List the scripts with their number (ID) and name. A name can need a lookup or some decompilation. |
| R5 | Put the script operations in a sub-command: `scic.exe script decompile <game> --all`. |
| R6 | `decompile` overwrites existing `.sc` files (no refusal). |
| R7 | Refactoring is allowed, to make the code easier to call from a CLI and to decouple it from MFC and the GUI. |
| R8 | Propagate failures with a non-exception method (a `Result` type). No failure mode stays unhandled when an exception is thrown. Return partial success and failure after a compile or decompile. |
| R9a | Adopt the changes from lucasartsifier's `scicompile` that apply to this code (section 14). |
| R9b | The CLI works on a game that SCI Companion never opened: it does not rely on `game.ini`. |

Added by this plan:

| # | Requirement |
|---|---|
| R9 | No modal dialog, window, sound or registry access, and no crash dialog. The program never waits for a click. |
| R10 | Clear exit codes and error text, for scripts and CI. |
| R11 | The CLI and the GUI give the same output for the same input, because they run the same code. |
| R12 | No silent data loss. `scic` refuses a package write that a patch file would hide. |
| R13 | Room for the other resource operations later, as more command groups. |

## 2. What the code does today

### 2.1 Projects and build

- The solution has four projects: `SCICompanionLib` (static library, MFC,
  all real code), `SCICompanion` (the GUI exe, a thin wrapper), `ProfUISLIB`
  and `UnitTests` (a VSTest DLL).
- Target: Release|Win32, toolset v143, C++17, MBCS, static MFC, `/EHsc`.
  The GUI exe goes to `$(SolutionDir)Release\`.
- The GUI post-build copies data folders next to the exe: `include\`
  (`sci.sh`, `keys.sh`), `Decompiler\` (`Decompiler.ini`, `game.sh`),
  `TemplateGame\`, `Objects\`, `Samples\`, `Customization\`, `Plugins\` and
  `Tools\` (`SCICompanion\SCICompanion.vcxproj:74-105`).
- The library finds these folders relative to the running exe
  (`GetExeSubFolder`, `Src\Util\util.cpp:1026-1042`;
  `GameFolderHelper::GetIncludeFolder`, `Src\Resources\GameFolderHelper.cpp:122-135`).
- CI (`.github\workflows\build.yaml`) builds the whole solution and copies
  `Release\*.exe` into the artifacts (line 55). A new exe in the solution
  ships with no workflow change.
- No CLI project ever existed. Commit `aaa344bc` (2017) split out
  `SCICompanionLib` "to allow the possibility of a command line tool".

### 2.2 Headless support that exists now

- `AppState` accepts a null `CWinApp` (`HasGui()`, `Src\Util\AppState.h`).
  The unit tests use this mode (`UnitTests\Helper.cpp:86-128`).
- The `AppState` constructor creates no window. It starts three idle worker
  threads, creates two GDI fonts, registers four clipboard formats and reads
  `<exe>\Customization\syntaxcolor.ini`. This is GUI state that a CLI does
  not need.
- With no GUI, `SafeMessageBox` shows no dialog (`Src\Util\AppState.cpp:889-903`).
  The output-pane calls do nothing (#181). Load warnings use
  `SafeMessageBox` (#186).
- The compiler and decompiler engines have no UI calls. The GUI coupling is
  in the code that drives a run (dialogs, main frame), in a few save paths,
  and in the global `appState` (section 2.8).
- Compile and decompile need no `.rc` resources. `UnitTests.dll` has no
  resource section and runs both.
- A console exe that links MFC must call `AfxWinInit`. The test DLL gets
  this call from MFC's `DllMain`.

### 2.3 Compile today

Entry points:

- One script: `CScriptDocument::OnCompile`
  (`Src\MFCDocuments\ScriptDocument.cpp:123-199`). GUI only (message box,
  output pane).
- All scripts, or the changed scripts: `CompileABunchOfScripts`
  (`Src\MFCFrames\MainFrm.cpp:2075-2139`). It runs the modal
  `CNewCompileDialog` (`Src\Dialogs\NewCompileDialog.cpp`). The dialog
  compiles one script for each posted window message, and pumps messages
  between scripts.
- The work for each script: `NewCompileScript(results, log, tables, headers, scriptId)`
  (`ScriptDocument.cpp:221-315`). It has no UI, but it is in a GUI document file.
  B3a moved it to `Src\Compile\CompileScript.cpp`.

The sequence, for one script or for many:

1. `DeferResourceAppend defer(map)` queues all resource writes.
2. `CompileTables::Load` reads vocab 996 (classes), vocab 997 (selectors),
   the kernel names and vocab 000 (`Src\Compile\CompileContext.cpp:82-94`).
3. `PrecompiledHeaders` keeps the parsed `.sh` headers across scripts.
4. For each script, `NewCompileScript`:
   - parses `src\<name>.sc`;
   - loads the `.sco` file of each `(use ...)` script from `src\`
     (`CompileContext.cpp:104-133`). A missing `.sco` is an error;
   - generates the code;
   - queues the text resource (auto text, only if it changed), the script
     and the heap (SCI1.1);
   - writes `src\<name>.sco` and `debug\NNN.scd` immediately. These writes
     are not queued and not atomic.
5. `CompileTables::Save` queues vocab 996 and vocab 997 if they changed.
6. `defer.Commit()` writes the queue.

The "compile all" list is the `[Script]` section of `game.ini`, in file
order (`CResourceMap::GetAllScripts`, `Src\Resources\ResourceMap.cpp:1140-1176`).
The partial compile filters this list and keeps the order. If the section
is empty, the dialog asks to scan `src\*.sc`.

| | One script (`OnCompile`) | Compile all (dialog) |
|---|---|---|
| Tables saved | only if the compile succeeded | always, when the dialog closes |
| After an error | stop | continue with the next script |
| Cancel | not possible | stops after the current script; the finished work is still written |

Diagnostics: `CompileResult` has a message, a script path, a line, a column
and a type (`Src\Compile\CompileInterfaces.h:98-164`). `CompileLog` is a
plain in-memory sink. The message text already has a GUI prefix
(`"Error: (file) ... Line: N, col: C"`, `CompileContext.cpp:819-830`).

### 2.4 Decompile today

- The engine is `DecompileBatch` (`Src\Compile\DecompileBatch.h/.cpp`). It
  reports through `IDecompilerResults` (`Src\Compile\DecompilerResults.h`).
  It takes any set of script numbers, from one script to all.
- It decompiles each script once and names the global variables across the
  whole set. It decompiles a script again only when a later name changes
  it. One syntax tree is in memory at a time.
- For each script, it writes `src\<name>.sc` and `src\<name>.sco`. It
  writes `Main.sco` when a global variable gets a name. It overwrites an
  existing `.sc` file (`DecompileBatch.cpp:471`).
- The dialog (`Src\Dialogs\DecompileDialog.cpp`) does four more things. A
  CLI must do them too:
  1. It creates `src\`. If `src\Decompiler.ini` is missing, it copies
     `<exe>\Decompiler\*` into `src\` (`:121-139`) with `SHFileOperation`
     and a window handle.
  2. If `[Script]` has no entries, it gives every script a name in
     `game.ini` (`:681-684`; `_AssignFilenames`, `:727-780`). The name is
     "Main" for script 0. Otherwise it is the first class (a class named
     "Game" has priority), or else the first public instance. A duplicate
     name gets a `_N` suffix.
  3. It runs the batch on a worker thread (`:803-887`) and adds up the
     statistics (functions decompiled, bytes, asm fallbacks).
  4. After a run on a subset of the scripts, it finds the other scripts that
     use a renamed global by its old name (`FindScriptsReferencingGlobals`).
     These are the "stale" scripts. It offers to decompile them again
     ("Re-decompile when globals change").
- Names matter even for one script. The decompiler writes `(use Name)` lines
  with the `game.ini` names. So all scripts need names before one script is
  decompiled.
- The decompiler config reads `<exe>\include\sci.sh`, `<exe>\include\keys.sh`
  and `<game>\src\Decompiler.ini` (`Src\Compile\DecompilerConfig.cpp:36-83`).
  A missing ini gives only a warning. The template games have no `Decompiler.ini`.

### 2.5 Script names and numbers today

- `game.ini` `[Script]` has lines `nNNN=Name`. A script with no entry uses
  the default name `nNNN` (`GameFolderHelper::FigureOutName`).
- `GlobalClassTable` loads every compiled script (script and heap, patch
  files first) to know its objects (`Src\Resources\Vocab99x.cpp:903-980`).
  This reads the object tables only. It is not a full decompile, and it
  writes nothing.
- The Decompile dialog lists each script resource with its name, and shows
  if the `.sc` and `.sco` files exist (`DecompileDialog.cpp:220-260`).
- Everything that turns a script number into a file name reads `[Script]`
  in `game.ini`: `GameFolderHelper::GetScriptFileName(n)`,
  `GetScriptObjectFileName(n)` and `FigureOutName(Script, n)`
  (`Src\Resources\GameFolderHelper.cpp:61-120, 327`), `SaveSCOFile(helper, sco)`
  (`Src\Compile\SCO.cpp:630-637`), and `CResourceMap::GetAllScripts`,
  `GetNumberToNameMap` and `GetScriptNumber` (`ResourceMap.cpp:922-1210`).
  With no `game.ini`, every script is `nNNN`: the decompiler writes
  `src\n110.sc` and `(use n255)`, and compile-all finds no script.
- The other `game.ini` values (`SaveToPatchFiles`, `Codepage`,
  `GenerateDebugInfo`, `NoDbugStr`) have defaults when the file is missing.
  A game folder opens with no `game.ini` (`SniffSCIVersion` reads only the
  resources).

### 2.6 How resources are saved today

- The destination is the `ResourceSourceFlags` value of each `ResourceBlob`:
  `ResourceMap` (the package) or `PatchFile` (a loose file).
- The only setting is `game.ini` `[Game] SaveToPatchFiles=true|false`
  (`GameFolderHelper.cpp:296-322`). If it is missing, the GUI saves to the
  package.
- The compile output always uses this setting. This applies to the script
  and the heap (`ScriptDocument.cpp:278,283`; since B3a, `CompileScript.cpp`), vocab 996 and 997
  (`Vocab99x.cpp:800,1099`), and the text resource (through
  `AppendResource(ResourceEntity&)`, `ResourceMap.cpp:611-615`). A caller
  cannot choose.
- Patch mode is a mode for the whole game, not only a save option:
  - In patch mode, most reads hide the package (`ExcludePackagedFiles`
    through `AddInDefaultEnumFlags`; `GameFolderHelper.cpp:312-316`,
    `ResourceMap.cpp:656-664`).
  - When you switch to patch mode in Game Properties, the GUI extracts every
    resource to a patch file. When you switch back, it rebuilds the package
    and deletes all patch files (`Src\Dialogs\GamePropertiesDialog.cpp:190-222`).
  - So the CLI must never change `SaveToPatchFiles` to select its output.
- Patch file names come from `GetFileNameFor`
  (`Src\Resources\ResourceUtil.cpp:62-88`):

  | Resource | SCI0 map format | Later map formats |
  |---|---|---|
  | script | `script.110` | `110.scr` |
  | heap | `heap.110` | `110.hep` |
  | text | `text.110` | `110.tex` |
  | vocab 996 / 997 | `vocab.996` / `vocab.997` | `996.voc` / `997.voc` |

- On read, a patch file always wins over the package
  (`GameFolderHelper.cpp:390-425`). Vocab 996/997 and compiled scripts also
  read the patch file first.
- A package write never touches patch files. A stale `110.scr` or
  `997.voc` hides the new package copy from the game and from the next
  compile. There is no warning. For vocab 997 this gives wrong selector
  numbers.
- A package write rewrites the whole volume and the map into `.bak` files,
  then renames them (`Src\Resources\ResourceSources.h:178-208`). Each
  rename is atomic. The set of renames is not.
- A patch write does one `.bak` file and one rename for each resource
  (`Src\Resources\PatchResourceSource.cpp:155-174`). A failure part way can
  leave a new `110.scr` with an old `110.hep`.
- The SCI1.1 map has no volume field. All SCI1.1 writes go into `resource.000`.

### 2.7 Problems that block or hurt a CLI

| # | Problem | Where | Effect on a CLI |
|---|---|---|---|
| P1 | The code that drives compile-all and decompile is in dialogs. | `NewCompileDialog.cpp`, `MainFrm.cpp:2075`, `DecompileDialog.cpp` | Must move into the library. |
| P2 | A caller cannot choose the package or patch files. | `ScriptDocument.cpp:278,283` (since B3a, `CompileScript.cpp`), `Vocab99x.cpp:800,1099`, `ResourceMap.cpp:611-615` | Needs a parameter. |
| P3 | `EndDeferAppend` always returns `S_OK`. Write errors go only to a message box. | `ResourceMap.cpp:280-347` | Wrong exit code; silent loss. |
| P4 | A nested `DeferResourceAppend` loses the outer queue. The inner destructor calls `AbandonAppend` after `Commit`. | `Src\Resources\ResourceMap.h:197-229`, `ResourceMap.cpp:268-279` | Silent loss if code nests batches. |
| P5 | Names go into `game.ini` even when the write failed. | `ResourceMap.cpp:324-330`, `:530` | Wrong state after an error. |
| P6 | The same resource queued twice corrupts the map (SCI1: duplicate entries). | `EndDeferAppend` | Duplicate selections must be removed. |
| P7 | `ValidateResourceSize` calls `AfxMessageBox` directly. | `ResourceMap.cpp:582-598` | A modal box can block the CLI. |
| P8 | With no GUI, `SafeMessageBox` text goes to a log file that is closed by default. The text is cut at 260 characters. | `AppState.cpp:870-903` | Errors vanish. |
| P9 | The patch writer ignores the result of `SaveToHandle`. An empty `.bak` file then replaces a good file. | `PatchResourceSource.cpp:169` | Data loss for an oversize resource. |
| P10 | The `.sco`, `.scd` and `.sc` writes ignore errors. | `Src\Compile\SCO.cpp:646-649`, `MakeTextFile` in `util.cpp` | Silent failure. |
| P11 | `CompileLog::CalculateErrors` adds to a running total. | `ScriptDocument.cpp:54-59` | Wrong error counts if one log is used for many scripts. |
| P12 | `SetGameFolder` throws `CUserException*` with no text. | `ResourceMap.cpp:1288-1293` | Poor "cannot open" message. |
| P13 | The diagnostic text has a GUI prefix. Some parser errors use 0-based lines: `LineCol::Line()` is 0-based (`Src\Util\sci.h:303`), and `SCISyntaxParser.cpp:1376, 1389, 1452, 1457, 1675, 1680, 1685, 1764` do not add 1. | `CompileContext.cpp:819-830`, `Src\Compile\SCISyntaxParser.cpp` | Editors cannot jump to the right line. |
| P14 | Script names are chosen in hash-table order, not by number. The `_N` suffix does not follow the number. | `DecompileDialog.cpp:727-780`, `Vocab99x.cpp:907` | `list` and `decompile` must share one ordered rule. |
| P15 | The 3-argument `DecompileScript(helper, n, results)` passes a null config, which is then used. | `ScriptDocument.cpp:344-355` (B3b removed it) | Dead code. It crashes if called. |
| P16 | The library has a leftover DLL-template `theApp` object. | `SCICompanionLib\SCICompanionLib.cpp:67` | A second `CWinApp` if a CLI links it. |
| P17 | `_fTrackHeaderFiles` is read before it is set. Worse, `DependencyTracker` takes the value by value and keeps a reference to that parameter, so every later read is undefined. | `AppState.cpp:82` and `:108`, `DependencyTracker.cpp:20` | Undefined value. |
| P18 | The engine reaches the game through the global GUI object `appState`. | section 2.8 | The CLI needs `AppState`, or the engine must change. |
| P19 | The data folders (`include\`, `Decompiler\`) must be next to the exe. | `util.cpp:1026-1042`, `GameFolderHelper.cpp:122-135` | `scic.exe` cannot move, and tests need `SetIncludeFolderForTest`. |
| P20 | The parser reads its input through the editor's text buffer class (CrystalEdit). | `Src\Util\CrystalScriptStream.h`, `Src\Compile\SyntaxParser.h:24` | The engine depends on an MFC editor class. |
| P21 | Failures use at least seven styles, and some are lost (section 2.9). | section 2.9 | No reliable exit code; no partial-success report. |
| P22 | Script names come only from `game.ini`. | section 2.5 | A game that SCI Companion never opened has no names: wrong file names, no compile-all. |

### 2.8 MFC and `AppState` coupling (inventory)

- The library's precompiled header (`SCICompanionLib\stdafx.h`) includes all
  of MFC (`afxwin.h`, `afxext.h`, `afxcview.h`, OLE, ODBC, DAO, common
  controls), GDI+ and Prof-UIS for every source file.
- MFC use in the engine folders is small:
  - `Compile\`: about 36 uses. Almost all are `ASSERT` and `DEBUG_NEW`. One
    is `CCrystalTextBuffer` (`DecompilerConfig.cpp:41`).
  - `Resources\`: about 70 uses, mostly in picture, sound and raster code.
    `ResourceMap.cpp` has five `AfxMessageBox` calls (save dialogs and the
    size check). `Vocab000.cpp` has three, in vocabulary editor functions
    (not on the compile path). `Components.cpp` uses `CPoint` and `CSize`.
  - `Util\`: the parser stream (`CrystalScriptStream.h/.cpp`), `util.cpp`,
    `ImageUtil.cpp`, `sciwin.h`, `CObjectWrap.h`.
- `appState` use in the engine:
  - `Compile\`: 24 references in 10 files. `Resources\`: 29 references in
    10 files (`Vocab99x.cpp` has 11). `Util\` codecs: 12. `ClassBrowser.cpp`:
    20. `ExtractAll.cpp`: 11.
  - The kinds in `Compile\` and `Resources\`: `GetResourceMap` (24),
    `LogInfo` (13), `GetVersion` (13), GUI flags (`_fDontCheckPic`,
    `_fNoGdiPlus`, `_fWarnOnUnusedInstances`, clipboard format: 8), and
    `GetClassBrowser` (1).
  - On every script read path: the codecs call `appState->LogInfo` when
    decompression fails (`Src\Util\Codec.cpp:398`, `CodecDCL.cpp`,
    `CodecSTAC.cpp`), and `Text.cpp:40,56` reads the codepage through
    `appState`. With no `AppState`, these are null dereferences.
- `CResourceMap` works with no `AppState`. It checks its app-services and
  recency pointers for null (`ResourceMap.cpp:507, 635, 1298`).
- The class browser helps the compiler only with error hints ("did you
  forget a `use`?", `CompileContext.cpp:845-888`). The compile is correct
  without it. `NewCompileScript` takes its lock (`ScriptDocument.cpp:224`).
  Since B3a, the GUI callers take the lock around the compile.
- The parser stream already copies the text. `CScriptStreamLimiter` builds a
  `ReadOnlyTextBuffer` (a `std::vector<char>` and line offsets) from a
  `CCrystalTextBuffer` (`Src\Util\CrystalScriptStream.cpp:34-64`). Only
  that constructor and `CPoint` tie it to CrystalEdit.
- `CCrystalTextBuffer::LoadFromFile` finds the line-ending style from the
  first line feed in its first 32 KB read: CR LF if a CR comes before it,
  LF CR if a CR comes after it, else LF alone; with no line feed, CR LF.
  It then splits only on that style (`Src\CrystalEdit\CCrystalTextBuffer.cpp:289-397`),
  so a CR-only file is one line. After a partial match it does not test
  the breaking character again, and a NUL ends a line's text. A new loader
  must copy this rule (B2 does, in `SplitScriptText`), or line numbers
  change.

### 2.9 Failure handling (inventory)

Counts are for the engine folders (`Src\Compile`, `Src\Resources`,
`Src\Util`) unless stated.

| Style | Count | Notes |
|---|---|---|
| `throw std::exception("…")` | 52 | Mostly bad data in resource readers. The constructor with a message is a Microsoft extension, not standard C++. |
| Decompiler control-flow exceptions | 38 | `ControlFlowException` (22) and `ConsumptionNodeException` (16). Caught inside the decompiler for the asm fallback (`ControlFlowGraph.cpp:2862`, `DecompilerNew.cpp:3734, 3755`). They never leave it. |
| `HRESULT` returns | 28 functions | For example `AppendResource`, `GetScriptNumber`, `SaveToFile`. |
| `bool` returns | 30 or more | Load, save and create functions. The reason for a failure is usually lost. |
| Stream state | `sci::istream` | Sets a fail bit by default. Resource-entity reading turns on throw mode (`ResourceEntity.cpp:77`), and `CreateResourceHelper` then substitutes a default resource. |
| Message boxes | 31 `AfxMessageBox`, 10 `SafeMessageBox` | Save paths, validators, editors. |
| Error text in objects and logs | `DecompilerConfig::error`, `CompileLog`, `IDecompilerResults`, `LogInfo` | Diagnostics and some failures mixed together. |

- There are about 85 `catch` sites in the whole library, and 24 are
  `catch (...)`. Some swallow the failure with no report:
  - `TextReadFrom` (`Resources\Text.cpp`): a text resource that fails part way keeps the
    texts read so far, with no message.
  - `Dialogs\DecompileDialog.cpp:866`: the decompile worker swallows every
    exception, including a failure to load the lookups or the config.
  - `Resources\VersionDetectionHelper.cpp:902, 925`: failed probes keep the
    default value, with no note.
  - `Resources\AudioCacheResourceSource.cpp:453, 470`.
- Some return values are ignored: P9, P10, the compile's `AppendResource`
  results (`ScriptDocument.cpp:278, 283`, since B3a in `CompileScript.cpp`;
  `Vocab99x.cpp:800, 1099`).
- A batch cannot report partial success: `EndDeferAppend` always returns
  `S_OK` (P3), and the dialogs count results in window messages.

## 3. Design

### 3.1 Layers

```
scic.exe (main.cpp only)                SCICompanion.exe
      |                                       |
Src\Cli\                                AppState, frames, views, dialogs
  arguments, console, exit codes          (owns one GameSession; the dialogs
      |                                    call the services)
      +-------------------+-------------------+
                          |
Services (MFC-free interfaces; return sci::Result)
  ScriptCatalog   list, selectors, names, shadow check
  CompileBatch    compile 1..N scripts to a chosen destination
  DecompileRun    prepare src\, names, batch, stale scripts, statistics
                          |
Src\Core\                 GameSession (CResourceMap, options, data folder, log sink)
                          Result.h (sci::Error, sci::Result, the exception boundary)
                          |
Engine                    parser, compiler, decompiler, resource containers, codecs
```

- One `GameSession` is one open game. The CLI creates one for each run.
  The GUI's `AppState` owns one and forwards `GetResourceMap()` and
  `GetVersion()` to it, so the GUI code does not change. `LogInfo()` goes
  to `CoreLog`, and the GUI's `AppState` is the log sink.
- The engine and the services never use `appState`. A unit test runs every
  script operation with `appState == nullptr` to keep it so.
- Every service returns `sci::Result<T>` or `sci::Status`. No exception
  leaves a service (section 6).
- All CLI logic is in the library. The unit tests call it in-process with
  `RunCli(args, console)`. The exe calls only `RunCli` (and `AfxWinInit`
  while it still links MFC).

### 3.2 `GameSession` and the core log

New folder `Src\Core\`. The interface uses no MFC type:

```cpp
enum class LogLevel { Info, Warning, Error };

class ILogSink
{
public:
    virtual ~ILogSink() = default;
    virtual void Write(LogLevel level, const std::string &text) = 0;
};

// Engine code logs through CoreLog. The host installs the sink: the GUI
// (AppState) sends it to the log file from its command line, the CLI to
// stderr, a test to a list. A sink must be thread-safe.
ILogSink *SetCoreLogSink(ILogSink *sink);   // returns the sink before it
void CoreLog(LogLevel level, const std::string &text);
void CoreLogFormat(LogLevel level, const char *format, ...);  // for old LogInfo sites
class ScopedCoreLogSink;                    // installs a sink for a scope

struct SessionOptions
{
    std::string dataFolder;             // include\ and Decompiler\; default: the exe folder
    bool warnOnUnusedInstances = true;  // the GUI default (added in B3a, where the compiler reads it)
};

class IClassHints;                      // optional compile error hints; the class browser implements it (B3a)

class GameSession
{
public:
    // The GUI passes its app services and resource recency; the CLI neither.
    GameSession(const SessionOptions &options = {}, ISCIAppServices *appServices = nullptr,
        ResourceRecency *resourceRecency = nullptr);
    sci::Status Open(const std::string &gameFolder);   // no exception, no dialog
    CResourceMap &ResourceMap();
    const GameFolderHelper &Helper() const;
    const SCIVersion &Version() const;
    const SessionOptions &Options() const;
    IClassHints *ClassHints() const;    // null in the CLI (B3a)
};
```

B1 built the log and the session without a log sink parameter: the host
installs the one global sink (`ScopedCoreLogSink` in the CLI and the tests;
`AppState` in the GUI).

Rules:

- The log is for information. A failure is an `Error` value, not a log line.
- `SafeMessageBox` with no GUI sends its text to `CoreLog`, with no length
  limit.
- `GameSession::Open` uses a new `CResourceMap::TryOpen(folder)`, which
  returns a `Status`. `SetGameFolder` keeps its GUI behaviour and throws as
  before.
- The engine gets the session (or the parts it needs: the resource map, the
  helper, the options) as parameters. The engine uses only two
  process-wide settings: the log sink, and the text codepage
  (`SetTextCodepage`, which the game open sets from `game.ini`, B3a).
- The data folder comes from the session. `scic` has a `--data-dir` option.
  The tests stop using `SetIncludeFolderForTest`.

### 3.3 Service interfaces

The sketches show the shape. The names can change.

```cpp
// Src\Resources\ScriptCatalog.h
enum class NameSource { GameIni, Derived, Default };
struct ScriptRow
{
    uint16_t number;
    std::string name;          // the name that list and decompile use
    NameSource source;
    std::string derivedName;   // for --derived
    std::string location;      // "resource.000", "110.scr (patch)" or "" (not compiled)
    bool hasSource, hasObjectFile;
};
sci::Result<std::vector<ScriptRow>> ListScripts(GameSession &session, bool alwaysDerive);
// All bad selectors are in one Usage error.
sci::Result<std::vector<ScriptId>> ResolveScriptSelectors(GameSession &session,
    const std::vector<std::string> &selectors, SelectorMode mode);
sci::Result<std::vector<std::string>> FindShadowingPatches(const GameFolderHelper &helper,
    const std::vector<ResourceKey> &resources);

// Src\Compile\CompileBatch.h
struct CompileOptions
{
    ResourceSaveLocation saveTo;   // Patch or Package from the CLI; Default (game.ini) from the GUI
    bool dryRun = false;
    bool failFast = false;
    bool replaceShadowingPatches = false;
};
class CompileBatch
{
public:
    // Fails if the batch cannot start: tables do not load, or the shadow check refuses.
    static sci::Result<CompileBatch> Start(GameSession &session,
        std::vector<ScriptId> scripts, const CompileOptions &options);
    bool Step(const std::atomic<bool> &abort, ICompileEvents &events);  // next script; false when done
    CompileReport Finish();            // tables, one commit; statuses in the report
};
// The CLI's one-call form (Start, Step until done, Finish).
sci::Result<CompileReport> CompileScripts(GameSession &session, std::vector<ScriptId> scripts,
    const CompileOptions &options, const std::atomic<bool> &abort, ICompileEvents &events);

// Src\Compile\DecompileRun.h
enum class NameAssignment { Missing, All, None };
struct DecompileRunOptions
{
    DecompileOptions engine;       // the existing debug and asm options
    NameAssignment names = NameAssignment::Missing;
    bool updateStale = false;
    bool writeFiles = true;        // false for --stdout and --dry-run
};
sci::Result<DecompileReport> RunDecompile(GameSession &session, const std::set<uint16_t> &scripts,
    const DecompileRunOptions &options, IDecompilerResults &results,
    IDecompileOutput *output = nullptr);   // output: a callback instead of files
```

- `CompileBatch::Step` compiles one script. The GUI dialog calls it once for
  each window message, as now, so the dialog stays responsive. The CLI calls
  `CompileScripts`.
- Progress, diagnostics and cancel use plain callbacks and `std::atomic`.
  There is no `HWND` and no `PostMessage`.
- `CompileReport` and `DecompileReport` are in section 6.5.

### 3.4 Script names without `game.ini`

The CLI must work on a game folder that SCI Companion never opened
(requirement R9b). So the script names come from a script-name map, not
from `game.ini` alone. The session builds the map when it opens the game.
For each script number, the first source that has a name wins:

1. `game.ini` `[Script]`, if the file exists (SCI Companion projects).
2. A `src\*.sc` file that declares `(script# N)`: the file title. This
   covers source trees from other decompilers and projects with no
   `game.ini`. Two files for one number is a usage error that names both.
3. A `src\*.sco` file: the script number is in its header.
4. A name derived from the compiled script, with the decompiler's rule
   (Main, the first class, the first public instance, a `_N` suffix in
   number order; section 2.4). Only `list` and `decompile` need it, so the
   session derives these names only when asked.
5. `nNNN`.

Rules:

- `GameFolderHelper` gets an optional `std::shared_ptr<const ScriptNameMap>`.
  When it is set, `GetScriptFileName(n)`, `GetScriptObjectFileName(n)`,
  `FigureOutName(Script, n)`, `SaveSCOFile` and the compiler's number-to-name
  map use it. When it is not set (the GUI), they read `game.ini` as before.
- The CLI never creates `game.ini` unless you give `--game-ini create` to
  `script decompile`. When `game.ini` exists, `decompile` adds the names of
  the scripts it writes that have no entry, so the GUI finds them.
- The names are stable from run to run: a file that `decompile` wrote is
  found again by rule 2 on the next run.

### 3.5 Program name and folder

- Name: `scic.exe`. Project: `SCICompanionCli\SCICompanionCli.vcxproj`.
- Output: `$(SolutionDir)Release\scic.exe`, next to `SCICompanion.exe` and
  the data folders.
- At start, `scic` makes sure that `<data folder>\include\sci.sh` exists. If
  it does not, `scic` stops with exit code 3 and a clear message.
- The data folder is the exe folder by default. `--data-dir <folder>` or the
  environment variable `SCIC_DATA_DIR` changes it.

## 4. Command line

### 4.1 Synopsis

```
scic script list      <game-folder> [script ...] [options]
scic script decompile <game-folder> (<script> ... | --all) [options]
scic script compile   <game-folder> (<script> ... | --all) [options]
scic script sco       <game-folder> (<script> ... | --all) [options]
scic help [group [command]]
scic --version
```

- `script` is the first command group. Later groups use the same pattern
  (section 13): for example `scic resource extract`, `scic package rebuild`.
- `scic script` with no command, and `scic help script`, list the script
  commands.

Common options (all commands):

| Option | Meaning |
|---|---|
| `-q`, `--quiet` | Show errors only. |
| `-v`, `--verbose` | Show more detail: each file written, memory use, warnings for each function. |
| `--log <file>` | Also write all messages to a file. |
| `--data-dir <folder>` | The folder that holds `include\` and `Decompiler\`. Default: the exe folder. |
| `--dry-run` | Do the work in memory. Write nothing. Show what a real run would write. |

### 4.2 Script selectors

All three script commands use the same selectors:

| Form | Example | Meaning |
|---|---|---|
| number | `110` | script 110 |
| range | `100-199` | each script in the range that exists |
| name | `rm110`, `Main` | a name from the script-name map (section 3.4), case-insensitive: `game.ini`, `src\`, or for `list` and `decompile` a derived name. |
| path | `src\rm110.sc` | `compile` only. The file must be in `<game>\src`. |
| `--all` | | every script |

Rules:

- `decompile` and `compile` need `--all` or one or more selectors. If you
  give neither, or both, `scic` stops with a usage error (exit 2).
- `list` with no selector lists all scripts.
- `scic` removes duplicates (`0` and `Main` are one script). A duplicate in
  one write batch corrupts the map (P6).
- An unknown selector is a usage error. `scic` finds all of them before any
  work starts, and the message tells you to run `scic script list`.
- `compile` rejects header files (`.sh`, `.shm`, `.shp`).
- Order: `compile` uses the `[Script]` order of `game.ini` when the file
  exists, the same as the GUI (see Q4), then the other scripts in number
  order. `decompile` uses the script numbers in ascending order, as the
  batch does now.

### 4.3 `scic script list`

Purpose: show the number and the name of each script. `list` only reads.

Default output:

```
 No.  Name        Name from  In game          src  sco
   0  Main        game.ini   resource.000     yes  yes
  13  AboutCode   source     13.scr (patch)   yes  yes
  26  rm26        sco        resource.000     -    yes
 110  rm110       derived    resource.000     -    -
 255  Controls    game.ini   (not compiled)   yes  yes
 994  n994        default    resource.000     -    -
```

- Rows: each script resource in the game, plus each script that has only a
  source file, a `.sco` file or a `[Script]` entry ("not compiled"). The
  rows are in number order.
- "Name from" is the rule of section 3.4 that gave the name:
  - `game.ini`: the `[Script]` section has the name.
  - `source`: a `src\*.sc` file declares `(script# N)`.
  - `sco`: a `src\*.sco` file has the number.
  - `derived`: `scic` derives the name from the compiled script with the
    decompiler's rule (Main, first class, first public instance).
    `scic script decompile` writes the script under this name.
  - `default`: no rule gives a name, or a file already uses the default
    name. The name is `nNNN`.
- `list` and `decompile` use one library function for this rule, so they
  always agree.
- To derive names, `list` loads the compiled scripts
  (`GlobalCompiledScriptLookups`). This is the "some decompilation" case: it
  reads the object tables, not the code. `list` does it only if one or more
  scripts have no name from rules 1 to 3, or if you give `--derived`.
- `list` works the same with or without `game.ini`.
- A script that cannot be read is still listed. Its row shows the error
  (for example `(unreadable: truncated heap)`), and `list` exits with 6.
- "In game" shows where the resource is: a package volume or a patch file.
  "src" and "sco" show if `src\<name>.sc` and `src\<name>.sco` exist.
- Options:
  - `--format text|tsv`: `tsv` prints tab-separated columns with a header
    row, for use in scripts. JSON comes later (section 13).
  - `--derived`: add a column with the derived name, also for scripts that
    have a name from rules 1 to 3. It shows where the names differ.
- `list` writes nothing: no `game.ini`, no `src\` folder, no `Decompiler.ini`.

### 4.4 `scic script decompile`

```
scic script decompile C:\Games\SQ3 --all
scic script decompile C:\Games\SQ3 0 255 rm110
scic script decompile C:\Games\SQ3 110 --stdout > rm110.sc
```

Steps:

1. Open the game. Resolve the selectors.
2. Prepare `src\`. Create it if necessary. If `Decompiler.ini` is missing,
   copy `<data folder>\Decompiler\*` into it. Never overwrite a file here.
3. Resolve the names of all scripts with the script-name map, deriving the
   missing ones (section 3.4). All scripts need names first, because
   `(use Name)` refers to them.
4. Run the batch on the selected numbers. Existing `.sc` files are
   overwritten, as in the GUI.
5. Write the names into `game.ini` as `--game-ini` says.
6. For an individual run, find the stale scripts and report them. With
   `--update-stale`, decompile them too. Repeat until there are no new
   stale scripts.
7. Print the report: each failed script with its error, then the totals
   (scripts written and failed, function and byte success rates, asm
   fallbacks, globals renamed).

Individual and bulk:

| | Individual (`110 rm120 ...`) | Bulk (`--all`) |
|---|---|---|
| Scripts | the selection | each script resource (patch file or package) |
| Global names | found across the selection, plus the names already in `Main.sco` | found across the whole game |
| Other scripts | can still use an old global name ("stale"). `scic` reports them, or decompiles them with `--update-stale`. | none are stale |
| Names | resolved for all scripts first, because `(use Name)` needs them | the same |
| `Main.sco` | updated when a global gets a name | the same |

Options:

| Option | Meaning |
|---|---|
| `--game-ini update\|create\|none` | `update` (default): when `game.ini` exists, add a `[Script]` entry for each written script that has none, so the GUI finds the files. `create`: the same, and create `game.ini` when it is missing. `none`: never write `game.ini`. Without `game.ini`, the names come back on the next run from the files in `src\` (section 3.4). |
| `--reset-names` | Use the derived name for every script, also for scripts that have a name (the dialog's "Reset filenames"). The old files keep their old names, and a warning lists them. |
| `--update-stale` | After an individual run, also decompile the stale scripts. |
| `--stdout` | One script only. Print the source to stdout and write nothing: no `.sc`, `.sco`, `game.ini` or `src\`. The `(use ...)` lines use the names of section 3.4, as a file run does. |
| `--text-tuples` | Replace text resource tuples with strings (a dialog option). |
| `--asm-only` | Disassemble only (the dialog's "Disassemble only"). |
| `--debug-control-flow`, `--debug-instructions`, `--debug-filter <name>` | Decompiler debug output (dialog options). |

Exit codes: section 8.

### 4.5 `scic script compile`

```
scic script compile C:\Games\MyGame --all
scic script compile C:\Games\MyGame rm110
scic script compile . 0 rm110 --to package --replace-patches
scic script compile . --all --dry-run
```

Steps:

1. Open the game. Resolve the selectors to source files.
2. Start the batch: load the tables and headers, and, for the package, run
   the shadow check. If the batch cannot start, stop before any compile
   (exit 3 or 8).
3. Compile each script in order. Each script gets its own log (P11) and its
   own status in the report.
4. If one or more scripts compiled, save vocab 996 and 997. The tables then
   hold the new selectors.
5. Commit all resource writes in one batch.
6. Print the diagnostics as they occur, then the report: each failed script
   with its error, what went where, and the totals.

Individual and bulk:

| | Individual | Bulk (`--all`) |
|---|---|---|
| Scripts | the selection | every script that has a source file: the `[Script]` entries of `game.ini` (when it exists) and every `src\*.sc` that declares `(script# N)` |
| Order | `game.ini` order, then number order | the same |
| No `game.ini` | works: the names come from `src\` (section 3.4) | the same |
| A script with a name but no `.sc` file | usage error (exit 2) | skipped, and listed as a warning |
| Passes | one | repeat while a `.sco` file changed, at most `--passes` times (section 14) |
| Tables (996, 997) | saved once at the end, if one or more scripts compiled | the same |
| Resource writes | one batch | one batch, from the last pass |
| After an error | continue with the next script; `--fail-fast` stops | the same |

Why the passes: `(use X)` reads `X.sco` from disk, and scripts have `use`
cycles (`Main` and the system scripts). When a script's interface changes,
the scripts that use it are right only after a second pass. A pass that
changes no `.sco` file ends the loop, so a project with consistent `.sco`
files (for example after `script decompile --all`) needs one pass.

For one script, the table rule is the same as the GUI (save only on
success). For many scripts, the GUI saves the tables even when all scripts
failed. `scic` does not. This difference has no effect on the game.

A script that is not in `[Script]` compiles, but `scic` warns: other
scripts cannot find its classes by number. Add the entry with the GUI, or
later with `compile --register` (section 13).

Options:

| Option | Meaning |
|---|---|
| `--to patch\|package` | Where to write the resources (section 5). Default: `patch`. `package` is the resource package (`resource.map` and a volume). `--into-volume` is another spelling of `--to package`. |
| `--replace-patches` | With `--to package`: after the package write succeeds, move the patch files that would hide the new resources to `<game>\replaced-patches\<time>\`. |
| `--out-dir <folder>` | Write the patch files into this folder, not into the game folder. The game's resources do not change; `src\*.sco` still does. Use it to build a set of patch files to ship. Not with `--to package`. |
| `--raw` | With `--out-dir`: write the plain resource data with no patch header, as `script.110.bin` and `heap.110.bin`. |
| `--passes <n>` | With `--all`: the largest number of passes (default 5). `--passes 1` is one pass, as in the GUI. |
| `--fail-fast` | Stop at the first script with errors. |
| `--no-warn-unused` | Turn off the "unused instance" warning. It is on by default, as in the GUI. |
| `--dry-run` | Compile, but write no resource, table, `.sco` or `.scd`. Later scripts in the same run then read the old `.sco` of earlier scripts. |

Diagnostics use the MSBuild format. Visual Studio and the VS Code
`$msCompile` problem matcher can then go to the line:

```
I:\Games\MyGame\src\rm110.sc(12,5): error : Undeclared identifier 'foo'.
I:\Games\MyGame\src\rm110.sc(40,1): warning : Unused instance 'bar'.
```

Exit codes: section 8.

### 4.6 `scic script sco`

```
scic script sco C:\Games\LSL2 --all
scic script sco C:\Games\LSL2 rm26 rm38
```

Purpose: make the `.sco` object files from source that another tool wrote
(for example sluicebox's sci-tools), so that `script compile` can resolve
each `(use ...)`. Our own decompiler writes `.sco` files, but another
decompiler does not, and `compile --all` cannot start from nothing, because
every script uses another one.

- For each selected script, `sco` parses `src\<name>.sc`, loads the game's
  compiled script, and writes `src\<name>.sco` from the two, with the same
  code that the decompiler uses (`SCOFromScriptAndCompiledScript`, with the
  PR K2 fix).
- It does not compile and does not change a resource.
- A script with no source file or no compiled resource is skipped and
  listed. A parse error is a failure of that script (exit 6).
- Then run `scic script compile --all`.

## 5. Where compiled output goes

Two terms:

- Read view: where `scic` reads resources. It is always the game's own
  mode (`SaveToPatchFiles`). `--to` never changes it.
- Destination: where `scic script compile` writes.

| `--to` | Package-mode game (`SaveToPatchFiles` false or missing) | Patch-mode game (`SaveToPatchFiles=true`) |
|---|---|---|
| `patch` (default) | patch files. They override the package copies, in the game and in SCI Companion. | patch files |
| `package` | the package, after the shadow check | refused (`WriteRefused`): in this mode SCI Companion hides the package |

Rules:

- One compile run writes everything to one destination: script, heap, auto
  text, vocab 996 and vocab 997.
- `scic` never changes `SaveToPatchFiles`.
- Patch files go into the game folder with the standard names (section 2.6).
- Shadow check for `--to package`: before the compile, `scic` looks for a
  patch file for each resource that it can write. These are script N, heap
  N (SCI1.1), text N, vocab 996 and vocab 997. A patch file would hide the
  new package copy (section 2.6). If `scic` finds one, the batch does not
  start (`WriteRefused`), and the error lists the files, unless you give
  `--replace-patches`. The batch checks the queued writes again before the
  commit.
- If a patch file with a non-standard name (for example `001.scr`) exists
  for a resource, `scic` warns. SCI Companion can load either file.
- Patch files in a package-mode game: the new `996.voc` and `997.voc` hide
  later package saves of those tables by the GUI. `scic` prints this
  warning once. The GUI gets the same shadow check (PR S2): before it saves
  to the package, it asks to move the hiding patch files aside.
- For every destination, `.sco` goes to `src\` and `.scd` goes to `debug\`.
- One batch for each run. In the package, this is one rewrite of the volume
  and the map for all scripts. For patch files, `scic` writes all the `.bak`
  files first, then does all the renames (PR A2).

## 6. Failure handling

### 6.1 Should the old failure handling be replaced?

Yes, at every boundary that the CLI and the services use. It gives:

- one reliable exit code, from structured errors instead of lost messages;
- a report of partial success after a compile or decompile;
- no failure that disappears in a `catch (...)`, a closed log file or an
  ignored return value (section 2.9);
- no exception that ends the process, because the exception boundary
  (section 6.3) turns it into an `Error`;
- the same error text in the CLI and in the GUI.

This plan does not rewrite the whole engine. Some deep code keeps its
exceptions inside an exception boundary, where a change gives no gain
(section 6.4).
This is the same policy as the .NET rewrite plan (section 5.22): known
failures are values, and exceptions are for bugs.

### 6.2 The model

There are three kinds of outcome:

1. **Diagnostics.** Messages about the user's input: compile errors and
   warnings, decompiler warnings, asm fallbacks. They are data, not failures
   of the call. A sink gets them as they occur, and the report keeps them
   for each script.
2. **Errors.** An operation cannot produce its result: a file is missing, a
   read or write fails, a resource is corrupt, the version is not
   supported, a write is refused, the user cancels. The function returns
   the error as a value: `Result<T>`, or `Status` when there is no value.
3. **Bugs.** A broken invariant. `assert` in Debug. In Release, the
   exception boundary turns an escaped exception into an `Internal` error for the
   current script, and the run continues with the next script.

A compile with errors is a normal outcome. The script's status is
`Error{Compile, "rm110.sc: 3 errors"}`, and the three errors are in its
diagnostics.

Types (`Src\Core\Result.h`):

```cpp
namespace sci
{
    enum class ErrorCode
    {
        // The same families as the rewrite plan's SciError.Code:
        Format,        // corrupt or truncated data
        Unsupported,   // SCI version or feature not supported
        NotFound,      // file, resource or script missing
        Io,            // a read or write failed
        Compile,       // the script has compile errors (details in the diagnostics)
        WriteRefused,  // a patch file would hide the write; a patch-mode game
        // Added for C++:
        Usage,         // a bad selector or option
        Cancelled,     // Ctrl+C or the Cancel button
        Internal,      // a bug: an unexpected exception or a broken invariant
    };

    struct ErrorLocation
    {
        std::string file;                          // a game file or a source file
        std::string resource;                      // for example "script 110" or "heap 110"
        int64_t offset = -1;
        int line = 0, column = 0;
    };

    struct Error
    {
        ErrorCode code;
        std::string message;                       // one line, for people
        ErrorLocation where;
        std::vector<std::string> context;          // outermost last: "writing 110.hep", "script 110 (rm110)"
    };

    template<typename T> using Result = tl::expected<T, Error>;
    using Status = Result<void>;
}
```

Helpers:

| Helper | Purpose |
|---|---|
| `sci::Fail(code, message, where)` | Make the error value (`tl::unexpected<Error>`). |
| `SCI_TRY(expr)` | Return early with the error if `expr` failed. |
| `SCI_TRY_ASSIGN(var, expr)` | The same, and put the value into `var`. |
| `WithContext(result, text)` | Add a context line on the error path only. |
| `Guard(context, fn)` | The exception boundary (section 6.3). |
| `FromHResult(hr, what)`, `FromLastError(what)` | Convert old codes at a boundary. |

Rules:

- A discarded `Result` is a build error. tl::expected v1.2.0 and later
  mark the class `[[nodiscard]]` in C++17 (it reads `_MSVC_LANG`, so no
  `/Zc:__cplusplus` is needed). PR F1 adds `/we4834` to our projects (not
  to Prof-UIS). `build-warn.log` (2026-09-14) shows no C4834 warning today,
  and F1 confirms this with a full rebuild.
- Do not call `.value()`. It throws `tl::bad_expected_access` (MSVC defines
  `_CPPUNWIND` under `/EHsc`). Use `SCI_TRY`, `SCI_TRY_ASSIGN`, or test the
  result before you use `*r`.
- `Result.h` defines `TL_ASSERT` before the include, so a wrong access (`*r`
  on an error) throws `sci::InvariantViolation` in Release too. The
  exception boundary then reports it as `Internal`. Without this, a wrong access is
  undefined behaviour in Release.
- One error type (`sci::Error`) everywhere. This also avoids tl::expected
  issue #132 (implicit conversions between error types in `and_then`).
- Use the `std::expected` names only: `and_then`, `or_else`, `transform`,
  `transform_error`. Do not use tl's `map` and `map_error`. A later move to
  C++23 `std::expected` is then a rename.
- Include `Result.h` through the precompiled header, before any
  `#define new DEBUG_NEW`. The MFC macro breaks the placement `new` in the
  library header.
- The CLI prints an error as `scic: error: <context>: <message> [<code>]`,
  and a diagnostic in the MSBuild format (section 4.5).

### 6.3 The exception boundary

`Guard(context, fn)` runs `fn`. If an exception escapes, `Guard` returns an
`Error` in its place:

| Exception | `ErrorCode` |
|---|---|
| `sci::DataError` (new; PR F2 puts it in place of `std::exception("…")`) | its own code, usually `Format` |
| `std::bad_alloc` | `Internal` ("out of memory") |
| `sci::InvariantViolation`, `tl::bad_expected_access`, any other `std::exception` | `Internal` |
| MFC `CException*` (the text from `GetErrorMessage`, then `Delete()`) | `Io` for `CFileException`, else `Internal` |
| anything else (`...`) | `Internal` |

- A `Guard` sits at each public service entry and around each item of a
  batch (each script). `RunCli` has one more `Guard` around the whole
  command (exit 1).
- The MFC case is in one `.cpp` file (a rethrow-and-catch helper), so
  `Result.h` includes no MFC header. Phase E can then drop the MFC case.
- Access violations (SEH), stack overflow and `abort()` are not C++
  exceptions. The exception boundary cannot catch them (section 6.6).

### 6.4 Where exceptions stay

These stay as they are, inside an exception boundary:

- The decompiler's control-flow exceptions (38 throw sites). They mean "give
  up on this function and fall back to asm", inside one function's
  decompile, and they never leave the decompiler. To convert them would
  thread results through the structuring code (`ControlFlowGraph.cpp`, 2930
  lines; `DecompilerNew.cpp`, 3771 lines), with a risk to the golden
  snapshots and no gain for callers.
- The resource component readers, which use the stream's throw mode
  (`ResourceEntity.cpp:77`). The boundary is `CreateResourceFromResourceData`,
  which gets a `Result` form in PR F2. The GUI can still use the
  default-resource fallback, but as an explicit choice.
- Third-party code that throws (cpptoml, CppFormat, the STL). The exception
  boundary contains it.

These go away on the script paths (PR F2): the Microsoft-only
`std::exception("…")` throws, the silent `catch (...)` blocks, and the
message boxes (PRs A2 and B1).

### 6.5 Partial success: batch reports

```cpp
struct ScriptOutcome
{
    uint16_t number;
    std::string name;
    sci::Status status;                     // ok, or why this script failed
    std::vector<CompileResult> diagnostics;
    std::vector<WrittenFile> written;       // resources and files for this script
};

struct CompileReport
{
    std::vector<ScriptOutcome> scripts;
    sci::Status tables;                     // saving vocab 996 and 997
    sci::Status commit;                     // the one package or patch write
    bool cancelled = false;
};

struct DecompileReport
{
    std::vector<ScriptOutcome> scripts;
    std::vector<std::pair<std::string, std::string>> globalRenames;
    std::set<uint16_t> stale;               // for an individual run
    DecompileStats stats;                   // functions, bytes, asm fallbacks
    bool cancelled = false;
};
```

- The outer `Result` fails only when the batch cannot start: the selection
  is invalid, the game cannot be read, the tables do not load, or the
  shadow check refuses.
- Each script has its own `Status`. One failure does not stop the others
  (unless `--fail-fast`).
- The `commit` status covers the resource writes of all scripts. Each
  destination (the package, the patch files, the audio cache) is written
  as one unit: the patch writer writes every `.bak` file and checks every
  target before it renames any. A failed destination does not undo another
  destination that was written, and a rename that fails after the checks
  (for example, another program locks the file at that moment) can leave
  a mix. The report names what failed. The `.sco` files were already
  written; the report says so.
- The same report drives the GUI output pane, the CLI summary, the exit
  code (section 8) and, later, `--json`.

### 6.6 Crashes

A `Result` cannot catch a crash. So that a crash never waits for a click:

- `RunCli` calls `SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)`
  and `_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)`.
  Windows then shows no crash dialog.
- An unhandled-exception filter prints one line, for example
  `scic: crash 0xC0000005 while compiling script 110 (rm110)`, and exits
  with 1. A thread-local "current item" gives the context.
- The ASan and `/analyze` CI legs continue to find these bugs.

### 6.7 Library choice

| | tl::expected (recommended) | Boost.Outcome / standalone Outcome | Own `Result<T>` | C++23 `std::expected` |
|---|---|---|---|---|
| Licence | CC0 1.0 (`COPYING`) | BSL 1.0 (Boost); Apache 2.0 or BSL 1.0 (standalone) | ours | part of the STL |
| Size | one header, 2475 lines, standard headers only | single-header edition about 395 KB (QuickCppLib inside); the Boost edition needs other Boost libraries | about 200 lines | none |
| Standard | C++11 or later; CI on MSVC 2015 to 2022 | C++14 or later; the docs say "only from Visual Studio 2022 onwards are almost all corner case problems fixed" | C++17 | C++23; needs a new language switch for the whole project |
| `[[nodiscard]]` | on the class since v1.2.0 | yes | we add it | yes |
| Composition | `and_then`, `or_else`, `transform`, `transform_error` (and `map`, `map_error`) | TRY macros (`OUTCOME_TRY`); `TRYX` is GCC and clang only | we write it | `and_then`, `or_else`, `transform`, `transform_error` |
| Error type | ours | `std::error_code` by default | ours | ours |
| Learning cost | low: the `std::expected` API | the FAQ says "a steeper learning curve and more complex mental model than when programming with Expected" | low | low |
| Later move to `std::expected` | a rename | a rewrite | a rewrite | not applicable |

Recommendation: vendor tl::expected v1.3.1 (released 2025-09-01; master
has no newer commit) at `Src\tl-expected\tl\expected.hpp`, with its
`COPYING` file, and add the notice to `SCICompanion\Files\Licenses`.
Outcome's extra features (interop with `std::error_code`, carrying an
`exception_ptr`) do not help here: the project has one error type, and the
exception boundary turns exceptions into errors. To move the whole project to C++23
for `std::expected` would be a separate project with its own risk.

Known tl::expected issues to respect: #180 (a leak with converting
constructors between types with resource-allocating default constructors):
construct the target type directly, and do not convert between `Result`
types of different value types. #148 (`expected<void,E>::operator*`): use
`Status` with `SCI_TRY`, not `*`.

### 6.8 Scope

- `Result` at every boundary that the CLI and the services use: game open,
  script text load, compile, decompile, the catalog, the resource reads
  that the services use, and all writes (resources, `.sco`, `.scd`, `.sc`,
  and the names in `game.ini`).
- The GUI calls the same services. It shows the same `Error` text in its
  message boxes and output pane. Failures that the GUI swallows today, for
  example in the decompile worker, become visible.
- Other code moves to `Result` when a later change touches it, for example
  the resource commands of section 13.
- A CI check (`UnitTests\Tools\CheckFailureHandling.ps1`, in the static
  analysis job) fails on a new `catch (...)` with an empty body, a new
  `throw std::exception(`, and a new `AfxMessageBox` in `Src\Core`,
  `Src\Compile` or `Src\Resources`. Since B3b, it also fails on a new use
  of `appState` in those folders and in `Src\Util`. An allowlist holds
  today's sites, so only new ones fail.

## 7. The CLI host

`main` (in `SCICompanionCli\main.cpp`):

1. Call `AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0)`
   while `scic` links the MFC library. Phase E removes this call.
2. `return RunCli(argc, argv, console);`

`RunCli` (in the library):

1. Turn off the crash dialogs and install the crash filter (section 6.6).
2. Parse the arguments. A usage error exits with 2 before `scic` opens the game.
3. Make sure that `<data folder>\include\sci.sh` exists (exit 3 if not).
4. Install a console log sink (`SetCoreLogSink`): warnings go to stderr,
   and all messages go to the `--log` file if you give one.
5. Create a `GameSession` with the options (data folder, warn on unused
   instances). There is no `AppState`: `appState` stays null. The session
   calls `InitializeSyntaxParsers()`, which the `AppState` constructor did
   before; without it the parser grammars are empty.
6. `session.Open(folder)`. If it returns an error, print it and exit with 3.
7. Run the command inside `Guard`. Print the report.
8. Map the result to an exit code (section 8). Destroy the session.

Ctrl+C: a `SetConsoleCtrlHandler` handler sets an `std::atomic<bool>`.

- `compile` stops after the current script. It commits the finished scripts
  and the tables, as the GUI's Cancel button does. The report has
  `cancelled = true`.
- `decompile`: `IsAborted()` returns true, and the batch stops at its next
  check. The files already written stay. The report has `cancelled = true`.
- A second Ctrl+C ends the process at once.

Streams: results go to stdout (the `list` table, the `--stdout` source).
Progress, diagnostics, errors and the report summary go to stderr.

## 8. Exit codes

The numbers are the same as in the rewrite plan's `scic` (Appendix A1,
section 5.3, which section 5.22 uses), plus code 9.

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Internal error (a bug, or an escaped exception) |
| 2 | Usage error: bad option, unknown script, header file given to `compile` |
| 3 | Cannot open the game or start the batch, or the data folder is missing |
| 5 | Compile errors |
| 6 | Partial failure: some scripts failed for a reason that is not a compile error |
| 7 | Cancelled |
| 8 | Write refused (`WriteRefused`) |
| 9 | Write failed (`Io` during a write) |

The mapping is one function in `Src\Cli\`, with a unit test for each case:

- An error before the first script: `Usage` gives 2, `WriteRefused` gives
  8, `Internal` gives 1, and every other code gives 3.
- A report: take the highest code that applies, in this order:
  1 (an `Internal` status) > 9 (the commit, the tables or a file write
  failed) > 8 > 7 (cancelled) > 5 (a script has compile errors) > 6 (a
  script failed for another reason) > 0.

## 9. Work plan

Each row is one PR. The PRs are stacked and atomic. Each PR has a test that
fails before the change and passes after it (AGENTS.md). The GUI behaves the
same unless a row says otherwise. Sizes: S is less than 1 day, M is 1 to 3
days, L is 3 to 5 days.

### Phase F: failure-handling foundation

| PR | Change | Test (negative check) | Size |
|---|---|---|---|
| F1 | Vendor tl::expected v1.3.1 (`Src\tl-expected\`, `COPYING`, notice in `SCICompanion\Files\Licenses`). `Src\Core\Result.h`: `ErrorCode`, `ErrorLocation`, `Error`, `Result`, `Status`, `Fail`, `SCI_TRY`, `SCI_TRY_ASSIGN`, `WithContext`, `FromHResult`, `FromLastError`, `InvariantViolation`, `TL_ASSERT`. `Src\Core\Result.cpp`: the exception boundary's mapping (section 6.3). Include through the precompiled header. `/we4834` for our projects. The CI check script and its allowlist (section 6.8). | `Guard` maps each exception kind to its code (`DataError`, `bad_alloc`, `std::exception`, `CException*`, `CFileException*`, `...`). `SCI_TRY` returns early. A wrong access throws `InvariantViolation` in Release (fails before `TL_ASSERT` is defined). | M |

### Phase A: safety fixes

| PR | Change | Fixes | Test (negative check) | Size |
|---|---|---|---|---|
| A1 | `DeferResourceAppend`: `Commit` returns a `Status` with the first write error. A nested batch keeps the outer queue (the destructor abandons only a batch that was not committed). An abandoned inner batch withdraws what it queued and puts back the queued copies that it replaced, so S2 can drop one script's partial output. Names are assigned only for resources that were written. A resource queued twice replaces the earlier copy. The batch shows its queued resources, for the final shadow check. New `WriteResource(const ResourceBlob&)` returns a `Status` and shows no UI; `AppendResource` stays as the GUI form, which shows the error. The audio repackage stops, and keeps the old audio volumes, when its audio maps cannot be saved. | P3, P4, P5, P6 | A nested batch writes all its resources (fails before). An abandoned inner batch writes nothing (fails before). A commit to a read-only map or volume returns an `Io` error and changes neither file (fails before: `S_OK`). A failed audio map save keeps `resource.aud` (fails before). Both templates. | S |
| A2 | Patch writer: check the size and the result of `SaveToHandle`. On failure, delete the `.bak` files and return the error. Write all `.bak` files, then check that every existing target can be replaced (not read-only, not locked), then do the renames; if a rename still fails, delete the `.bak` files that are left (A2 review). `CheckResourceSize` (in place of `ValidateResourceSize`) returns a `Status` that quotes the limit of the game's format, and shows no dialog. New `WriteResource(const ResourceEntity&)` returns a `Status` (`Cancelled` when the entity's own checks fail); `AppendResource` for an entity stays as the GUI form, which shows the error text. The audio cache writer lets its errors reach the caller and saves its audio map through `WriteResource` (from the A1 review). `DataError` can carry a whole `Error`, for code inside an exception boundary that cannot return a `Result`. | P7, P9 | An oversize resource saved to a patch file: the old file stays and an error comes back (fails before). A batch with one oversize patch file replaces none of its patch files (fails before). A headless oversize entity save returns `Unsupported` and opens no window. A failed save of the audio cache's map returns `Io` (fails before: success). | S |

### Phase B: decouple the script engine from `AppState` and CrystalEdit

| PR | Change | Fixes | Test (negative check) | Size |
|---|---|---|---|---|
| B1 | `Src\Core\`: `GameSession`, `ILogSink`, `CoreLog`, `SessionOptions` (section 3.2). `CResourceMap::TryOpen` returns a `Status`. `AppState` owns a `GameSession` and forwards `GetResourceMap` and `GetVersion` to it; `LogInfo` goes to `CoreLog`, and `AppState` is the GUI's sink. The session calls `InitializeSyntaxParsers()`. `SafeMessageBox` with no GUI goes to `CoreLog`. The codecs use `CoreLog`. `SessionOptions::dataFolder` sets the resource map's include and decompiler folders (`SetDataFolder` replaces `SetIncludeFolderForTest`). Fix the `_fTrackHeaderFiles` order, and the dangling reference in `DependencyTracker`. | P8, P12, P17 | `GameSession::Open` opens both templates with `appState == nullptr`. `Open` on a folder with no `resource.map` returns `NotFound` that names the file (fails before: `SetGameFolder` throws a `CUserException` with no text). A garbage map still opens, because the format detection is permissive (F2). A headless `SafeMessageBox` text reaches the sink in full (fails before). A codec failure logs with no `AppState` (fails before: a null dereference). | M |
| B2 | Script text with no CrystalEdit: `ReadOnlyTextBuffer(const ScriptText &)` and `CScriptStreamLimiter(const ScriptText &)`, `LoadScriptText(path)` returning `Result<ScriptText>` and `SplitScriptText(contents)` with the line-ending rule of section 2.8 (`Src\Util\ScriptText.h`), and a small `TextPos` in place of `CPoint` in the stream. The engine call sites use it: the compile, `SimpleCompile`, the header loads (`CompileContext.cpp:1204`) and `DecompilerConfig`. The editor keeps its buffer. | P20 | For every `.sc` and `.sh` file in both templates, and for crafted files (LF only, CR only, mixed, no final line break), the lines from both loaders are equal. A naive splitter fails the mixed case (the negative check). A missing file gives `NotFound`. | S |
| B3a | The compile path takes the session: `CompileTables::Load` and `Save` take the resource map, `CompileResults` takes the version, `CompileContext` and `GenerateScriptResource` take the session, and `NewCompileScript` and `SimpleCompile` move to `Src\Compile\CompileScript.cpp` (`SimpleCompile` takes the version or the defines). Also the vocab 996/997 tables and the class table (`Vocab99x.cpp`, which logs through `CoreLog`), the text codepage (`Text.cpp`: a process-wide setting that the game open sets from `game.ini`), the polygon folder (`Script::SetPolyFolder`, read by `SCISyntaxParser.cpp`), `ValidateSaid` and `ExtractScriptStrings`. The class browser becomes an optional `IClassHints` on the session, and takes its own lock. `SessionOptions::warnOnUnusedInstances`: `AppState::GetSession` copies the GUI setting into it. The GUI callers clear the dependency tracker after a compile. | P18 (compile) | Compile all of both templates with `appState == nullptr` (fails when one compile site reads `appState`: a null dereference). The codepage comes from `game.ini` with no `AppState` (fails when the open does not set it). The existing golden suites (bytecode oracle, decompile snapshots) do not change. | M |
| B3b | The decompile path takes the resource map: `DecompileBatch`, `DecompileScript` and `FixDuplicateObjectNames` (moved out of `ScriptDocument.cpp` to `Src\Compile\DecompileScript.cpp`; the 3-argument `DecompileScript` goes), and `CreateDecompilerConfig` (it reads `sci.sh` and `keys.sh` from the include folder of the data folder). `ConvertToSCISyntaxHelper` gets an overload that loads its class lookups from a helper; with no lookups, the formatter no longer loads them through `appState`. `DecompilerFallback` reads the version from its lookups. The engine's `LogInfo` calls (`DecompilerFallback`, `Disassembler`, `PaletteOperations`, `Sound`, `View`) go to `CoreLog`. A check-script rule, `appstate-in-engine`: no `appState` in `Src\Core`, `Src\Compile`, `Src\Resources` or `Src\Util`, with an allowlist for the audio-cache sites, the GUI flags of the pic code, and the GUI files of `Src\Util`. | P15, P18 (decompile), P19 | Decompile all of both templates with `appState == nullptr`, in a batch and one script at a time with the asm output (fails when one decompile site reads `appState`: a null dereference). The decompiler reads `sci.sh` from the data folder (B3b review; the real-game tools now resolve enum names, as the GUI does). The check script fails on a new `appState` in an engine folder. The in-repo golden suites do not change. | M |
| F2 | Engine errors as values at the boundary. `sci::DataError` (standard C++, with an `ErrorCode`) replaces the 46 `throw std::exception("…")` that were left (`Format` for bad data, `Unsupported` for a size limit or an unknown format, `Internal` for a misuse, `Io` for a short file read; `deletefile` uses `ThrowWin32`). `Result` forms of the reads that the services use: `TryCreateResourceFromResourceData`, `CompiledScript::TryLoad` (its streams are in throw mode, and it loads the SCI1.1 heap itself), `GlobalCompiledScriptLookups::TryLoad`, `CompileTables::TryLoad`, with `CheckResourceData` (a corrupt header or a failed decompression is `Format`) and `CheckVocabTables`; each error names the resource in its location (`DescribeResource`). The old `bool` forms stay for the GUI. The silent swallows on the script paths go: `TextReadFrom` in `Text.cpp` (a partial read is a `Format` error), the decompile worker (`DecompileDialog.cpp`: it reports the error in the results until S4), and the two version probes (`VersionDetectionHelper.cpp`: a failed probe keeps the default and logs why). | P21 | A truncated script resource gives `Format` with the resource number and the stream's text (fails before: `false`, or zeros read past the end). A text resource with no final NUL gives a `Format` error (fails before: a silent partial read). A failed decompression gives `Format` (fails before: a log line). A missing class table gives `NotFound` for vocab 996. A real-game probe: `TryLoad` and `Load` agree on the 5,913 scripts of 31 GOG game folders, and their 2,193 text resources all load. After the F2 review, they also agree on every script of both templates (fails before: SCI1.1 script 990). A damaged heap gives "heap N", a failed table load names its table, and a map entry or data past the end of the volume gives `Format` (fails before: "script N", one text for three tables, and an empty or unread blob with no flag). The golden suites do not change. | M |

### Phase K: compiler fixes found in `scicompile` (section 14)

Each fix makes the compiler agree with Sierra's compiler or with the game's
own data. Each fix is one PR with a test that fails before it. (K1 pins a
fix that the branch base already had: its tests fail with the old code put
back, not on the commit before K1.)

| PR | Change | Test (negative check) | Size |
|---|---|---|---|
| K1 | `and` and `or` in a value position give the deciding operand, as Sierra's `sc` does (`MakeAnd`/`MakeOr` in `COMPILE.CPP`): `a; bnt E; b; E:` for `and`, `bt` for `or`. Found at K1: commit `f5f7a01b` (2026-09-12, in the branch base) already does this; the plan's check missed its path above the old code (`f5f7a01b` guarded the path with `LangSyntaxSCI`, and `b6b892d9`, 2026-09-14, made that guard `if (true)`). K1 removes the unreachable old code (`_WriteFakeIfStatement`: an if with `ldi 1` and no else, so 1 or 0) and the `WeakSyntaxNode` that only it used, and pins the bytes. No GUI change in K1: the change of compiled output came with `f5f7a01b`. A nest of the other operator (`(or (and a b) c)`) gives Sierra's value but not Sierra's bytes (older than K1; section 14). | The bytes of `(= t (and a b))` and `(= t (or a b))` equal the Sierra shape written in `asm` (fails with the old path: `ldi 1` and no else). The same in a call argument, `(Abs (and a b))`: both exits join before the `push` (fails with a join after the push). The bytes of `(if (and a b) ... else ...)` and `(if (or a b) ... else ...)` equal the branch-to-the-else shape written in `asm` (the condition path does not change; fails with an `or` condition that goes through the value path). With the old path put back, 8 unit tests fail, the template recompiles among them. | S |
| K2 | `SCOFromScriptAndCompiledScript`: when the source has a `(public name N ...)` block, record those slots as they are. Only without that block, pair the definition order with the export table. | A script whose public procedures are defined out of slot order gives each name its own slot (fails before: the names shift). | S |
| K3 | Class numbering: in each script, order the species of `vocab.996` as the classes are in the game's compiled script (the species in each class header), not in number order; the table's other species for the script follow, in number order. A script that does not load keeps the old order. A compiled class whose species the table gives another script (a leftover class) is left out, so it keeps the old positional numbering (61 scripts in 30 GOG game folders; a known gap). The scripts are found in one pass (`CompiledScript::TryLoad` from blobs): a lookup for each script cost 300 to 700 ms on big games. | A table and a compiled script whose class order differs from the number order give each class its own species (fails before: two classes swap). An opt-in test over real games (`SCICOMP_SPECIES_GAME`): every script without a leftover class keeps each class's species in 30 GOG game folders (fails before: LB2 script 0 swaps two classes; The Colonel's Bequest script 999 shifts five). | M |
| K4 | `#` inside a selector name (not first): the parser (`SelectorP`) accepts it, and the formatter keeps it in property names and send selectors. KQ6 names selector 879 `dungeon#`. | `(properties dungeon# 0)` and `(self dungeon#:)` parse, and the formatter writes `dungeon#` (fails before: a parse error, and `dungeon_`). | S |
| K5 | A call to `proc<N>_<M>` that nothing resolves compiles to `calle N M`, with a warning, only if the game has no script N (a script that Sierra removed, as KQ6's 911). With a script N it stays an error. Also fix the parse of `__proc<N>_<M>`, which searched the whole name for `_` and gave script 0. | `proc911_0` in a copy with no script 911 gives `calle 911 0` and a warning (fails before: an error). `proc0_99` stays an error. `__proc911_0` gives `calle 911 0` (fails before: `calle 0 11`). | S |
| K6 | A `Said` string in a game with no `vocab.000` gives a compile error that names the missing resource (fails before: a null dereference in `LookupWord`). `ScriptId` splits a path on `\` and on `/`, so `src/rm110.sc` works. | Both cases. | S |

### Phase S: services

| PR | Change | Fixes | Test (negative check) | Size |
|---|---|---|---|---|
| S1 | Compile destination: `CompileWriteOptions { ResourceSaveLocation saveTo; std::string outDir; bool raw; bool writeResources, writeObjectFile, writeDebugInfo; }`. With `outDir`, the patch files (or with `raw` the plain data) go to that folder through `ResourceBlob::SaveToFile`. The script, heap and text writes and `CompileTables::Save(saveTo)` use it. `GameFolderHelper::GetSaveSourceFlags(location)` resolves `Default`. The GUI passes `Default`. The `.sco`, `.scd` and `.sc` writes return a `Status`. | P2, P10 | Copies of the SCI0 and SCI1.1 templates: `Patch` writes `script.NNN`, or `NNN.scr` and `NNN.hep`, and `resource.map` stays byte-equal (fails before: the output goes into the package). `Package` in a patch-mode copy writes the package. A `.sco` write into a read-only `src\` returns `Io` (fails before: silent). | M |
| S2 | `CompileBatch` and `CompileScripts` (section 3.3), returning `CompileReport` (section 6.5): one log for each script, the table rule, one deferred commit, an abort flag, the shadow check, `Guard` around each script, the passes of section 4.5 (write a `.sco` only when its bytes change; the commit holds the last pass), the `--all` list from the script-name map (not `GetAllScripts`), and a skip of named scripts with no source file. `CNewCompileDialog` and `OnCompile` use it. `CalculateErrors` counts again from zero. `CompileResult` gets the raw message. All lines are 1-based. GUI change: before a package save that a patch file would hide, the GUI asks to move the patch files aside. | P1 (compile), P11, P13 | Compile all scripts of both templates with 0 errors. With one broken script, the others compile and are written, and the report shows one `Compile` status. A script that throws inside the engine gives an `Internal` status, and the batch goes on (fault injection). The error counts are exact (fails before). A parser error gives the source line (fails before for the 0-based sites). The shadow check finds `997.voc`. | M |
| S3 | `ScriptCatalog` and the script-name map (sections 3.3, 3.4): `ScriptNameMap` built from `game.ini` (optional), `src\*.sc`, `src\*.sco`, derived names and `nNNN`; `GameFolderHelper` uses it when it is set (`GetScriptFileName(n)`, `GetScriptObjectFileName(n)`, `FigureOutName(Script, n)`, `SaveSCOFile`, the compiler's number-to-name map); `GameSession` builds and installs it. Also `SuggestScriptNames` (pure, in number order), `ListScripts`, `ResolveScriptSelectors`, `FindShadowingPatches`, all returning `Result`. `DecompileDialog::_AssignFilenames` uses `SuggestScriptNames`. GUI change: the `_N` suffix for a duplicate name follows the script number. | P14, P22 | The name rules: Main, "Game" first, first class, public instance, and a `_N` suffix that follows the number (fails before: hash order). The rule order of section 3.4 (a `.sc` name beats a derived name; `game.ini` beats both). A template copy with no `game.ini` gives the same file names from `src\` (fails before: `nNNN`). Two `.sc` files for one number is a `Usage` error. Selector cases: number, range, name, path, duplicate, header, unknown (all bad selectors in one `Usage` error). An unreadable script gives a row with its error. | M |
| S4 | `DecompileRun` (section 3.3), returning `DecompileReport`: the names of section 3.4 with `--reset-names`, `WriteScriptNamesToGameIni(update\|create\|none)`, `PrepareDecompileFolder` (plain file copy, no shell), the batch with `Guard` around each script, the stale-script loop, the statistics, an output sink (files or a callback). Also `GenerateObjectFiles(session, scripts)` for `script sco` (section 4.6). `DecompileDialog` uses it. Remove the leftover `theApp` (B3b removed the 3-argument `DecompileScript`). | P1 (decompile), P16 | The `update`, `create` and `none` modes of `--game-ini`, and `--reset-names`. With no `game.ini`, nothing creates it (fails before: the dialog's naming writes it). The folder preparation copies once and never overwrites. The callback sink writes no file. `--update-stale` stops when no script is stale. A script that fails gives a status in the report, and the others are written. | M |

### Phase C: the CLI

| PR | Change | Test | Size |
|---|---|---|---|
| C1 | The `SCICompanionCli` project: `scic.exe`, console subsystem, static MFC for now, Release\|Win32, references to the library and Prof-UIS, `.sln` rows, a VERSIONINFO `.rc`. `Src\Cli\`: the command groups and arguments (vendored CLI11, BSD-3 licence, notice in `SCICompanion\Files\Licenses`), console, the exit-code mapping (section 8), the host (section 7) with the crash settings, Ctrl+C. Commands: `help`, `--version`, `script list`. | In-process `RunCli`: `script list` on both templates (text and tsv), usage errors (exit 2), a bad folder (exit 3), a missing data folder (exit 3), and `list` writes nothing (the folder snapshot stays equal). A unit test for each row of the exit-code mapping. Integration: `scic.exe script list` through `IntegrationHarness::RunChildReadStdout` (`UnitTests\IntegrationHarness.h:126`). | M |
| C2 | `scic script decompile` (section 4.4) and `scic script sco` (section 4.6). | A template copy with no `game.ini` and no `src\` (a game SCI Companion never opened): `decompile --all` writes the derived names and creates no `game.ini`; a second run finds the same names. Individual and `--all` runs on template copies. `--stdout` and `--dry-run` write nothing. The stale report and `--update-stale`. Exit 6 when one script fails (a truncated script in a copy). | M |
| C3 | `scic script compile` (sections 4.5 and 5). | `compile --all` on a template copy with no `game.ini` compiles every `src\*.sc`. The default writes patch files and leaves `resource.map` byte-equal. `--to package` writes the package. The script bytes are equal for both destinations, and equal to the GUI path (S2). The shadow refusal (exit 8) and `--replace-patches`. The patch-mode refusal. `--dry-run` writes nothing. Exit 5 with one broken script, and the others are written. Round trip: `decompile --all`, then `compile --all`, with 0 errors (as `RecompileAllDecompiledScripts`, `UnitTests\DecompileHelper.cpp:544-593`). | M |
| C4 | CI and documents: a smoke step in `build.yaml` (copy `Release\TemplateGame\SCI1.1` to a temp folder, then run `script list`, `script decompile --all` and `script compile --all`). README "What's new": a "Command-line tool" item. AGENTS.md: the CLI build and tests, the third `.rc` file for the version, and the failure-handling rules (section 6.2). `UnitTests\README.md`. `UnitTests\Tools\CliCorpusSweep.ps1` (local use). | CI passes. | S |

### Phase E (optional, after phase C): a core library with no MFC GUI headers

| PR | Change | Size |
|---|---|---|
| E1 | New static library `SCICompanionCore`: `Src\Core`, the engine files of `Src\Compile` and `Src\Resources`, the codecs and streams of `Src\Util`, `cpptoml`, `CppFormat`, `CRC32`, `tl-expected`. Its precompiled header has Windows, ATL (`atlstr.h`, `atltypes.h` for `CString`, `CPoint`, `CSize`) and the STL: no `afxwin.h`, no Prof-UIS, no GDI+. Replace `ASSERT`, `VERIFY`, `TRACE` and `DEBUG_NEW`. Move the UI parts to the GUI library: the `ResourceMap.cpp` save dialogs, the `ResourceMapOperations.cpp` prompts, the `Vocab000.cpp` editor prompts, and the GUI helpers in `util.cpp`. Drop the MFC case from the exception boundary. `SCICompanionLib` (GUI) links Core. `scic.exe` links Core only and drops `AfxWinInit`. | L |

- The build then enforces the layers: a core file that includes an MFC GUI
  header does not compile.
- The inventory in section 2.8 sizes the work: about 150 mechanical edits,
  plus the moves.
- Phase C does not need phase E. Phase E makes the exe smaller and the
  layering permanent.

Dependencies: F1 is first; every later PR uses it. A1, A2, B1 and B2 need
only F1. B3a needs B1 and B2, and B3b needs B3a. F2 needs B3b. The K PRs
need F2 (they change code that B3 and F2 move) and do not depend on each
other. S3 needs the K PRs, because every later service uses the
script-name map. S1 needs S3. S2 needs S1, S3 and A1. S4 needs S3 and K2.
C1 needs S3. C2 needs C1 and S4. C3 needs C1, S2 and A2. C4 is last. E1
comes after C4. For the stacked-PR workflow, one straight order works:
F1, A1, A2, B1, B2, B3a, B3b, F2, K1, K2, K3, K4, K5, K6, S3, S1, S2, S4,
C1, C2, C3, C4.

GUI changes in this plan (all others are refactors with no visible change):

- A2: the "resource too big" message comes from the returned error, not
  from a message box inside the resource map. It quotes the limit of the
  game's format (before, always the SCI0 limit).
- A2: a failed save into the audio cache shows the real error. Before, a
  failed save of the cache's audio map showed a message box and then
  reported success, and other failures in that writer were silent.
- A1 and A2: a failed save reports its error and keeps the old files
  (the audio repackage keeps the old audio volumes; a batch of patch
  files replaces none of them when one cannot be written or replaced).
- F2 and S4: the Decompile dialog shows failures that it swallowed before.
- F2: a text resource whose last string has no NUL opens as a default
  resource marked "Resource load failed" (before: the strings read so
  far).
- F2 review: a resource whose header or data is not in its volume shows
  "Corrupt" in the status column of the resource list (before: an empty
  resource, or bytes that were not read from the volume).
- S2: the GUI asks before a package save that a patch file would hide.
- S2: compile-all saves the tables only if one or more scripts compiled.
- S3: the `_N` suffix of a duplicate automatic script name follows the
  script number.
- `f5f7a01b` (in the branch base; K1 pins it): `(and a b)` and `(or a b)`
  in a value position compile to Sierra's code, so their value is the
  deciding operand, not 1 or 0. A fan script that used the number 1 from
  such an expression gets a different number. The README "What's new"
  says so (added by the K1 review fixes).
- K3: a recompiled Sierra script keeps each class's own species.
- K4: the decompiler writes selector names with `#` as they are.
- K5: a call into a script that the game does not have compiles, with a
  warning.

## 10. Testing

- Unit tests (the default run: in-process, no child process):
  - Each library PR has its negative check (section 9).
  - A guard test runs `script list`, `decompile --all` and `compile --all`
    on both templates with `appState == nullptr`. It fails if any engine or
    service code uses `appState` again.
  - Exception-boundary tests: each exception kind maps to its code, and a batch goes
    on after one item throws (fault injection through a test hook).
  - Failure tests: a truncated script, a truncated heap, a read-only `src\`,
    a read-only volume, and a locked patch file. Each gives the right
    `ErrorCode`, the right item in the report, and the right exit code.
  - The CLI tests call `RunCli` with a console that captures the output.
    Each test first copies a template game to a temp folder (the `SetUpGame`
    pattern in `UnitTests\Helper.cpp`).
  - A "writes nothing" test compares a snapshot of the game folder (names,
    sizes, hashes) before and after the run.
  - Register each new test `.cpp` in `UnitTests.vcxproj` and
    `UnitTests.vcxproj.filters`.
- "Never opened" scenario: a copy of each template with `game.ini` deleted,
  and for `decompile` also `src\` deleted, must pass `script list`,
  `script decompile --all` and `script compile --all` from end to end, and
  no run may create `game.ini`.
- Golden suites: the bytecode oracle and the decompile snapshots must not
  change in phases F and B. They prove that the refactor keeps the output.
- Integration tests (the class name contains `Integration`; run with
  `-Integration`): start `Release\scic.exe` with
  `IntegrationHarness::RunChildReadStdout`. Check the exit code, stdout,
  stderr and the files on disk. Use a timeout, so a hidden modal box or
  crash dialog shows as a failed test and not as a stuck CI job.
- Parity: compile each template script through the GUI path (`CompileBatch`
  with `Default`) and through `scic`. The script and heap bytes must be equal.
- Round trip: `decompile --all`, then `compile --all`. Expect 0 errors on
  both templates.
- GUI smoke check for phases F, B and S (manual, for each PR that touches a
  dialog): compile one script, compile all, decompile some scripts, and
  decompile all in the template games. Also open a game with a truncated
  script and check that the error text appears.
- Corpus sweep (local, optional, not in CI): `CliCorpusSweep.ps1` copies each
  game from `F:\Games\Sierra` and `F:\games\gog` to a temp folder. It runs
  `script list`, `script decompile --all` and `script compile --all`. It
  writes a CSV with exit codes, error codes, error counts and times. It
  never writes into the source game folders. Copy the games; do not use
  junctions, because `decompile` writes `src\` and `game.ini`.
- CI: the build job already builds and ships `scic.exe`. C4 adds the smoke
  step. The integration job runs the new integration tests. The ASan job
  builds only `UnitTests`, so the in-process CLI tests get ASan coverage
  with no change. The static-analysis job runs the failure-handling check.

## 11. Decisions

Your answers:

| # | Question | Decision |
|---|---|---|
| Q1 | Call the exe `scic.exe`? | Yes. The exit codes and `--into-volume` match the rewrite plan's CLI. |
| Q2 | Default destination for `compile` | Patch files. `--to patch` and `--to package` select one explicitly. |
| Q3 | Refuse to overwrite `.sc` files on `decompile`? | No. `decompile` overwrites, as in the GUI. |
| — | Command structure | Command groups: `scic script <command>`. |
| — | Refactoring | Allowed, to decouple from MFC and the GUI (phases B and E). |
| — | Failure handling | Failures are values (`Result`), with partial-success reports (section 6). |
| — | `game.ini` | The CLI works on a game that SCI Companion never opened; it does not rely on `game.ini` (section 3.4). |

Still open (the plan uses the recommendation unless you say otherwise):

| # | Question | Recommendation |
|---|---|---|
| Q4 | Compile order | `game.ini` order in version 1, as in the GUI. An order that compiles a used script first comes later. |
| Q5 | Names for scripts that have none | The derived name (section 3.4), the same in `list` and `decompile`. When `game.ini` exists, `decompile` adds the missing entries (`--game-ini update`); it creates the file only with `--game-ini create`. |
| Q6 | JSON output in version 1 | No. `list --format tsv` in version 1; `--json` for all commands later. |
| Q7 | GUI and CLI on the same game at the same time | Version 1: not supported, and the documents say so. Later: a shared lock file. |
| Q8 | Argument parser | Vendor CLI11 (one header file, BSD-3 licence). It supports command groups. The other choice is a small parser written by hand. |
| Q9 | Phase E (core library split) | After phase C, as a separate stack. |
| Q10 | The rewrite plan's CLI uses `scic compile` (verb first). | Change it to the `scic script compile` form, so scripts carry over to the .NET tool. |
| Q11 | Result library | tl::expected v1.3.1, using only the `std::expected` names (section 6.7). |
| Q12 | `f5f7a01b` (which K1 pins) changes compiled output for fan scripts that used the 1 from an `and`/`or` value. | Adopt Sierra's semantics (the project's direction: Sierra syntax and Sierra-compatible output), with a README note. |
| Q13 | The scope of the `calle` fallback for `proc<N>_<M>` (K5) | Only when the game has no script N, with a warning. `scicompile` applies it to every unresolved name; a typo then compiles into a call to a missing export. |

## 12. Risks

| Risk | Mitigation |
|---|---|
| The phase B or F2 refactor changes the output. | The golden suites (bytecode oracle, decompile snapshots) must stay equal. B3 and F2 change plumbing only. |
| The refactor breaks a GUI path that has no test. | The manual GUI smoke check (section 10). `AppState` forwards to the session, so most GUI code does not change. |
| A modal box or crash dialog on a rare path blocks the CLI. | PRs A2 and B1, no `AppState` in the CLI, the crash settings (section 6.6), and integration tests with a timeout. |
| A new code path ignores a `Result`. | `[[nodiscard]]` and `/we4834` make it a build error. |
| New code swallows exceptions again. | The CI failure-handling check (section 6.8). |
| tl::expected has open issues (#180, #148). | Pin v1.3.1, one error type, the rules in section 6.7, and tests of our uses. |
| Patch files hide later package saves (mixed destinations). | The shadow check in the CLI and in the GUI (S2), the refusal in patch mode, and the warning in section 5. |
| The static library pulls GUI objects into `scic.exe`, so the exe is larger. | Accepted until phase E. The tests already link the same way. |
| The `.sco`, `.scd` and `.sc` writes are not atomic. | S1 returns their errors. Atomic writes come later. |
| Paths with non-ASCII characters fail (MBCS build). | The documents say so. The GUI has the same limit. |
| A long refactor stack conflicts with other work in the repo. | Short-lived PRs, merged in order. B3 was the largest, so it is split: B3a (the compile path) and B3b (the decompile path). |

## 13. Later: other resource operations

The same pattern extends the CLI: move the driver out of the dialog onto the
session, return `Result`, then add a command group.

| Command | Existing code | Coupling to remove |
|---|---|---|
| `scic game info` | `SniffSCIVersion`, `GameFolderHelper` | none |
| `scic resource list [--type <type>]` | `CResourceMap::Resources` | none |
| `scic resource extract` | `ExtractAllResources` (`Src\Util\ExtractAll.cpp`), `ResourceBlob::SaveToFile` | progress UI; 11 `appState` uses |
| `scic resource import` | `ResourceBlob` from a file, `MatchesResourceFilenameFormat`, `AppendResource` | the `AppendResourceAskForNumber` dialogs |
| `scic resource delete` | `DeleteResource` (`Src\Resources\ResourceMapOperations.cpp`) | message boxes (`:180-221`) |
| `scic package rebuild` | `PurgeUnnecessaryResources` (`MainFrm.cpp:1979`), `RebuildResources`, `PostRepackage.cmd` | It is in `MainFrm.cpp`. In patch mode it builds only from patch files, so it drops package-only resources. |
| `scic script disassemble` | `DisassembleScript` (`Src\Compile\Disassembler.cpp`) | `ShowTextFile` must become stdout |
| `scic pic render`, `scic view export` | image code, GDI+ | needs `GdiplusStartup`; the pic and sound validators open dialogs (`Src\Resources\Pic.cpp:1152`, `Src\Resources\Sound.cpp:1821`) |
| `scic message export`, `scic message import` | message resources, `.shm` headers | |
| `scic vocab ...` | `Vocab000` | editor prompts (`Vocab000.cpp:47, 390, 452`) |

Other later items:

- `--json` and `--json-lines` for all commands, from the reports.
- `script compile --order deps`: compile a used script before its users. Use
  `TarjanAlgorithm` for cycles.
- `script compile --changed`: use file times. The `DependencyTracker` is
  empty in a CLI.
- `script compile --register`: add a missing `[Script]` entry for a file
  given by path.
- `--out-dir` for patch output outside the game folder (`ResourceBlob::SaveToFile`).
- `script decompile --from package|patch`: choose the copy to decompile.
- A shared lock file for the GUI and the CLI.
- Move the patch file masks (`g_szResourceSpecByType`,
  `ResourceNumberFromFileName`) out of `MainFrm.cpp` into `ResourceUtil.cpp`.
- Convert the other `HRESULT` and `bool` engine functions to `Result` as
  their areas change.

## 14. Prior art: lucasartsifier's `scicompile`

`https://github.com/katiahayati/lucasartsifier/tree/main/tools/scicompile`
(GPL v2, last change 2026-08-24) is a headless Linux build of an older
upstream SCI Companion compiler. It builds with CMake and GCC through header
shims (`compat\`) and patched copies of 19 library files (`patched\`); it
does not change the vendored tree. Its CLI:

```
scicompile <game> <input.sc> <output.bin>   one script to a file (+ <output.bin>.hep on SCI1.1)
scicompile --all <game>                      compile all; writes .sco files, repeats to a fixed point
scicompile --sco <game>                      .sco files from source + the game's compiled scripts
  --version sci0|sci1|sci11, --wide-exports  overrides (it cannot detect them)
```

It found these problems. The table shows how each one applies to this code
(checked on `0dc1fef5`):

| Their change | This code | Action |
|---|---|---|
| `a[i] op= v` with a non-literal index stored to `a[0]` or `a[1]` (`eq?; toss; pprev`) | already fixed: `push0; eq?; ldi 0; or; pprev; sa?i` (`Compile.cpp:2089-2098`) | none |
| `(and a b)` / `(or a b)` in a value position gave 1 or 0 | already fixed by `f5f7a01b`, before this plan (the first check missed its path, which `b6b892d9` had put behind an `if (true)`; found at K1). A nest of the other operator, such as `(or (and a b) c)`, gives Sierra's value but not Sierra's bytes: SCI Companion takes the inner `bnt` straight to the next operand of the `or`, and Sierra's optimizer does not take a `bnt` through a `bt` (`a; bnt O1; b; O1: bt O; c; O:`). This is older than K1; only a byte-exact round trip of such a nest sees it. Sierra's `sc` 4.100 (`MakeAnd`: "the expression evaluates to its value") and the `sc` learnings table agree with `scicompile`. | K1: remove the old code, pin the bytes |
| `.sco` from source: a class missing from the compiled script dereferenced null | already fixed (#60) | none |
| `.sco` from source: a procedure export past the source's public procedures read past the end | already fixed (#60) | none |
| `.sco` from source: the public procedure names paired by definition order, not by the `(public name N)` slots (KQ5 `Interface.sc`) | the same bug (`SCO.cpp:730-780`); the decompiler uses this function too | K2 |
| Class numbering: species in number order, not in the script's class order (LB2 script 0) | the same bug (`SpeciesTable::_Create`, `Vocab99x.cpp:1152-1168`) | K3 |
| `#` inside a selector name (KQ6 `dungeon#`) | the same parser limit (`SCISyntaxParser.cpp:45-66`), and the formatter writes `dungeon_` (`CleanTokenSCI`) | K4 |
| An unresolved `proc<N>_<M>` compiles to `calle N M` (KQ6 speedRoom calls stripped script 911) | the decompiler writes `proc<N>_<M>` (`DecompilerCore.cpp:97`), and the compile fails; the `__proc` prefix parse is also broken (`CompileContext.cpp:679`) | K5, narrowed (Q13) |
| A null `vocab.000` guard in `LookupWord` | the same crash (`CompileContext.cpp:777-783`) | K6, as a clear error |
| `ScriptId` split a path only on `\` | the same (`util.cpp:761`) | K6 |
| The host must call `InitializeSyntaxParsers()` | the `AppState` constructor calls it; `GameSession` must | B1 |
| `--sco` mode | missing | `script sco` (C2, S4) |
| Compile-all to a fixed point; skip `[Script]` rows with no source | one pass, in the GUI | S2 and `--passes` |
| Output to a file, not into the game | missing | `--out-dir`, `--raw` (S1, C3) |
| `--version`, `--wide-exports` | detected by `SniffSCIVersion` and `_DetectIsExportWide` | none |
| A missing file gives empty data instead of an exception (`ScopedFile`, `streamOwner`) | not adopted: it hides the failure; the `Result` model reports it (section 6) | none |
| GCC portability (shims, `-I-`, `_Mynode()`, `lower_bound`, rvalue-to-reference bindings, `std::exception(const char*)`) | not needed on MSVC; F2 removes `std::exception(const char*)` | none now |

## Appendix A: host checklist

Must:

- While `scic` links MFC: call `AfxWinInit` first.
- Turn off the crash dialogs before any other work.
- Install the core log sink before the session opens the game.
- Create a `GameSession`. Never create `AppState` in the CLI.
- Run each command inside `Guard`. Map the result with the one exit-code
  function.
- Use `DeferResourceAppend` (inside `CompileBatch`) and commit it. Its
  destructor otherwise throws the queued resources away.
- Destroy the session before exit.

Must not:

- Call `AppState` code, `GenerateBrowseInfo`, `RunGame`,
  `CompileABunchOfScripts`, the compile or decompile dialogs, `OnCompile`,
  `AppendResourceAskForNumber` or `DeleteResource`.
- Call `.value()` on a `Result`.

## Appendix B: key references

| Topic | Location |
|---|---|
| Compile one script (GUI) | `Src\MFCDocuments\ScriptDocument.cpp:123-199` |
| Compile a script (engine) | `Src\Compile\CompileScript.cpp` (`NewCompileScript`) |
| Compile all (dialog) | `Src\Dialogs\NewCompileDialog.cpp`, `Src\MFCFrames\MainFrm.cpp:2075-2139` |
| Compile tables | `Src\Compile\CompileContext.cpp:82-94`; `Src\Resources\Vocab99x.cpp:747-802, 1086-1101` |
| Decompile batch | `Src\Compile\DecompileBatch.h`, `.cpp:547-702` |
| Decompile dialog steps | `Src\Dialogs\DecompileDialog.cpp:121-139, 646-705, 727-780, 803-887` |
| Save location | `Src\Resources\GameFolderHelper.cpp:296-322` |
| Deferred writes | `Src\Resources\ResourceMap.h:197-229`; `ResourceMap.cpp:263-347` |
| Resource append | `Src\Resources\ResourceMap.cpp:496-630` |
| Patch writer and names | `Src\Resources\PatchResourceSource.cpp:155-174`; `Src\Resources\ResourceUtil.cpp:62-88` |
| Open a game | `Src\Resources\ResourceMap.cpp:1266-1302`; `Src\Resources\VersionDetectionHelper.cpp:586` |
| Parser input | `Src\Util\CrystalScriptStream.h`, `.cpp:34-64`; `Src\CrystalEdit\CCrystalTextBuffer.cpp:289-397` |
| Stream error mode | `Src\Util\Stream.h:86-133`; `Src\Resources\ResourceEntity.cpp:77` |
| Decompiler exception containment | `Src\Compile\ControlFlowGraph.cpp:2862`; `Src\Compile\DecompilerNew.cpp:3734, 3755` |
| Silent swallows | `TextReadFrom` in `Src\Resources\Text.cpp`; `Src\Dialogs\DecompileDialog.cpp:866`; `Src\Resources\VersionDetectionHelper.cpp:902, 925` |
| Precompiled header | `SCICompanionLib\stdafx.h` |
| Headless test set-up | `UnitTests\Helper.cpp:53-128`; `UnitTests\DecompileHelper.cpp:132-222, 544-593` |
| Rewrite plan: CLI names and exit codes; error policy | The .NET rewrite plan (outside this repository), Appendix A1 section 5.3; section 5.22 |
| tl::expected | `https://github.com/TartanLlama/expected` (v1.3.1, CC0 1.0) |
| lucasartsifier `scicompile` | `https://github.com/katiahayati/lucasartsifier/tree/main/tools/scicompile` (GPL v2): `main.cpp`, `BUILD_NOTES.md`, `COMPILE_ALL_NOTES.md`, `patched\` |
| Sierra `sc` 4.100 source (MIT) | `https://github.com/Digital-Alchemy-Studios/da-sci-compiler-pub`, `COMPILE.CPP` (`MakeAnd`, `MakeOr`) |
