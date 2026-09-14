# SCICompanion
SCI Companion - a complete IDE for Sierra SCI games (SCI0 to SCI1.1)

Official website:
http://scicompanion.com

General notes:
The bulk of the code is in SCICompanionLib\Src

SCICompanion is the .exe which is just a thin wrapper over SCICompanionLib

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
