# Open items: decompiler scope parser

Open items only: gaps, unfixed findings, questions. The plan is in
`plan.md`.

## Outside the plan

- **Functions that start inside another function.** The decompiler takes
  some procedures to start in the middle of the code of another one (for
  example QfG3 `proc7_0` starts after the push of `new`, which Snuffer
  shows as `(sleepIcon new: 2 0 0 5)`; QfG1 `proc32_3` starts with
  `bnot`). The classic engine gives wrong text for some of them with no
  warning; the scope engine refuses them (`stack-underflow` or
  `acc-no-fact` at the first instruction that takes a value). The cause
  is in the search of the function bounds, not in the engines. About 20
  functions of the gate sample.
- **A slow classic decompile.** Hoyle Classic Card Games (1993), scripts
  715 and 718: the classic engine takes minutes for one function (also
  before this plan). The scope engine decompiles the scripts at once.

## Questions for the owner (gate rule 6)

On the gate sample with -Engine auto (step 11), 23 functions that change
are worse against Snuffer than with the classic engine; 29 are better.

- **Forms that the bytecode cannot tell apart (7 functions).** An `if`
  whose then-part ends with a `jmp` to the loop head: the scope engine
  gives `(if c X else Y)` (a `cond` for a chain), Snuffer and classic
  give `(if c X (continue)) Y` in some places (PQ2
  `phoneNumber::changeState`, KQ5 `invW::doit`, Longbow
  `series::changeState`). Snuffer marks each such `jmp` as a `continue`
  and its pass `IfContinueRefactor` makes an `else` of it only in a loop
  body list or a then-part. A `continue` form for every such `jmp` made
  the source shape of P2/F11 (a nested `cond`) worse, so it is not in
  this branch. Accept, or copy Snuffer's rule exactly?
- **Names (16 functions).** When a function that was asm becomes source,
  the name guess (from the assignments of the text) finds names for some
  locals and globals, so other functions of the game go from SAME to
  NAMES (Longbow scripts 200 and 330). Accept as not worse?

## For step 12

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
