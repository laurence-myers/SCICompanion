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

## For step 11 (corpus gate sample with -Engine auto)

- Forms that are worse against Snuffer than classic, with the same
  meaning: `cond` arms where Snuffer has `if`s with `(continue)` (PQ2
  `phoneNumber::changeState`), `(if c (break) else …)` chains where Snuffer
  has `breakif`s (KQ5 `invW::doit`), `else` in place of `(continue)` (Longbow
  `series::changeState`). The template snapshots change the same way
  (`breakif`, `(if c else (break))`, `cond` arms).
- Names: when a function that was asm becomes source, the name guess
  (the assignments of the text) finds new names for locals and globals, so
  other functions of the game go from SAME to NAMES against Snuffer (16
  functions, Longbow scripts 200 and 330).
- Refusals other than the function-bounds ones: `or-statement` (12, a
  statement list as an operand), `toss-outside-switch` (2), `case-test`
  (4), `term-statement` (1).

## Gaps

- The value stage refuses an empty and-term (`empty-term`: a `bnt` right
  after a `bnt` to the same place that another branch reaches). No test
  builds the shape with forward branches only; the guard is verified by
  inspection.
