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
