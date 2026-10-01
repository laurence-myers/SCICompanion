# Open items: decompiler scope parser

Open items only: gaps, unfixed findings, questions. The plan is in
`plan.md`.

## Outside the plan

- **Functions that start inside another function.** The decompiler takes
  some procedures to start in the middle of the code of another one (for
  example QfG3 `proc7_0` starts after the push of `new`, which Snuffer
  shows as `(sleepIcon new: 2 0 0 5)`; QfG1 `proc32_3` starts with
  `bnot`; QfG4 floppy `proc670_1` starts inside a `send`). The classic
  engine gives wrong text for some of them with no warning (QfG4
  `proc670_1` is `(fChopBlock)`); the scope engine refuses them
  (`stack-underflow` or `acc-no-fact` at the first instruction that takes
  a value). The cause is in the search of the function bounds, not in the
  engines. About 20 functions of the gate sample.
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
- **Decompiled text that does not compile.** 1418 functions of the gate
  sample are in scripts whose decompiled text has compile errors (for
  example `&rest` in a send whose target has a nested send, or a property
  that gets two values). The meaning check gives them UNCOMPARED
  (`not-recompiled`).

## For step 13

- Four functions that the classic engine decompiles and the scope engine
  refuses. With `scope` as the default they are asm: rule 1 counts them as
  REGRESSED. ICEMAN (1989) 100 `introScript::changeState` (issue #235: the
  decode replaces a corrupt branch with `ldi 47789`), QfG3 7 `proc7_0` and
  QfG4 floppy 670 `proc670_1` (the function bounds above: the classic text
  is wrong), SQ4 patch 381 `roboClerkWelcome::changeState` (the fan
  patch). Owner decision needed: accept them as REGRESSED, or fix the
  function bounds first.
- Refusals on the gate sample, other than the function bounds above:
  `case-test` (4), `no-scope-for-target` (3), `toss-outside-switch` (2),
  `dup-no-value` (1).
- The template snapshots change with the scope engine (12 of 86 scripts,
  reviewed: the same meaning): `(breakif c)` for a `bt`, an empty
  `(else )` gone, and the `if`/`else` form above (`Gauge`,
  `ScrollableInventory`, `SaveRestoreDialog`).

## Gaps

- The value stage refuses an empty and-term (`empty-term`: a `bnt` right
  after a `bnt` to the same place that another branch reaches). No test
  builds the shape with forward branches only; the guard is verified by
  inspection.
