# SCICompanion
SCI Companion - a complete IDE for Sierra SCI games (SCI0 to SCI1.1)

Official website:
http://scicompanion.com

General notes:
The bulk of the code is in SCICompanionLib\Src

SCICompanion is the .exe which is just a thin wrapper over SCICompanionLib

## Building

SCI Companion builds with **Visual Studio 2022** and the **v143** platform
toolset. You need:

* Visual Studio 2022 with the **Desktop development with C++** workload,
* the **MFC** component (the app and library are MFC), and
* the **Windows 10 SDK** (10.0.26100 or later).

Open `SCICompanion.sln` and build the **Release / Win32** configuration, or
build from a command prompt:

```
MSBuild.exe SCICompanion.sln -m -p:Configuration=Release -p:Platform=Win32
```

The unit tests live in the `UnitTests` project. Run them with
`UnitTests\RunTests.ps1` after building.

## Licence

SCI Companion is licensed under the GNU General Public License, version 2 or
(at your option) any later version; see the root `LICENSE` file. The notices
for the third-party components it uses are under
`SCICompanion/Files/Licenses` and are distributed with the program.

## What's new in 4.0.0

This release focuses on the compiler and decompiler, on stability, and on
modernizing the build. Broad highlights since the previous release:

* **Eliminated most `asm` fallbacks in the decompiler.** When the decompiler
  could not reconstruct a function's control flow it used to give up and emit
  raw `asm` disassembly. It now rebuilds the control flow into real source, so
  far fewer functions fall back to `asm` -- for example, the Quest for Glory IV
  scripts now decompile with no `asm` fallbacks.
* **Fixed bytecode output.** The compiler produced wrong bytecode in some cases:
  a constant `(mod a b)` was folded as bitwise-and instead of modulo (so
  `(mod 7 3)` gave 3, not 1), and large shift counts were mishandled. It now
  also reports an error instead of silently emitting bad bytecode when it cannot
  resolve a branch, corrects the SCI0 public-export order, and rejects assembly
  opcodes that the target SCI interpreter cannot run.
* **Improved decompilation output.** The reconstructed source is more idiomatic
  and follows the "golden" decompilations from
  [sluicebox's SCI tools](https://github.com/sluicebox/sci-tools) much more
  closely (control-flow shapes, expressions and comparisons).
* **Fewer crashes on bad or corrupt data.** The decompiler, compiler and
  resource loaders are hardened against malformed, truncated or crafted game
  files, so opening a damaged game no longer crashes the app.
* **Fixed deadlocks and race conditions** in background work (compiling,
  decompiling, the class browser, resource rendering and MIDI playback).
* **Fixed use-after-free bugs and memory leaks** across the editors and dialogs.
* **Fixed other crash conditions** surfaced by static analysis and sanitizers.
* **Safer saving.** Writing game resources is now atomic, so an interrupted or
  failed save no longer corrupts or loses a resource or volume file.
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
