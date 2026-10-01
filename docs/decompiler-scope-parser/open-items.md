# Open items: decompiler scope parser

Open items only: gaps, unfixed findings, questions. The plan is in
`plan.md`.

## Outside the plan

- **Names that compile to another value.** The meaning check finds text
  whose names the compiler resolves to another value (the names come from
  the lookups, not from the control flow). 79 functions of the
  gate sample (the gate allowlist has them):
  - an object with the name of a property: `(= controls controls)` in
    `Rm::init` of many SCI0 games stores the property, not the object
    `controls`; also `message` in Longbow `ok::select` (20);
  - a property at another index in a decompiled class: LSL1 VGA
    `GameControls::show` reads `okButton` (property 42) as property 44
    (1);
  - two selectors with one name: the SCI1.1 template sends selector 509
    (`case`), and its recompile sends 732 (`Conversation::add`,
    `DialogEditor::exit`).
- **Decompiled text that does not compile.** 1418 functions of the gate
  sample are in scripts whose decompiled text has compile errors (for
  example `&rest` in a send whose target has a nested send, or a property
  that gets two values). The meaning check gives them UNCOMPARED
  (`not-recompiled`).

## Meaning DIFFs of the full corpus

The full gate run (`-Full -Meaning`, 93 games) gives 1506 DIFF rows (1402
of scope functions). Many games have two or three copies, so there are
fewer distinct defects. Survey by cause:

- **A class with no `of`** (104 rows): `Class_943_3` (Castle of Dr.
  Brain), `Class_86_0`, `Class_47_1`. Each property of the recompile is
  at index + 2; the original has no `name` slot (not checked in the
  bytes).
- **Another property layout of a superclass** (35 rows): LSL1 and
  Mixed-Up Fairy Tales 995, IconBar and `Inv`.
- **An object with the name of a property** (67 rows; see "Names that
  compile to another value").
- **A local procedure that only dead code calls** (2 rows): QfG3 460
  `localproc_1f5b`, Mixed-Up Fairy Tales 927 `localproc_0492`. The
  decompiler finds it as the target of a call in dead code after the end
  of another function, and leaves that code out. The recompiled procedure
  has no caller, so the check does not find it (`no-recompiled-function`).
- **Selectors with no name** (22 rows): QfG2 dev 909, `sel_713` and
  others get new numbers in the compile.
- **Two kernel functions with one name** (6 rows): ECO1 540 `Dummy` (81
  and 38).
- **Not explained** (1 row): Island of Dr. Brain 268 `anElement::select`.

## Refusals

The full corpus run gives 66 functions as `asm`:

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
- `statement-in-expression` (4): Longbow 24 `yeoScript::changeState`
  (three copies) and QfG4 CD 81 `antOut::changeState`: a call whose value
  no instruction reads, in the operands of another call after a store.
- `slot-effect` (2): PQ1 VGA 999 `Obj::showSelf` and `Collect::showSelf`:
  a `calle` with no arguments takes the result of a send as its argument
  count (`push; calle 921 0 0`).
- `stack-underflow` (2): Camelot 40 `Rm40::handleEvent` (two copies): a
  branch back goes past the push of an argument count.
- `term-statement` (1): Pepper 230 `sTalkPoorRich::changeState`: an
  and-term with statements before its value.
