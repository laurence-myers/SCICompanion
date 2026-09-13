# SCI Companion unit tests

These tests use the Microsoft C++ Unit Test Framework. The project builds a
test DLL (`UnitTests.dll`) that `vstest.console.exe` runs.

## Build and run

Build the solution, then run the tests:

```
MSBuild.exe SCICompanion.sln -m -p:Configuration=Kawa -p:Platform=Win32
.\UnitTests\RunTests.ps1
```

`RunTests.ps1` finds `vstest.console.exe` with `vswhere`, runs the DLL, and
writes `TestResults\UnitTests.trx`. By default it runs the decompiler suites
(`TestDecompile` and `TestAstPasses`). Pass `-All` to run every test. Pass
`-UpdateSnapshots` to accept a deliberate change in decompiler output (see
Snapshots below).

Only the **Kawa** solution configuration builds the test project. The test DLL
and its data land in the `Kawa` output folder next to `SCICompanion.exe`. The
app post-build copies the template game, the include headers, and the
decompiler config there. The test post-build copies the fixtures to
`Kawa\TestFiles`.

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

which reruns the snapshot test (it writes actuals to `Kawa\SnapshotActuals`) and
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
`Kawa\SnapshotActuals\Expected` so it can be reviewed and committed.

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
| `C2_IndexedMathAssign` | 920 | (compiler) | indexed `+=` compiles to Sierra's sequence |
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

`TemplateGame_FallbackBaseline` guards against new fallbacks. The template game
started with 7 known fallbacks. The Family 1 and Family 6 fixes each removed
one, and the branch structurer removed one more (System's `InRect`), so the
baseline is now 4. Lower `BASELINE` when a fix removes more.

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
vstest.console.exe Kawa\UnitTests.dll /Platform:x86 /TestCaseFilter:"FullyQualifiedName~Dump_ExistingGame"
.\UnitTests\Tools\CompareDecompile.ps1 -Expected $env:SCICOMP_DUMP_NAMES -Actual $env:SCICOMP_DUMP_OUT -Mode Structural
```

`SCICOMP_DUMP_NAMES` names each output file after the golden file with the
same `(script# N)` header, so the structural compare matches files by name.
`<out>\_warnings.txt` lists every fallback. The golden tree is sluicebox's
output for QfG4; it differs in variable names and formatting, which the
structural mode ignores.

## Other tests

`TestCompile`, `TestClassBrowser`, `TestResource*`, `TestPolygonLoad`,
`TestPicDraw`, and `TestAllGamesLoad` predate this work. Some fail for reasons
unrelated to the decompiler (an SCI0 compile exception and pic pixel diffs
against stored bitmaps), so the default `RunTests.ps1` run skips them.
