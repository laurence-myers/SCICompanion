# Open items: decompiler scope parser

Open items only: gaps, unfixed findings, questions. The plan is in
`plan.md`.

## Outside the plan

- **A public instance with the name of a property.** The decompiler gives
  an instance that is not public, and that has the name of a property of
  an object of its script, another name (with its name string as an
  explicit `name` property). A public instance keeps its name, because
  other scripts refer to it by its name: in a method of an object with
  that property, the text then means the property. The full corpus has no
  such case.
- **The name of an object with no name slot after --info--.** The reader
  takes the name of an object only from the slot after `--info--`, and only
  when the value there points to a string. It has no selector table, so it
  cannot find a `name` slot that is later in the layout: QfG3 47
  `Class_47_1` and its subclasses get made-up names, and a class with no
  superclass whose first property holds a string gets that string as its
  name. The meaning check keys a class with a made-up name by its species.
- **Decompiled text that does not compile.** 2112 functions of the full
  corpus are in scripts whose decompiled text has compile errors (the
  meaning check gives them UNCOMPARED `not-recompiled`). By cause:
  - 1156: two script names in `game.ini` that differ only in case
    (ICEMAN `subMarine` and `Submarine`, two copies; QfG1 VGA dev EGA
    `Rock`). On Windows the two `.sco` files are one file. Corpus data.
  - 598: names with a dot (Hoyle Classic Card Games `gGame.opt`, Hoyle 2
    `gOptions.sol`), which PR #256 handles.
  - 99: a script that the script uses did not compile.
  - 81: a `lofsa` to an address that is no object and no start of a
    string (`LOOKUP_ERROR`; SQ4 main points into the middle of a string).
  - 76: corrupt functions (`CorruptFunction_CantDetermineCodeBounds`).
  - 64: instances of a class whose script is not in the game (Slater and
    Charlie 947, the dialog editor): the text has no superclass for them,
    so their `super` does not compile.
  - 38: other single causes: a global past the globals of `Main`, a
    property name that is no selector (`curPosnX`), corrupt code that reads
    as variables such as `global33792`.

## Refusals

The full corpus run gives 110 functions as `asm`:

- `syntax` (46): code that the text cannot have: a property past the end
  of its object (for example `Act::canBeHere` of ICEMAN, LSL3 and QfG1,
  Hoyle 1 `Deck`, `MouthSync::init`), and a
  property read in a procedure (an export at the code of a method:
  Mixed-Up Mother Goose script 0).
- `case-test` (46): each function has a corrupt branch (issue #235; the
  decode replaces it with `ldi 47789` and warns "Bad branch").
- `no-scope-for-target` (5): the SQ4 copy in a "patch" folder (scripts 16,
  271, 387, 391): a fan patch.
- `acc-no-fact` (4): SQ4 patch 405 and 410: the
  operands of a `mul` in the other order, with a variable pushed after the
  call (`callk Random; lsg 199; mul`); as text, the compiler reads the
  variable before the call, which can change it.
- `stack-unbalanced` (4): Longbow 24 `yeoScript::changeState` (three
  copies) and SQ4 EGA 376 `sp1::doVerb`: values that a sequence leaves on
  the stack, where the code after it can read them (a `dup` at their depth,
  a branch).
- `statement-in-expression` (1): QfG4 CD 81 `antOut::changeState`: a call
  whose value no instruction reads, in the operands of another call after a
  store.
- `slot-effect` (2): PQ1 VGA 999 `Obj::showSelf` and `Collect::showSelf`:
  a `calle` with no arguments takes the result of a send as its argument
  count (`push; calle 921 0 0`).
- `stack-underflow` (2): Camelot 40 `Rm40::handleEvent` (two copies): a
  branch back goes past the push of an argument count.
