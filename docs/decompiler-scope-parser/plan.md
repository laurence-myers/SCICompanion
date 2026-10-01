# A scope parser and a forward value stage for the decompiler

## 1. Context

The decompiler has many defects in branches, loops and Boolean expressions.
The owner suspects that the control-flow-graph heuristics are too complex,
and that the optimised bytecode makes the analysis hard. The owner rejected
a port of `sci-tools` and asked for a design from original research, based
on the current codebase.

Evidence (corpus sweep of 2026-09-29, 596 failure rows, data in
`I:\tmp\dcsweep`):

- About 490 functions fail in the graph stage (`ControlFlowGraph.cpp`).
- 73 rows fail in the consumption stage. 66 more rows have a graph-stage
  message but keep a value defect after a correct parse. More are masked:
  in a review, 4 of 22 functions had a value defect after a correct parse.
- About 14 of the 19 control-flow fixes of the last 20 days added a shape
  rule. 11 name Sierra's branch threading as the cause.
- No check compares the structure with the bytecode.

**Owner decisions (2026-09-30):** side by side in C++. Route: scope parser
plus forward value stage. Gate: structure match with the Snuffer output. A
text change of a function that decompiles today is accepted when it is
better or equal against Snuffer.

## 2. Findings of the research

Sources: Sierra `sc` 4.100 (`E:\Code\Cpp\da-sci-compiler-pub`), the current
engine, the real failing functions of the sweep.

1. **The compiler emits code in source order.** Each construct is one
   address interval. Intervals nest.
2. **Each branch goes to a continuation of an enclosing construct.**
   Threading (`OPTIMIZE.CPP:171-198`) only moves a target to a continuation
   further out.
3. **Threading keeps the meaning.** Two targets are the same target when
   they resolve to the same place for the sense of the branch.
4. **The bytecode is a forward machine.** Values go forward in the
   accumulator and on the stack, across branches. The optimiser deletes a
   load when it knows what the accumulator holds; a label resets that
   knowledge, a branch does not.

The current engine uses none of these. The graph stage builds a general
graph and matches shapes (7 rewrites, 5 rules, 4 guards). The consumption
stage reads the code backwards, one structure at a time, and repairs each
value that crosses a structure border with placeholders and steal/clone
searches.

## 3. The design

Keep: the decoder, `_GetInstructionConsumption`, `_CodeNodeToSyntaxNode`
(instruction to syntax, all the names), the AST passes, the formatter.
Replace: the graph stage and the backward walk. No stage edits an
instruction.

```
decode -> code model -> scope parser -> region tree -> verify
       -> forward value stage -> chunk tree -> _CodeNodeToSyntaxNode -> AST passes
```

### 3.1 Target equivalence (replaces all de-threading)

Three functions over the instruction array, with cycle guards:

- `resJ(a)`: follow each `jmp` from `a`.
- `resF(a)` (arrive with a false value): follow `jmp`; take `bnt`; fall
  through `bt`.
- `resT(a)` (arrive with a true value): follow `jmp`; take `bt`; fall
  through `bnt`.

`bnt X` equals `bnt K` when `resF(X) == resF(K)`. `bt` uses `resT`; `jmp`
uses `resJ`.

### 3.2 Pre-passes (linear scans)

| Pass | Rule |
|---|---|
| Live code | Reachability from the entry. A `jmp` that skips only dead code (or nothing) does nothing. A conditional branch whose target equals its fall-through does nothing. A `bt` or `bnt` right after one of the same kind to the same place (only `jmp`s that do nothing between them), which no other branch reaches (nor those `jmp`s), does nothing: the optimiser deleted the load of a repeated value (`(and a b b)`). Dead straight-line code right after a `ret` stays as statements; other dead code gives no statement (as text it would run; verify makes sure that no path of the tree gets to it); a dead `jmp` is a layout hint only, but a dead `break` keeps its place when a branch resolves through it. |
| N-ary compare | `cmp; bnt O; pprev` is one value when `resF(O)` equals `resF` of the chain end. The `bnt` is marked inert in a side table. |
| Loops | A branch to an address at or before it is a back branch. The loop of head S is [S, last back branch to S]. One loop per head. When the parse fails on a jump to the instruction after an earlier back `jmp` J, the loop is split at J and parsed again. |
| Switches | A stack-depth profile in address order. Each `toss` ends one switch; its head is the push of the tossed slot. A case starts at each `dup` at the switch depth; its value is [dup+1, `eq?`) and is parsed as a sequence; its body ends at the trailing `jmp` to the `toss`. |
| Dialect | A forward `bt X` with a `bnt` just before X takes that `bnt` as target. A forward `bnt X` with a forward, non-loop `bt` just before X takes that `bt`. (The `or` forms of this repo's compiler; both are equal targets by 3.1.) |

### 3.3 The scope parser

A recursive-descent parser walks the instructions in address order. Each
open construct is a scope: a sequence (its end), the then-part of an `if`
(also the else entry), a loop (continue, exit; the step of a `for`), a
switch (the `toss`). Each sequence has a known physical interval: no
search, no iteration. "Equal" means equal by 3.1, from the innermost scope
out. `p` is the branch position, `hi` the sequence end.

| Branch | Target | Result |
|---|---|---|
| `bnt X` | inside (p, hi] | `If`. A `jmp J` at X-1 is the else marker when the then-part is not empty, the `jmp` is not a loop latch, and J is inside (X, hi] or equal to the sequence end. If not, there is no else. |
| `bnt X` | equal to the sequence end | `If`, then = rest of the sequence |
| `bnt X` | equal to the else entry of the enclosing `if` | one more `and` term of that `if` |
| `bnt X` | equal to exit / continue of loop n | `If`, then = rest, else = `break n` / `continue n` |
| `bt X` | inside (p, hi] | `Or`, second operand = [p+1, X) |
| `bt X` | equal to the sequence end | `Or`, second operand = rest |
| `bt X` | equal to exit / continue of loop n | `breakif n` / `contif n` |
| `jmp X` | equal to exit / continue of loop n | `break n` / `continue n` |
| any | forward, in an enclosing loop, no match | `continue` of a `for`; X is its step. The loop is parsed again with the step as a scope. |
| any | no match | the function falls back to `asm` |

The parser makes no presentation choice.

### 3.4 Verify

From the region tree, emit the branch skeleton with the plain templates.
For each live non-branch instruction, compute the successors (`Next`,
`Exit`, `Cond(onTrue, onFalse)`) with the functions of 3.1, for the
skeleton and for the bytecode. They must be equal, and each live
instruction must be in the tree one time. If not: `asm`.

### 3.5 Forward value stage (replaces the backward walk)

An evaluator walks the region tree forwards with a symbolic stack, an
accumulator and a statement list. It builds the same `ConsumptionNode` /
`ChunkType` tree that `_CodeNodeToSyntaxNode` reads today.

- **Instruction:** `_GetInstructionConsumption` gives the operands. The
  node takes its stack operands (in push order), then the accumulator
  node, as children (address order, as today).
- **Statement:** a node that goes to the accumulator is put at the end of
  the statement list. A consumer removes it. A node that stays is a
  statement.
- **Facts:** the accumulator and the stack top each have a fact (immediate
  n, plain variable, property, self, unknown), with the set and reset
  rules of `OPTIMIZE.CPP` (a label resets; a branch does not; a store to a
  plain variable sets the fact). When a consumer needs the accumulator and
  its node is used already, or was made before one of the consumer's own
  stack operands, the operand is a copy made from the fact. With no fact:
  `asm` with a stable id. There is no search. Where the optimiser believes
  more than the machine (a store or an increment to the stack leaves the
  accumulator alone), the facts follow the machine; a store to a property
  keeps the fact, as in the optimiser.
- **Region:** `if`, `and`, `or`, `switch` and loop regions are evaluated
  with the stack carried through. The value of a region is the
  accumulator at its end. The then-part starts with the facts of the
  branch; the code after a join starts with no facts.
- **Switch:** the head is the stack top. `dup` at the switch depth gives a
  marker; `eq?` takes the marker and the accumulator as the case value.
  Other `dup`: a copy of the stack top (also a value pushed before the
  sequence: the `dup` does not take it). `pprev`: the accumulator operand
  of the previous compare (n-ary).
- **Dead code:** dead code right after a `ret` stays as statements, with no
  fact; other dead code gives no statement; a structure that no path
  reaches is dead code. A dead `break` or `continue` is a statement only when
  a path of the tree goes through it (verify records these).
- **Presentation** (`and` or nested `if`, `while` or `repeat`, shortest
  then-part, `contif`): decided here, from facts that the evaluator has
  (a sequence is one value; a test is the first thing in a loop). The
  forms: a loop whose body is one `if` with an `else` `break` is a `while`
  of its test (the AST passes make an `and` of a second such test); a
  `bt` latch is a `do` loop; a loop with a step is a `for`; a `bt` to a
  loop exit or continue point is `breakif`/`contif`; an `if` with an
  empty then-part and an `else` `break` is `(breakif (not c))`; an `or`
  whose second operand has statements is, as a statement, `(if (not c)
  X)` (this compiler has no statement list as an expression). An `if`
  whose `else` marker goes to the continue point stays an `if`/`else`:
  the bytecode cannot tell it from `(if c X (continue)) Y`.

Invariants, each a failure to `asm`: each instruction is in the chunk tree
one time (copies marked); the stack is balanced at each statement end and
region exit; each accumulator read has a node or a fact; children are in
address order.

This removes `CodeChunkEnumContext`, `EnumerateCodeChunks`, the deferred
consumer, the placeholders and the passes `_ResolveNeededAcc`,
`_ResolveDUPs`, `_ResolvePPrevs`, `_FixupSwitches`,
`_RestructureCaseHeaders`, `_LiftOutFromConditions`, `_RemoveTOSS`.

### 3.6 Meaning check (step 12)

The verify stage checks the region tree, not the text. The presentation
forms and the AST passes come after it, and three defects of milestone 2
gave text with another meaning (a dead `break` as a statement, dead code
after an exit as statements, `(break 2)` printed as `(break)`). They were
found only by reading the diffs against Snuffer. The bytecode oracle of
the tests (`TestBytecodeOracle`) checks idempotence: a stable text with
another meaning passes it.

The meaning check compares a function of the original bytecode with the
same function of the bytecode that the compiler makes from the decompiled
text. The bytes differ (dialect, idioms, layout), so the check compares
an effect graph of each:

- **Effects:** a symbolic walk of the instructions, as in the value stage,
  makes an expression tree for each value. A node of the graph is an
  effect: a call (`call*`, `send`, `self`, `super`) with its target and
  arguments, a store or an increment of a variable or a property (with its
  index and value), a `ret` with its value (when the function returns a
  value), and a test (a conditional branch on its value). The value of a
  call is a reference to its node.
- **Normal forms:** `jmp` chains and threading resolve away; `bt x` is
  `bnt (not x)`; a deleted load, a `push` of the accumulator, `pushi`,
  `dup` and `pprev` give the same expressions as the plain loads; a
  `switch` gives compares of the switch value; `toss`, `link`, line
  numbers and a `send` with no message are no effects. Variables compare
  by kind and index (in script 0 a local is the global of its index),
  properties and selectors by number, objects by name, strings by text; a
  `lofsa` of a class of the script is the value of `class`. The values:
  - a load gives the value of the last store to the variable on the path,
    and the truth of that value when a test on the path read it (so a
    load and a reused value are one value); a loop head drops these
    values (a value of the last turn is not the same value);
  - a store or a call changes the variables that it can change: a value
    of the old variable is a value of its own (`x@<effect>`);
  - the operands of `+`, `*`, `&`, `|` and `^` are one list, their
    numbers fold into one, and an operation on numbers folds;
  - a test is no effect when the path knows its truth, when it tests a
    number, or when its two outcomes get to the same next effect in the
    same state (with no accumulator where no instruction reads it);
  - a loop head keeps the stack depth of the code model: a `continue` out
    of a switch leaves values on the stack that no instruction reads.
- **Compare:** a bisimulation of the two graphs from the entry: matched
  nodes are the same kind of effect with equal expressions, and their
  successors match for each outcome of a test.
- **Report:** SAME, DIFF (with the first place that differs), or
  UNCOMPARED with a reason: the recompile of the script failed
  (`not-recompiled`), the version is SCI2 or later (the compiler makes
  SCI0 to SCI1.1), the code runs past the end of the function (its end
  was found wrong), or a form the check does not read (a stack underflow,
  a `send` frame, too many paths, a very large value). The gate joins
  the rows with the function report: DIFF of a function that the scope
  engine gives is a defect, or a defect of the names or of the compiler
  that the open items list.

Where it runs:

- Unit tests: each fixture of `TestDecompileScope` also compares the
  meaning of the decompiled and recompiled function with the compiled
  fixture. CI has no games, so this is the check that CI runs.
- Corpus: a hidden `scic dev compare-meaning <game> <compiled folder>`
  command (the folder of `scic script compile --all --out-dir <folder>
  --raw`), and `DecompileGate.ps1 -Meaning`, which decompiles each game
  copy with the engine of the run, compiles it into a folder and compares
  each function of the scripts of the run. Gate rule 7 (section 6).

## 4. Evidence

Hand simulation on real functions that fail today (bytecode from
`I:\tmp\dcsweep\run2\src`, expected source from `snuffer1\out`), by the
author and by two independent reviews whose task was to break the design:

- Parser: 22 hard functions from 14 failure classes: **no wrong tree**. 16
  were correct with the first rule set; the others needed general
  refinements, now in 3.2 and 3.3. Two go to `asm` correctly.
- Parser: about 30 nesting combinations of the `sc` templates: none
  defeats the resolve-and-match core. The fixtures parse (`F15` with the
  loop split, `N1`/`N2` with the n-ary pass); the negative check
  `X_SharedThenBranch` fails as it must.
- Value stage: the rules give the Snuffer result on the examined value
  defects: first case that reuses the accumulator (LB2 `iconMode::doit`),
  case value with a branch and a deleted load (PQ3 `rm036`), value `if`
  and `switch` as call arguments with a reused `ldi` (SQ4
  `driveCloseUp`). This stage has no independent review yet; step 9
  starts with fixtures from these functions.

## 5. Steps

Each step is one atomic PR (stacked), with tests that fail before and pass
after. New files in `SCICompanionLib\Src\Compile\`, registered in
`SCICompanionCore\SCICompanionCore.vcxproj` and `.filters`; tests in
`UnitTests\UnitTests.vcxproj` and `.filters`. CRLF. No copyright headers.

### Milestone 0: measure

| # | PR | Main changes | Exit |
|---|---|---|---|
| 1 | Engine switch and function report | `DecompileEngine { Classic, Scope, ScopeThenClassic }` on `DecompileOptions` (`DecompileBatch.h:20`) and `DecompileLookups`; `scic script decompile --engine classic\|scope\|auto`; `SCIC_DECOMPILE_ENGINE`; `IDecompilerResults::InformFunction` (at `DecompilerCore.cpp:1321`); `--function-report <tsv>`. No GUI change. | Classic output byte-identical; `TestCli` cases. |
| 2 | Compare tool and corpus gate | Move `UnitTests\StructuralCompare.*` to `Src\Compile`; hidden `scic dev compare-structure` (SAME / NAMES / SHAPE / DIFF; with `--baseline`: FIXED / CHANGED / REGRESSED); `UnitTests\Tools\Corpus.Common.ps1` + `DecompileGate.ps1` (from `CliCorpusSweep.ps1`; cached Snuffer run); `UnitTests\Files\Corpus\gate-sample.json` (the scripts of the known failures and 5 random scripts of each game); `UnitTests\Files\Corpus\gate-baseline.json` (counts only) | Baseline of the Classic engine recorded on the sample. The full corpus (`-Full`) runs only at steps 8 and 13 (owner decision, 2026-09-30: a full run for each step is too slow; CI has no games). |

### Milestone 1: control flow, in shadow mode (output does not change)

| # | PR | Main changes | Exit |
|---|---|---|---|
| 3 | Code model | `ScopeCode.h/.cpp` (about 350 lines): index array over the `std::list<scii>`, labels, live code, `resJ`/`resT`/`resF`, depth profile, n-ary and dialect passes | `TestScopeCode.cpp` on asm fixtures. |
| 4 | Region tree and verify | `ScopeRegion.h` (tree, text dump), `ScopeVerify.h/.cpp` (about 650 lines) | `TestScopeVerify.cpp`: 7 shapes in 3 dialects give one normal form; right trees pass; negative checks (swapped arms, dropped leaf, wrong break level, break to the `toss`, truth folding off) are rejected with the address. |
| 5 | Parser: sequences, `if`, `and`, `or` | `ScopeParser.cpp` part 1 | Region dumps of `F3_*`, `F13_*`, `F17`, `B1`, `N1`, `N2`, `C1`, `P1`; V passes on each. |
| 6 | Parser: loops | back branches, `break n`/`continue n`, `for` step, do-while, shared-head split | `F1`, `F4_*`-`F6`, `F9`-`F12`, `F14`-`F16`, `F18`-`F20`, `P2`. |
| 7 | Parser: switches | depth-based cases, case values, empty last case | New `S1_SwitchValue`, `S2_CaseValueBranch`, `S3_EmptyLastCase`. |
| 8 | Shadow run and fixes | `DecompileRaw` runs parser + V for each function and reports `scope: ok` or `scope:<stage>:<id>`; one batch of general rule fixes from the corpus | **Decision point.** Target: V accepts each function that Classic decompiles, and at least 90% of the graph-stage failures. If not reached: port the `sci-tools` loop and branch analysis as a second producer of the same region tree; steps 4 and 9-14 do not change. |

### Milestone 2: values

| # | PR | Main changes | Exit |
|---|---|---|---|
| 9 | Value stage: code, facts, `if`/`and`/`or` | `ScopeValues.h/.cpp` part 1 (about 600 lines); `OutputNewStructure` overload that takes the region tree; the four invariants; `ChunkType::Break`/`Continue` level | New fixtures from real functions (accumulator reuse across `bnt`, first case reuse, value `if` as an argument); loopless fixtures pass for both engines (`FIXTURE_TEST` macro, `TestDecompile` + `TestDecompileScope`). |
| 10 | Value stage: loops, switches, n-ary | part 2 (about 400 lines): switch head and case values, `dup`, `pprev`, `&rest`, loop tests, a region as a value | All fixtures pass with `scope`; `TemplateGame_FallbackBaseline` = 0 and `_Recompiles` with `scope`. |
| 11 | Presentation and corpus fixes | Forms (`and`, also from nested `if`s whose `else` is one `break` or `continue`, as the parser gives a `while` test; `while`/`do`/`for`; `breakif`/`contif`, also from an `or` over the rest of a loop body; else marker; statement list as an operand; the dead code after an `exit`); missing AST passes if the compare shows a need; batches by compare verdict | Per function: not worse against Snuffer than Classic. Template snapshot differences reviewed. |

### Milestone 3: switch

| # | PR | Main changes | Exit |
|---|---|---|---|
| 12 | Meaning check and gate fixes | The meaning check (section 3.6): the effect graph, the compare, the fixture assertion in `TestDecompileScope`, `scic dev compare-meaning`, `DecompileGate.ps1 -Meaning`; then the defects that the gate and the meaning check show | Negative checks: the three defects of milestone 2 (a dead `break` as a statement, dead code after an exit, `(break 2)` printed as `(break)`), put back one at a time, each give DIFF in a fixture and on the corpus. Gate rules 1-7 pass on the sample (section 6). |
| 13 | Default = `scope` | New snapshot baseline after review; README "What's new" | `RunTests.ps1 -All` passes. |
| 14 | Remove the old stages | Delete `ControlFlowGraph.*`, `ControlFlowNode.*`, `TarjanAlgorithm.*`, `ControlFlowGraphViz.*` (about 4,500 lines); in `DecompilerNew.cpp` the backward walk and its passes (about 2,200 lines); in `DecompilerCore.cpp` `_RemoveDeadBranches`, `_ObtainInstructionSequence`, the shared-head retry | Build clean; `CheckFailureHandling.ps1 -Update`. |

New code: about 3,000 lines. Removed: about 6,700 lines.

Failure model: one exception type `ScopeError : sci::DataError`
(`Unsupported`, a stable id such as `no-scope-for-target` or
`acc-no-fact`, the offset), caught at each stage entry with `sci::Guard`;
the message `[scope:<stage>:<id>]` goes to the function report.

## 6. Gate for step 13

From `DecompileGate.ps1 -Check`:

1. REGRESSED = 0 (a function that was source and is now `asm`).
2. The `asm` count is not higher in any game and lower in total.
3. No crash, `[internal]` or timeout rows.
4. `RunTests.ps1 -All` passes with `SCIC_DECOMPILE_ENGINE=scope`.
5. Against Snuffer, where both are source: SAME or NAMES, or an allowlist
   entry with a category (allowlist outside the repo).
6. A function whose text changes from Classic: the verify and invariant
   checks pass, and its verdict against Snuffer is not worse.
7. (with `-Meaning`) No function that the scope engine gives as source
   has DIFF in the meaning check (section 3.6); each UNCOMPARED has a
   reason.

**Owner decision (2026-10-01) on rule 6:** two kinds of change are not
worse: names that change because a function that was `asm` is now source
(the name guess reads its assignments), and `(if c X else Y)` where Snuffer
has `(if c X (continue)) Y` (the bytecode is the same).

## 7. Risks

- The parser rules came from hand simulation. Step 8 measures them on the
  whole corpus before any output changes, and has a defined exit.
- The value stage has no independent review yet. Its rules come from the
  optimiser source; the invariants stop a wrong result; the compare tool
  shows each text change.
- The text of functions that decompile today can change (86 template
  snapshots, the QfG4 compare). Gate rule 6 controls this.
- The meaning check is new code: a fault in it can give SAME for a wrong
  text. Its negative checks (the three known defects put back) show that
  it finds what the Snuffer diffs found; its own normal forms have unit
  tests on asm pairs that mean the same and that do not.
- Forms that the bytecode cannot tell apart (`(repeat (while …))` or
  `if`/`continue`; `breakif` or `(if c (break))` in fan code) follow the
  Snuffer compare.

## 8. Way of working

- Stacked, atomic PRs from `claude/decompiler-control-flow-heuristics-594d31`.
- One adversarial review per step on the same checkout (Opus for steps 4
  to 7, 9 and 10; Sonnet for the mechanical steps 2 and 14).
- A new session at each milestone. Open items in
  `docs\decompiler-scope-parser\open-items.md` (open items only).
- First action after approval: save a memory pointer to this plan.

## 9. Verification

```
MSBuild.exe SCICompanion.sln -m -p:Configuration=Release -p:Platform=Win32 -p:VcpkgManifestInstall=false
.\UnitTests\RunTests.ps1
$env:SCIC_DECOMPILE_ENGINE = 'scope'; .\UnitTests\RunTests.ps1 -All
.\UnitTests\Tools\DecompileGate.ps1 -Library F:\Games\Sierra,F:\games\gog -Exclude '_vgm*' -Snuffer E:\Code\Cs\sci-tools\Snuffer\bin\Release\net10.0\Snuffer.exe -Work I:\tmp\scic-gate -Engine classic
.\UnitTests\Tools\DecompileGate.ps1 -Library F:\Games\Sierra,F:\games\gog -Exclude '_vgm*' -Snuffer E:\Code\Cs\sci-tools\Snuffer\bin\Release\net10.0\Snuffer.exe -Work I:\tmp\scic-gate -Engine scope -BaselineRun <run folder of the classic run> -Allowlist <allowlist> -RequireFewer -Meaning -Check
```

- Per step: the tests of the step table, with a negative check for each
  rule.
- Steps 8, 11 and 12: the corpus gate on the sample, compared with
  `gate-baseline.json`. Steps 8 and 13 also run it with `-Full`. From
  step 12, the gate runs with `-Meaning` too.
- The corpus folders are read-only: the scripts copy each game first.
