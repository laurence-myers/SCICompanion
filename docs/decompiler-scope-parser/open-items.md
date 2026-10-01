# Open items: decompiler scope parser

Open items only: gaps, unfixed findings, questions. The plan is in
`plan.md`.

## Outside the plan

- **A slow classic decompile.** Hoyle Classic Card Games (1993), scripts
  715 and 718: the classic engine takes minutes for one function (also
  before this plan). The scope engine decompiles the scripts at once.
- **Names that compile to another value.** The meaning check finds text
  whose names the compiler resolves to another value. Both engines give
  this text (the names come from the same lookups). 79 functions of the
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
- **An export table that differs from Snuffer's.** KQ5 floppy and Mixed-Up
  Fairy Tales (VGA and EGA), script 975: the decompiler reads export 1 as
  `013a`, export 2 as `015c`, export 3 as `007c` and export 4 as `5776`;
  Snuffer has other addresses and a real `proc975_3`. Export 3 is inside
  `DR::quitGame`, so the decompiler leaves it out as stale, and no function
  covers the code from `008c` to `013a` (Snuffer's `proc975_1` and
  `proc975_3`). Older than the stale-export rule; 3 of the 39 stale
  exports of the corpus (the other 36 are not in Snuffer's public blocks).
- **Decompiled text that does not compile.** 1418 functions of the gate
  sample are in scripts whose decompiled text has compile errors (for
  example `&rest` in a send whose target has a nested send, or a property
  that gets two values). The meaning check gives them UNCOMPARED
  (`not-recompiled`).

## For step 13

- One function that the classic engine decompiles and the scope engine
  refuses: ICEMAN (1989) 100 `introScript::changeState` (issue #235: the
  decode replaces a corrupt branch with `ldi 47789`). With `scope` as the
  default it is asm: rule 1 counts it as REGRESSED. The owner accepts it
  (2026-10-01); step 13 records the decision in plan section 6.
- The other refusals of the gate sample with the `scope` engine (16
  functions; the classic engine gives no source for them either):
  `acc-no-fact` (ICEMAN 385 `localproc_02bc` in three copies, Hoyle
  Classic 716 `other1_tree::doit`, QfG1 VGA 0 `proc0_3` in two copies),
  `stack-underflow` (Camelot 40 `Rm40::handleEvent` in two copies),
  `case-test` (PQ3 36 `alreadyDoneIV::changeState` in three copies),
  `no-scope-for-target` (SQ4 patch 16, 271, 391), `dup-no-value` (QfG4
  floppy 670 `pMainDoor::doVerb`), `term-statement` (Pepper 230
  `sTalkPoorRich::changeState`).
- The template snapshots change with the scope engine (12 of 86 scripts,
  reviewed: the same meaning): `(breakif c)` for a `bt`, an empty
  `(else )` gone, and the `if`/`else` form above (`Gauge`,
  `ScrollableInventory`, `SaveRestoreDialog`).

## Gaps

- The value stage refuses an empty and-term (`empty-term`: a `bnt` right
  after a `bnt` to the same place that another branch reaches). No test
  builds the shape with forward branches only; the guard is verified by
  inspection.
