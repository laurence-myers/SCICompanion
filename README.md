# SCICompanion

SCI Companion - a complete IDE for Sierra SCI games (SCI0 to SCI1.1)

## What's new in 4.0.0

This release focuses on the compiler and decompiler, on stability, and on
modernizing the build. Broad highlights since the previous release:

* **A command-line tool, `scic.exe`.**
* **A new decompiler engine.**
* **Decompiled scripts compiles to the same bytecode.**
* **Fixed incorrect bytecode output.**
* **Eliminated most `asm` fallbacks in the decompiler.** Now, only bad bytecode
  will produce `asm`, rather than bugs in the decompiler.
* **Faster whole-game decompiles.** No more multi-pass decompiles.
* **Improved decompilation output.** The reconstructed source is more idiomatic
  and follows the "golden" decompilations from
  [sluicebox's SCI tools](https://github.com/sluicebox/sci-tools) much more
  closely (control-flow shapes, expressions and comparisons).
* **Decompiles games that have no selector table** (such as the floppy Laura
  Bow 2). Each selector gets a numbered name, sel_<number>.
* **The SCI1.1 template game is compiled from its current sources.** A new
  game starts with the scripts that its `src` folder holds.
* **Better compile warning/error messages.**
* **Fixed some crashes on bad or corrupt data.** 
* **Fixed some deadlocks and race conditions.**
* **Fixed some use-after-free bugs and memory leaks.**
* **Fixed other crash conditions** surfaced by static analysis and sanitizers.
* **Atomic file saving.** 
* **Removed legacy SCI Studio script syntax.** Scripts now use SCI Companion's
  Sierra-style syntax only.
* **Modern build tools.** The project now builds with the Visual Studio 2022
  toolset (v143), and uses standard C++17 facilities such as `std::filesystem`.
* **More testing and continuous integration.** The unit-test suite runs by
  default and is much larger, with an integration-test harness and golden
  regression suites for both compiled bytecode and decompiler output. CI also
  runs an AddressSanitizer leg and MSVC static analysis (`/analyze`).
* **Updated dependencies.**
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
* `&layout` - normally, class properties are defined in the order of the superclass, but some games flaunt this rule - perhaps the superclass was re-compiled after the class was compiled, creating stale bytecode. This syntax lets you override the order. (You shouldn't normally use this.)
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

## Project Layout

- SCICompanion: the GUI entry point.
- SCICompanionCli: the CLI entry point.
- SCICompanionCore: holds the MFC-free code for resource handling, including the script compiler,
the script decompiler, and the resource formats. Both the CLI and GUI use this.
- SCICompanionLib: most functionality is implemented here, coupled to the GUI.

## Building

SCI Companion builds with **Visual Studio 2022** and the **v143** platform
toolset. You need:

* Visual Studio 2022 with the **Desktop development with C++** workload,
* the **MFC** component (the app and the GUI library are MFC),
* the **Windows 10 SDK** (10.0.26100 or later), and
* **vcpkg**: the vcpkg component of Visual Studio, or a vcpkg folder in the
  `VCPKG_ROOT` environment variable. The first build downloads the
  libraries of `vcpkg.json` (tl::expected, CLI11 and toml++).

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
