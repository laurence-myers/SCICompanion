# SCICompanion
SCI Companion - a complete IDE for Sierra SCI games (SCI0 to SCI1.1)

Official website:
http://scicompanion.com

General notes:
The bulk of the code is in SCICompanionLib\Src. The engine (the compiler,
the decompiler and the resource formats) builds into SCICompanionCore, a
library with no MFC; the GUI builds into SCICompanionLib.

SCICompanion is the .exe which is just a thin wrapper over SCICompanionLib
and SCICompanionCore. scic.exe, the command-line tool, links only
SCICompanionCore.

## Building

SCI Companion builds with **Visual Studio 2022** and the **v143** platform
toolset. You need:

* Visual Studio 2022 with the **Desktop development with C++** workload,
* the **MFC** component (the app and the GUI library are MFC),
* the **Windows 10 SDK** (10.0.26100 or later), and
* **vcpkg**: the vcpkg component of Visual Studio, or a vcpkg folder in the
  `VCPKG_ROOT` environment variable. The first build downloads the
  libraries of `vcpkg.json` (tl::expected and CLI11).

Open `SCICompanion.sln` and build the **Release / Win32** configuration, or
build from a command prompt:

```
MSBuild.exe SCICompanion.sln -m -p:Configuration=Release -p:Platform=Win32
```

The unit tests live in the `UnitTests` project. Run them with
`UnitTests\RunTests.ps1` after building. The build also makes the
command-line tool, `Release\scic.exe`; `scic help` shows its commands.

## Licence

SCI Companion is licensed under the GNU General Public License, version 2 or
(at your option) any later version; see the root `LICENSE` file. The notices
for the third-party components it uses are under
`SCICompanion/Files/Licenses` and are distributed with the program.

## What's new in 4.0.0

This release focuses on the compiler and decompiler, on stability, and on
modernizing the build. Broad highlights since the previous release:

* **A command-line tool, `scic.exe`.** It comes next to `SCICompanion.exe`,
  and it works also on a game that SCI Companion never opened (with no
  `game.ini`). `scic script list <game folder>` shows each script of a game:
  its number, its name and where the name comes from, where the game keeps
  it, and whether its source and `.sco` files exist. `scic script decompile`
  decompiles scripts as the Decompile dialog does, or prints one script's
  source (`--stdout`). `scic script sco` makes the `.sco` files from source
  that another tool wrote. `scic script compile` compiles scripts as one
  batch, into patch files (the default), the package or another folder,
  and prints each error in the format that Visual Studio and VS Code can
  open. `scic help` shows the commands, `--dry-run` writes nothing, and
  the exit code tells a build script what happened. It refuses a game whose
  `resource.map` is damaged or empty, and names the volume files when they
  are missing. A compile of named scripts prints each error when its script
  is done, and a decompile warning names its script. `--function-report`
  writes a line for each decompiled function: whether it became source or
  `asm`, and why the decompiler could not do it. A console shows names
  with their own characters. It has none of the GUI code in it: the engine
  is now a library of its own, with no MFC.
* **A new decompiler engine.** The decompiler now reads the control flow of
  each function in the order that the compiler wrote it (a scope parser), and
  follows the values forwards through it, instead of matching shapes in a
  graph. On a sample of 92 game copies, 17 functions fall back to `asm`
  (343 with the old engine). Its tests compile the decompiled text again
  and check that each function does the same as the original bytecode (the
  same calls, stores and tests, with the same values).
* **Decompiled text that compiles to the same game.** Over a library of 93
  game copies, the decompiled text is compiled again and each function is
  compared with the original. More of it now compiles, and means the same:
  classes keep their species (also a copy of a class in another script),
  objects with the name of a property, a keyword or a class, and two
  classes with one name, get another name and keep their name string, a
  class with no superclass keeps the order of its properties, a class whose
  properties are not those of its superclass keeps them (with the new
  `&layout`, see "Language extensions"), an object gets the name of its
  `name` property also when that is not its first property, selectors
  and kernels with no name of their own read back as their numbers,
  `&rest` keeps its place among the arguments, and SQ4 EGA's objects
  resolve. Code that the text cannot have (a property past the end of its
  object, a `super` in a procedure) is `asm`, so the rest of the script
  compiles. More of Sierra's control flow decompiles (for example a loop
  used as a value, or values that a case leaves on the stack). Three
  compiler errors are now warnings: a `&rest` in the parameters of a send
  whose target calls, a selector that the object does not have, and a
  property sent with more than one value (Sierra's compiler gives them).
* **Eliminated most `asm` fallbacks in the decompiler.** When the decompiler
  could not reconstruct a function's control flow it used to give up and emit
  raw `asm` disassembly. It now rebuilds the control flow into real source, so
  far fewer functions fall back to `asm` -- for example, the Quest for Glory IV
  scripts now decompile with no `asm` fallbacks. More of Sierra's nested loops
  decompile, for example a loop that a `break` leaves past its last jump back,
  a `while` that is the first statement of a `repeat`, and a loop whose body
  starts with a `switch`, as in the save dialog of many SCI0 games. So do more
  nested `and`/`or` expressions, for example the dialog code of many SCI0
  games, and an `and` with an `if` used as a value in one of its operands.
* **Fixed bytecode output.** The compiler produced wrong bytecode in some cases:
  a constant `(mod a b)` was folded as bitwise-and instead of modulo (so
  `(mod 7 3)` gave 3, not 1), and large shift counts were mishandled. An `and`
  or `or` used for its value now gives the operand that decides it, as Sierra's
  compiler does, not 1 or 0, so a script that used that 1 gets a different
  value. A call to `procN_M`, a procedure of a script N that is not in the
  game (Sierra left some in King's Quest VI), compiles with a warning. The
  decompiler writes such a call as `__procN_M`, which recompiles to the same
  call with no warning. The compiler now also reports an
  error instead of silently emitting bad bytecode when it cannot resolve a
  branch, corrects the SCI0 public-export order, and rejects assembly opcodes
  that the target SCI interpreter cannot run. A `(continue 2)` (or a higher
  level) from a loop inside a `for` loop now goes to the step of the `for`;
  it gave no code before.
* **Faster whole-game decompiles.** Naming the global variables used to mean
  decompiling every script again, several times over, until no more names
  changed. The decompiler now decompiles and writes each script once, keeps
  only a small record of how each one uses the globals, works out the global
  names across all of them from that, and then decompiles again only the
  scripts a new name changes. One script is in memory at a time. When a
  smaller selection renames a global, only the previously decompiled scripts
  that use it are offered for decompiling again. The Decompile dialog starts
  with every script selected.
* **Improved decompilation output.** The reconstructed source is more idiomatic
  and follows the "golden" decompilations from
  [sluicebox's SCI tools](https://github.com/sluicebox/sci-tools) much more
  closely (control-flow shapes, expressions and comparisons). Names keep a
  `#` as the game has it (for example `river#1`, before `river_1`). The
  Decompile dialog names new scripts in script-number order, so the `_N`
  suffix of a duplicate name is stable, and a name is always a valid file
  name and `(use ...)` name. An export that Sierra left pointing into the
  middle of another function (for example in Quest for Glory III) is left
  out with a warning, as in sluicebox's tools; it gave wrong text before.
  A variable named from an object is a valid name too (`gGame_opt` from the object `game.opt`).
* **More accurate compile messages.** Every compile message gives the right
  line (some parser messages were one line early), the error and warning
  counts are exact, and a script file that cannot be read gives an error. A
  compile that cannot start, or cannot save the class and selector tables,
  says why in the compile output. An `else` clause that is not the last
  clause of a `cond` is a warning: the compiler drops it and the clauses
  before it. A header that does not parse gives its errors once for a
  compile of many scripts, a game with no vocabulary gives one error for
  its synonyms, and a line break of another style than the file's first one
  is a warning (it does not end a line, as in the script editor).
* **Fewer crashes on bad or corrupt data.** The decompiler, compiler and
  resource loaders are hardened against malformed, truncated or crafted game
  files, so opening a damaged game no longer crashes the app. Damaged data is
  now reported instead of hidden: the Decompile dialog shows why a decompile
  stopped, a text resource that is cut off is marked as failed instead of
  being shown in part, and a resource that is missing from its volume file,
  or whose header is damaged, is marked "Corrupt" in the resource list. An
  empty resource (for example, a text with no strings) is valid: "Rebuild
  resources" keeps it, and a delete of it works.
* **Fixed deadlocks and race conditions** in background work (compiling,
  decompiling, the class browser, resource rendering and MIDI playback).
* **Fixed use-after-free bugs and memory leaks** across the editors and dialogs.
* **Fixed other crash conditions** surfaced by static analysis and sanitizers.
* **Safer saving.** Writing game resources is now atomic, so an interrupted or
  failed save no longer corrupts or loses a resource or volume file. A failed
  save now says what went wrong (for example, the size limit of the game's
  format), and a group of patch files replaces none of them when one cannot be
  written. A compile or decompile now also reports a failed write of its
  `.sco`, `.scd` or `.sc` file, and a compile that cannot write its output
  fails and writes nothing of that script, so the game never gets a
  script without its class table. A compile whose write is refused puts
  back the `.sco` and `.scd` files that it changed. A write of the package
  that fails before it changes the game leaves no `.bak` file; one that
  fails after it names the `.bak` files that put the game right. Before a compile writes into the game's package, it asks what to do
  with patch files that would hide the new resources (the game reads a patch
  file first), and it can move them aside to a `replaced-patches` folder.
  When the Decompile dialog prepares the `src` folder, it copies the
  decompiler files with no prompt and never overwrites a file of the game.
* **Removed legacy SCI Studio script syntax.** Scripts now use SCI Companion's
  Sierra-style syntax only.
* **Modern build tools.** The project now builds with the Visual Studio 2022
  toolset (v143), upgraded from Visual Studio 2015 (v140), and uses standard C++
  facilities such as `std::filesystem`.
* **More testing and continuous integration.** The unit-test suite runs by
  default and is much larger, with an integration-test harness and golden
  regression suites for both compiled bytecode and decompiler output. CI also
  runs an AddressSanitizer leg and MSVC static analysis (`/analyze`).
* **Updated dependencies.** The bundled giflib is updated from 5.1.1 to 5.2.2,
  which brings its decoder hardening and security fixes.
* **Removed SCI11+ extensions.** Language extensions that only run on the
  customized SCI11+ interpreter have been removed, because they do not work on a
  stock Sierra SCI interpreter. See the note below for details.

## Language extensions

The compiler supports a few keywords beyond Sierra's original syntax. Each
one is rewritten into ordinary code before generation and emits only bytecode
and kernel calls that a stock Sierra SCI interpreter runs, so scripts using
them still work on the original interpreters. They are always available; there
are no build-time feature switches.

* `&exists` - clearer optional-argument checks, as in `(if (&exists theX) ...)` instead of `(if (>= argc 1) ...)`.
* `foreach` - iterate an array or a Node-based collection (anything using the Node kernel calls and exposing `elements`): `(foreach val anArray ...)`. `val` need not be declared beforehand. `foreach` is a reserved word.
* `verbs` - a terse block that expands into a standard `doVerb` method. `verbs` is a reserved word.
* `&layout` - `(properties &layout ...)` in a class gives all the property slots of the class after `--info--`: the properties of the text, in the order of the text. The class then has no other property of its superclass, and it has a `name` property only when its text has one (usually as the first property). Without `&layout`, a class has the properties of its superclass, then its new properties. Some classes of Sierra's games have properties that are not those of their superclass (Sierra probably changed the superclass and did not compile the class again). The decompiler writes `&layout` for such a class, so that its methods read the same slots when the text compiles again. A subclass or an instance of the class gets its layout. `&layout` is not for an instance.
* `&getpoly` - `(gRoom addObstacle: (&getpoly "Foo"))`, where `Foo` is a named polygon from the picture editor, expands into the `((Polygon new:) type: ... init: ... yourself:)` bytecode you would see when decompiling a Sierra original. Remove the room's `(include ___.shp)` line and add `(use Polygon)`.

Check out [the examples](examples.md) for a somewhat better explanation of the keywords.

A few non-syntax conveniences are also always on: friendlier `Display`
argument names when decompiling (`dsWIDTH` instead of `106`), extra vocab
sidebar previews, hexadecimal font-grid labels, and an optional "warn on
unused instances" compile check (in Preferences). There are also changes that
were never behind a switch, such as the *Shrinkwrap cel* menu item.

Kawa's variable-dereference operator (`*var`, the `LDM`/`STM` opcodes) has been
removed. Those opcodes exist only in Kawa's customized "SCI11+" interpreter,
not in any Sierra interpreter, so scripts using them would not run on an
original game. Use the `Memory` kernel call for raw memory access instead.
