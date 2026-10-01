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
  - a class name that two species have: KQ5 (species 29 and 88, 26 and
    27, 25 and 26), GK1 and PQ1 VGA: the text sends to the other class
    (58);
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

- **The compile gives other species** (1074 rows, 1010 in KQ5). The
  compile takes the species of a class from the class table (vocab 996)
  by script and position, and fails when a game has one species in two
  scripts (KQ5: `Rev` in scripts 992 and 978). Also ECO1, PQ1, SQ4,
  Freddy, LSL1 and LSL3.
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

- The refusals of the gate sample other than ICEMAN #235 (16 functions,
  given as `asm`):
  `acc-no-fact` (ICEMAN 385 `localproc_02bc` in three copies, Hoyle
  Classic 716 `other1_tree::doit`, QfG1 VGA 0 `proc0_3` in two copies),
  `stack-underflow` (Camelot 40 `Rm40::handleEvent` in two copies),
  `case-test` (PQ3 36 `alreadyDoneIV::changeState` in three copies),
  `no-scope-for-target` (SQ4 patch 16, 271, 391), `dup-no-value` (QfG4
  floppy 670 `pMainDoor::doVerb`), `term-statement` (Pepper 230
  `sTalkPoorRich::changeState`).

## Gaps

- The value stage refuses an empty and-term (`empty-term`: a `bnt` right
  after a `bnt` to the same place that another branch reaches). No test
  builds the shape with forward branches only; the guard is verified by
  inspection.
