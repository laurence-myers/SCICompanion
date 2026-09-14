# SCICompanion
SCI Companion - a complete IDE for Sierra SCI games (SCI0 to SCI1.1)

Official website:
http://scicompanion.com

General notes:
The bulk of the code is in SCICompanionLib\Src

SCICompanion is the .exe which is just a thin wrapper over SCICompanionLib

## A note from Kawa
The following defines are available:

* `ENABLE_EXISTS` - Enables the use of the `&exists` keyword, as in `(if (&exists theX) ...)` instead of `(if (>= argc 1) ...)`.
* `ENABLE_LDMSTM` - Enables variable dereferencing (`*var`) as an alternative to `(Memory memPEEK)` and `(Memory memPOKE)`. Requires a special build of the SCI interpreter with two new opcodes, such as SCI11+.
* `ENABLE_FOREACH` - Enables the use of the `foreach` keyword. `(foreach val anArray ...)` or `(foreach val aCollection)` (where `aCollection` is anything that uses the Node kernel calls and exposes `elements`) will expand into loops where `val` is each value in the set, in order. `val` needs not be defined beforehand. With `ENABLE_LDMSTM` enabled you can also use `&val` as a reference to a `val` you *did* define beforehand.
* `ENABLE_VERBS` - Enables the use of the `verbs` keyword.
* `DISABLE_DEBUGSTUFF` - Hides some in-depth decompiler stuff that's not officially supported.
* `DISABLE_TRANSPARENCYNAG` - Disables the confirmation dialog when pasting an image that contains the transparent color. Annoying!
* `ENABLE_FONTNUMSINHEX` - Makes the font editor's character picker labels display in hexadecimal, to match Windows Character Map.
* `DISABLE_FONTLIMIT` - Disables the 256-cel limit on fonts (but also maybe views, so be careful!). Fonts have a 16-bit character count, and with a customized interpreter such as SCI11+ you can now have basic UTF-8.
* `ENABLE_MOREVOCABPREVIEWS` - Adds some more sidebar previews for vocabs, like the Object Offsets (994), ~~and the opcode list (998)~~. Also changes the display format for kernel names (999).
* `ENABLE_DISPLAYMASSAGE` - Makes the `Display` kernel call's arguments look better, with proper constants instead of numbers, so `dsWIDTH` instead of `106`.
* `ENABLE_GETPOLY` - Enables the use of the `&getpoly` keyword. `(gRoom addObstacle: (&getpoly {Foo}))`, where `Foo` is a named polygon from the picture editor, will expand into `(gRoom addObstacle: ((Polygon new:) type: <whatever> init: <long list of coords> yourself:))` upon compilation, just like you'd see if you decompile a Sierra original. Don't forget to *not* include the `.shp` file, and *do* `(use Polygon)`. Convert all your rooms and you won't need `AddPolygonsToRoom` and `CreateNewPolygon` any more!

Check out [the examples](examples.md) for a somewhat better explanation of the features that add keywords.

There are a few other changes that aren't defined away, such as the *Shrinkwrap cel* menu item. In the `Debug` and `Release` target, none of the above are enabled. In `Mild`, only `ENABLE_MOREVOCABPREVIEWS` and `ENABLE_DISPLAYMASSAGE` are, unless someone wishes otherwise. The `Kawa` target has all of them enabled *but* `ENABLE_LDMSTM`.
