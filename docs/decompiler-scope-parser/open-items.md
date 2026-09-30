# Open items: decompiler scope parser

Open items only: gaps, unfixed findings, questions. The plan is in
`plan.md`.

## Function report (`--function-report`)

- Two export slots that point at one procedure give two functions with
  one offset. The report keys its lines by script and offset, so the
  second line replaces the first, and the report has fewer lines than
  the statistics count (`CliScriptCommands.cpp`, `CliDecompileResults`).
- Pass 2 of the batch (`DecompileAndRewrite`) goes through the quiet
  `PassThroughResults`, which drops `InformFunction`. So a line comes
  from pass 1, not from "the last decompile of a script" as
  `CliCommands.h` and `docs\scic-cli\plan.md` say. The text is the same
  today, because a decompile is deterministic.
- `DecompileRaw` sends the line before the AST passes and the naming. If
  one of them throws, the script fails, but its functions keep lines
  with `ok`. A graph analysis that throws something other than a
  `ControlFlowException` gives no line.
- The report file is checked only for "not a function report": a folder
  that does not exist shows only at the end, as exit 9. A run that
  selects no script, or that cannot start, leaves the old report in
  place.
- The help of `--dry-run` says "write nothing", but a dry run writes the
  function report.

## Compare tool and corpus gate

- 49 Snuffer files of the library do not parse with our parser (for
  example `((ScriptID 310 4) heading:)`, a call as a send target in an
  argument). The compare leaves those scripts out and gives a warning; a
  function there has no verdict.
- Methods pair by the index of their class (`class#N::method`): a class
  that one side does not have shifts the keys of the classes after it.
  Local procedures pair by an alignment; classes do not.
- In the sample mode, compare-structure parses every Snuffer script of a
  game, and the gate keeps only the rows of the sampled scripts. A filter
  of the scripts in the command would make a run faster.
- The three games that Snuffer cannot read (King's Quest IV 1988, Willy
  Beamish, Space Quest 3 dev) give only ONLY-ACTUAL rows.
- The ASM verdict count (296) is lower than the asm count of the
  function reports (343): a function of a script that the compare leaves
  out, or of a game with no Snuffer output, has no ASM verdict. Rule 2 of
  the gate uses the function reports.
- `scic dev compare-structure "" <folder>` treats the empty folder as a
  folder with no scripts (exit 6 with a warning), not as NotFound (3).
- `TestCli::Dev_CompareStructure` checks the table's form, not a verdict
  or a change value (TestStructuralCompare does), and not that no game
  opens.
- A Snuffer run that times out or fails writes its state into
  `status.txt` and is never run again; a partial `src` of a failed run is
  still the expected side. Delete `<cache>\snuffer\<md5>` to run it again.
  A Snuffer failure does not fail the gate: the rows of that game become
  ONLY-ACTUAL, which rule 6 does not rank.

## Code model and verify

- The dialect pass can move a `bnt` onto a `contif` of a `for` loop (a
  `bt` to the step): `_IsLoopContinuation` knows only the head and the
  instruction after the latch, not the step. Both targets are equal by
  resolution, so only the presentation can change.
- `HasDepthConflict` marks each instruction after a merge point again
  when a smaller depth comes later, not only the merge point.
- A dead back branch to a loop head counts as a latch. Junk code after
  the end of a function that jumps back into a loop would make the loop
  too long; the parse or the verify stage then fails (asm), it does not
  give a wrong tree.
- Verify builds the layout of a tree before it checks the indices of its
  code regions: a malformed tree with a very large code range allocates
  that much memory.

## Parser

- The dialect pass also moves a `bnt` onto a Sierra `breakif` whose
  threaded target is past the loop exit (a `breakif` of an outer loop).
  The tree is correct; only its shape changes (`if a {b}; breakif` in
  place of `if a {breakif b}`).
- A switch with only an else, whose else body starts with a `dup` at the
  switch depth (Sierra's optimiser puts a `dup` in place of `pushi N` when
  the switch value is the same constant N), is read as a case and fails
  with `case-value` (asm).
- An and-term is taken only for an `if` with an else-part. For a `while`
  test (an else that is a loop exit) the terms are nested ifs, each with
  a break as the else; the presentation must make them one `and`.
