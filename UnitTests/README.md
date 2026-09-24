# SCI Companion unit tests

These tests use the Microsoft C++ Unit Test Framework. The project builds a
test DLL (`UnitTests.dll`) that `vstest.console.exe` runs.

## Build and run

Build the solution, then run the tests:

```
MSBuild.exe SCICompanion.sln -m -p:Configuration=Release -p:Platform=Win32
.\UnitTests\RunTests.ps1
```

`RunTests.ps1` finds `vstest.console.exe` with `vswhere`, runs the DLL, and
writes `TestResults\UnitTests.trx`. By default it runs the whole suite, so CI
cannot silently skip a test. Pass `-Filter "FullyQualifiedName~..."` to run a
subset locally; `-All` is kept as an explicit "everything" override. Pass
`-UpdateSnapshots` to accept a deliberate change in decompiler output (see
Snapshots below).

Tests with `OptIn` in their name need an input that CI does not have (for
example `OptIn_Oracle_ExistingGame` needs `SCICOMP_ORACLE_GAME` set to a real
game's `resource.map` folder). They fail when that input is missing, so no run
includes them unless you ask with `-Filter "FullyQualifiedName~OptIn"`. A green
result therefore always means the test really ran.

The test DLL and its data land in the build's output folder (`Release`, or
`Debug`) next to `SCICompanion.exe`. The app post-build copies the template
game, the include headers, and the decompiler config there. The test
post-build copies the fixtures to `Release\TestFiles`.

GitHub Actions builds the solution and runs `RunTests.ps1` in `build.yaml`.

## Test levels

The decompiler work is guarded at three levels, all without Sierra game data.

**Unit (`TestAstPasses.cpp`).** Parses a small Sierra-syntax procedure with
`AstPassHelper`, runs the AST passes on it, and compares the printed text. No
bytecode. `ParseSierraScript` and `ScriptToText` parse and print; `WrapProcedure`
wraps a body; `ApplyAllPasses` parses, runs the passes, and returns normalized
text.

**Integration (`TestDecompile.cpp` fixtures).** `DecompileHelper` compiles a
fixture, decompiles it, and checks the result:

- `CompileFixture` compiles a fixture script into the temporary game.
- `DecompileToText` decompiles a compiled script to source text and
  diagnostics.
- `DecompileAndRoundTrip` compiles, decompiles, recompiles, and decompiles
  again, asserting the two decompiles match (round-trip stability).
- `AssertDecompileMatchesExpected` also compares the decompiled text with a
  committed `<fixture>.expected.sc` oracle (fidelity, not just stability).

`TestDecompileBatch.cpp` drives `DecompileBatch` (the Decompile dialog's path:
decompile every script once, name the globals across all of them, then write)
over two fixtures whose global names depend on each other, and checks the
stale-script scan the dialog uses afterwards.

**Regression (`TestDecompile.cpp` template guards).**

- `TemplateGame_FallbackBaseline` counts assembly fallbacks across the template
  and asserts the set of failing scripts against an allowlist.
- `TemplateGame_Recompiles` decompiles every template script, writes all
  sources, and recompiles them, asserting each one compiles (allowlist for two
  pre-existing defects). `CountFallbacksAllScripts` and
  `RecompileAllDecompiledScripts` back these.
- `TemplateGame_Snapshot` compares the decompiled text of every template script
  with a committed snapshot (see below).

## Snapshots

`Files\Decompile\Snapshots\SCI1.1\<title>.sc` holds the decompiled text of every
template script. `TemplateGame_Snapshot` fails on any change. To accept an
intended change, review it, then run:

```
.\UnitTests\RunTests.ps1 -UpdateSnapshots
```

which reruns the snapshot test (it writes actuals to `Release\SnapshotActuals`) and
copies them into the committed folder. Commit the snapshot change with the code
change so the review shows exactly what moved. `Tools\CompareDecompile.ps1`
diffs two folders of `.sc` files, in exact mode (snapshot review) or structural
mode (the QfG4 golden diff, which ignores names and formatting).

## Fixtures

Each fixture in `Files\Decompile\SCI1.1` reproduces one bytecode shape. Most
use a hand-written `(asm ...)` block that matches Sierra's exact bytecode,
because SCI Companion's own compiler emits a different branch dialect. The
`C1`/`P1` fixtures are plain source compiled by SCI Companion, so they cover
that dialect. A fixture with a `<name>.expected.sc` file is pinned to that
text (after whitespace normalization) by `AssertDecompileMatchesExpected`. The
expected text is the Sierra shape, not whatever the tool emitted. When an
expected file is missing, the test writes the actual to
`Release\SnapshotActuals\Expected` so it can be reviewed and committed.

| Fixture | Script | Family | Status |
|---|---|---|---|
| `D0_Plain` | 901 | (smoke) | decompiles clean |
| `F1_LoopHeadContinue` | 900 | 1 | fixed; reconstructs the if |
| `X_SharedThenBranch` | 903 | (none) | non-Sierra shape; must fall back cleanly |
| `F4_BreakElseEdge` | 904 | 4 | fixed; pinned text |
| `F5_EmptyLeadingWhile` | 905 | 5 | fixed; reconstructs both loops |
| `F6_EmptyTrailingFor` | 906 | 6 | fixed; reconstructs the loops |
| `F7_UnknownClass` | 907 | 7 | class stays as asm; clear message |
| `C1_ValueAndOr` | 908 | (compiler) | value and/or round-trips |
| `C2_IndexedMathAssign` | 920 | (compiler) | indexed `+=` compiles to Sierra's sequence; used as a value it gives the new value |
| `C4_ClassDefRealClass` | 934 | (compiler) | a classdef with a real class's species keeps the selector check (must not compile) |
| `C5_IndexerSideEffect` | 935 | (compiler) | an indexer with a side effect in an indexed `+=` gets a warning |
| `C3_SierraIndexedMathAssign` | 932 | (chunk stage) | Sierra's own `lati; push` sequence for an indexed `+=` folds back |
| `F3_ValueIfReturn` | 909 | 3 | fixed; `(return (and a b))` |
| `F3_OrThreeTerms` | 910 | 3 | fixed; n-ary or |
| `F3_OrAndOr` | 911 | 3 | fixed; needs the branch deoptimizer |
| `F3_AndOr` | 912 | 3 | fixed |
| `F3_IfValueWithElse` | 913 | 3 | fixed; `(= x (if c 1 else 2))` |
| `F3_AndAsArgument` | 914 | 3 | fixed; and as a call argument |
| `F4_WhileAnd` | 915 | 4 | fixed; else-break folded into the test |
| `F4_WhileOr` | 916 | 4 | fixed; or as the loop test |
| `P1_CompoundConditions` | 917 | (compiler) | SCI Companion dialect; text equals source |
| `F8_AssignBeforeCondInRet` | 918 | 8 | fixed; statement lifts out of a value if |
| `F8_DeadValueStatement` | 919 | 8 | fixed; dead value becomes a bare statement |
| `F9_BreakInSwitchCase` | 921 | (structurer) | fixed; break out of a loop from a switch case |
| `N1_ChainedCompare` | 923 | (n-ary) | fixed; `(< 0 x 19)` is built from its pprev at consumption, a send in the middle included |
| `N2_SierraChainedCompare` | 933 | (n-ary) | fixed; Sierra's own chain shape with a variable last; the chain's `bnt` is neutralized only between two compares of the same operator |
| `F10_MidBodyContinue` | 922 | (structurer) | fixed; a mid-body `jmp head` is a `(continue)`, written as an if-else by `IfContinueRefactor` |
| `F11_LatchTrampoline` | 924 | (structurer) | fixed; a shared `jmp head` folds into the common latch |
| `P2_CondInLoop` | 926 | (compiler) | SCI Companion dialect; nested conds in a loop round-trip stably |
| `F12_BreakJoin` | 925 | (structurer) | fixed; a break edge into a shared statement moves to the if's follow |
| `R1_ReturnShapes` | 927 | (returns) | fixed; an if whose branches return is not returned, a value if at the end is, a `++` is not a return value |
| `A1_ReusedAcc` | 928 | (chunk stage) | fixed; a store whose value a later send reuses stays a statement |
| `B1_DeadBranch` | 929 | (fixup) | fixed; a `bnt` right after a `bnt` to the same target is deleted (`_RemoveDeadBranches`) |
| `A2_ReusedSelector` | 930 | (chunk stage) | fixed; a selector pushed as `push` after an `ldi` of its number, and a `dup` argument |
| `F13_ValueIfArgument` | 931 | (chunk stage) | fixed; a send whose arguments are value ifs, with the selector and earlier arguments pushed before the if; an if test that reuses the accumulator hands those pushes to the send (`deferred`) |
| `F14_BreakPastLatch` | 936 | (structurer) | fixed; a loop whose break jumps past its latch ends at its follow node, so the loop after its latch (in its else) is built first; King's Quest V script 755 |

`TemplateGame_FallbackBaseline` guards against new fallbacks. The template game
started with 7 known fallbacks. The Family 1 and Family 6 fixes each removed
one, the branch structurer removed one more (System's `InRect`), and the
loop-body fixes (latch trampolines, tail breaks, ret-only loop exits) removed
the last four. The baseline is 0: every template script decompiles.

### How Families 3 and 4 are fixed

The decompiler structures branches from the immediate post-dominators
(`ControlFlowGraph.cpp`, `_StructureAllBranches`): a `bnt` becomes an if whose
follow is the post-dominator, a `bt` becomes an or, and an and is only made
where an outer `bnt` shares an inner if's else. Nothing synthesizes a `not`.
Two instruction fixups run first: `_UnchainBtToBnt` maps SCI Companion's
`bt <then>` onto Sierra's `bt <join bnt>`, and `_DeoptimizeBtChains` restores
the join that Sierra's optimizer bypasses in `(or P (and Q R))`. A `bnt` to the
loop exit inside a loop body becomes an if with a synthesized else-break
(`_SolveLoopBranches`). The chunk stage treats an if as a value. AST passes
(`DecompilerAstPasses.cpp`) then give the idiomatic text: nested and
value-position ifs become `and`/`or`, loop cleanup folds the breaks, double
nots collapse in boolean context, and `(= a (+ a b))` becomes `(+= a b)`.
`TestAstPasses` covers the passes on parsed source, with no game data.

Return values (`R1_ReturnShapes`): a function returns the accumulator, so
whether a statement before a `ret` is the return value is a judgement. The
`ReturnCleanup` pass (last in `DecompilerAstPasses.cpp`) follows the golden
decompilations: a returned loop, or if/switch with a return inside, is
unwrapped; a value-shaped statement that the final ret follows is returned;
a final bare `(return)` is dropped; `onMe`/`onTarget` always return a value;
`handleEvent`/`changeState`/`init` return only unmistakable values. The
`ReturnsValue` hint (`_DetermineIfFunctionReturnsValue`) no longer counts a
`++`/`--` before a `ret`.

Reused accumulator (`A1_ReusedAcc`): Sierra's compiler drops the load of a
send target or pushed argument when the accumulator already holds that
variable from a store before the pushes. A send evaluates its target after
its arguments, so at chunk enumeration (`EnumerateCodeChunks`,
`ReusesAccumulator`) a generator met after all the stack operands is an
earlier statement, and the send gets a `NeedsAccumulator` that resolves to a
load of the variable. `aTop` joined the short-circuit set (a reused property
store reads back as `pToa`).

Value shapes (`TestAstPasses`, `CopyValue_*`, `IfToAnd_ValueContext*`,
`Loop_*`): `CopyValue` gives a value if with an empty then the tested
variable as its then (`(= x (if a a else b))`, `(= x (if (= t y) t else b))`,
and a value `(or a b)` with a variable first takes the same form), as
Sierra's compiler did not load the variable again. `IfThenToAnd` sees a
value context through the branches of a value if (a cond case body that is
one if becomes an and), never folds a cond case itself, keeps an assignment's
own if, and leaves a branch with a return inside alone. In a loop,
`IfContinueRefactor` turns an if whose then ends in a continue (body level)
or a break or return (any depth) followed by more statements into an
if-else, and `ContinueTrim` drops a continue at the end of the body. The
chunk stage clones a reused plain load instead of stealing it, so a stray
number before a send that reuses it stays a statement (`A1_ReusedAcc`).

The structural compare keys exported procedures by export slot (from the
public block, or a `proc<script>_<slot>` name) and local ones by ordinal,
and skips golden procedures marked `; UNUSED` (dead code the decompiler
never reaches).

Family 2 is fixed but has no isolated fixture; the baseline test guards it.
Family 8 (a statement before the test of an if that a `ret` consumes): the
lift pass in `DecompilerNew.cpp` (`SkipGuaranteedExecutions`) now climbs out
of the first operand of an instruction, because that operand runs before the
instruction. The statement moves to before the return.

`DiagnosticDumps::Dump_FailingTemplateScripts` is not in the default filter.
It decompiles named template scripts with the control-flow dump on, for
diagnosing a new fallback. Edit its title list, then run it by name.

### Golden diff against a real game

`DiagnosticDumps::Dump_ExistingGame` (also outside the default filter)
decompiles every script of an existing game, read-only, into a folder. It is
driven by environment variables, so no local path lives in the source:

```
$env:SCICOMP_DUMP_GAME  = 'F:\Games\GOG\Quest for Glory 4 - dev'
$env:SCICOMP_DUMP_OUT   = 'C:\dump\qfg4'
$env:SCICOMP_DUMP_NAMES = 'E:\Code\Esoteric\sci-scripts\qfg4-cd-dos-1.0\src'
vstest.console.exe Release\UnitTests.dll /Platform:x86 /TestCaseFilter:"FullyQualifiedName~Dump_ExistingGame"
$env:SCICOMP_COMPARE_EXPECTED = $env:SCICOMP_DUMP_NAMES
$env:SCICOMP_COMPARE_ACTUAL   = $env:SCICOMP_DUMP_OUT
$env:SCICOMP_COMPARE_OUT      = 'C:\dump\qfg4-compare'
vstest.console.exe Release\UnitTests.dll /Platform:x86 /TestCaseFilter:"FullyQualifiedName~Compare_Structural"
```

Keep `SCICOMP_DUMP_OUT` short: the test creates it with `CreateDirectoryA`,
which fails silently on a long path. `SCICOMP_DUMP_NAMES` names each output
file after the golden file with the same `(script# N)` header, so the
structural compare matches files by name. `<out>\_warnings.txt` lists every
fallback. The golden tree is sluicebox's output for QfG4; it differs in
variable names and formatting.

`DiagnosticDumps::Compare_Structural` (`UnitTests\StructuralCompare.cpp`) is
the structural compare. It parses both sides with the real parser, normalizes
each function with AST passes (`cond` to nested ifs, `for` to `while` with the
step at the end, unsigned compares to signed, then the decompiler's own passes
so nested ifs, `op=` and loop shapes converge, then every value, variable,
define, literal, send target and call name to one token; selector names stay)
and compares the printed bodies per function. It writes
`<out>\_structural.txt` (totals and the differing functions) and one
`<file>.<function>.diff.txt` per difference with both normalized texts. Two
unit tests in `TestAstPasses` pin it: golden style and SCI Companion style of
one function compare equal, and a real difference is reported by name.
`Tools\CompareDecompile.ps1` is now the exact text compare only, for
reviewing a snapshot change.

## Keyword codegen

`TestKeywordCodegen` proves the merged language extensions (`foreach`, `verbs`,
`&exists`) are pure sugar over standard Sierra bytecode. Each test compiles a
small script that uses the keyword and decompiles it; since the decompiler only
understands standard opcodes, a clean decompile with no assembly fallback and no
trace of the keyword is the proof. `&exists` additionally asserts byte-for-byte
equality with its `(> argc N)` expansion. These run in the default filter.

## Other tests

`TestCompile`, `TestClassBrowser`, `TestResource*`, `TestPolygonLoad`,
`TestPicDraw`, and `TestAllGamesLoad` predate this work. All of them now run by
default; the earlier SCI0 compile exception no longer reproduces (the full
suite is green). `TestAllGamesLoad` still needs a local game library and passes
vacuously ("Found no games") when it is absent.
