# Open items: decompiler scope parser

Open items only: gaps, unfixed findings, questions. The plan is in
`plan.md`.

## Outside the plan

- **A public instance with the name of a property.** The decompiler gives
  an instance that is not public, and that has the name of a property of
  an object of its script, another name (with its name string as an
  explicit `name` property). A public instance keeps its name, because
  other scripts refer to it by its name: in a method of an object with
  that property, the text then means the property.
- **Decompiled text that does not compile.** 2122 functions of the full
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
  - 10: two classes of the class table with one name (King's Quest V 764,
    `SaveIcon`).
  - 38: other single causes: a global past the globals of `Main`, a
    property name that is no selector (`curPosnX`), corrupt code that reads
    as variables such as `global33792`.

## Meaning DIFFs of the full corpus

The full gate run (`-Full -Meaning`, 93 games) gives 148 DIFF rows of
scope functions. Many games have two or three copies, so there are fewer
distinct defects. Survey by cause:

- **A class with no `of`** (105 rows): `Class_943_3` (Castle of Dr.
  Brain), `Class_86_0`, `Class_47_1`, SQ4 EGA `Class_950_0`. Each
  property of the recompile is at index + 2; the original has no `name`
  slot (not checked in the bytes).
- **Another property layout of a superclass** (38 rows): LSL1 and
  Mixed-Up Fairy Tales 995, IconBar and `Inv`; LSL1 VGA
  `GameControls::show` reads `okButton` (property 42) as property 44.
  `DelayedEvent` of script 947 (Castle of Dr. Brain, Mixed-Up Fairy Tales,
  QfG2) is a class of `Event` with the properties of `Script`. EcoQuest 2
  959 `QSnd` and 960 `TimedCue`, The Colonel's Bequest dev 414
  `ToastClass`.
- **An instance with the name of a class** (2 rows): Pepper 110 has an
  `Actor` named `twisty`, the name of the game class; the text means the
  class.
- **A local procedure that only dead code calls** (3 rows): QfG3 460
  `localproc_1f5b`, Mixed-Up Fairy Tales 927 `localproc_0492`. The
  decompiler finds it as the target of a call in dead code after the end
  of another function, and leaves that code out. The recompiled procedure
  has no caller, so the check does not find it (`no-recompiled-function`).

## Refusals

The full corpus run gives 116 functions as `asm`:

- `syntax` (49): code that the text cannot have: a property past the end
  of its object (for example `Act::canBeHere` of ICEMAN, LSL3 and QfG1,
  Hoyle 1 `Deck`, King's Quest V `SaveIcon`, `MouthSync::init`), and a
  property read in a procedure (an export at the code of a method:
  Mixed-Up Mother Goose script 0).
- `case-test` (46): each function has a corrupt branch (issue #235; the
  decode replaces it with `ldi 47789` and warns "Bad branch").
- `no-scope-for-target` (5): the SQ4 copy in a "patch" folder (scripts 16,
  271, 387, 391): a fan patch.
- `acc-no-fact` (6): QfG1 VGA 0 `proc0_3` (two copies): the value of a
  loop is an operand of an `or`, after a store that would have to be the
  initialisation of a `for`. SQ4 patch 405 and 410 (four functions): the
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
- `term-statement` (1): Pepper 230 `sTalkPoorRich::changeState`: an
  and-term with statements before its value.
