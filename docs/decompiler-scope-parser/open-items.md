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

## Gaps

- The value stage refuses an empty and-term (`empty-term`: a `bnt` right
  after a `bnt` to the same place that another branch reaches). No test
  builds the shape with forward branches only; the guard is verified by
  inspection.
