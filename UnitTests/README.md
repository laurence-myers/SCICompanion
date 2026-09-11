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
writes `TestResults\UnitTests.trx`. By default it runs only the decompiler
suite (`TestDecompile`). Pass `-All` to run every test.

Only the **Kawa** solution configuration builds the test project. The test DLL
and its data land in the `Kawa` output folder next to `SCICompanion.exe`. The
app post-build copies the template game, the include headers, and the
decompiler config there. The test post-build copies the fixtures to
`Kawa\TestFiles`.

GitHub Actions builds the solution and runs `RunTests.ps1` in `build.yaml`.

## Decompiler tests

`TestDecompile.cpp` checks the decompiler against the SCI1.1 template game. No
Sierra game data is needed. `DecompileHelper` provides the harness:

- `CompileFixture` compiles a fixture script into the temporary game.
- `DecompileToText` decompiles a compiled script to source text and
  diagnostics.
- `DecompileAndRoundTrip` compiles, decompiles, recompiles the decompiled text,
  and decompiles again. It asserts the two decompiles match, so every fixture
  survives decompile, recompile, and decompile.
- `CountFallbacksAllScripts` decompiles every template script and counts the
  functions that fall back to assembly.

## Fixtures

Each fixture in `Files\Decompile\SCI1.1` reproduces one QfG4 decompilation
failure. The fixture uses a hand-written `(asm ...)` block. The asm matches
Sierra's exact bytecode. SCI Companion's own compiler emits cleaner code that
decompiles fine, so a fixture must bypass the compiler with asm.

| Fixture | Script | Family | Status |
|---|---|---|---|
| `D0_Plain` | 901 | (smoke) | decompiles clean |
| `F1_LoopHeadContinue` | 900 | 1 | fixed; reconstructs the if |
| `F3_ValueJoin` | 903 | 3 | not fixed; pins the fallback |
| `F5_EmptyLeadingWhile` | 905 | 5 | fixed; reconstructs both loops |
| `F6_EmptyTrailingFor` | 906 | 6 | fixed; reconstructs the loops |
| `F7_UnknownClass` | 907 | 7 | class stays as asm; clear message |

A fixed family's test asserts a clean decompile. A not-fixed family's test pins
the current fallback (the warning plus an asm block) and proves the asm
round-trips. When a fix lands, flip the pinning block to assert a clean
decompile.

`TemplateGame_FallbackBaseline` guards against new fallbacks. The template game
started with 7 known fallbacks. The Family 1 and Family 6 fixes each removed
one, so the baseline is now 5. Lower `BASELINE` when a fix removes more.

### Families still open

- Family 2 (compound-condition early abort) is fixed, but has no isolated
  fixture. The disabled shape entangles with Family 3, so a minimal case fails
  for the Family 3 reason instead. The baseline test guards it.
- Family 3 (and/or value join) needs a new "condition value" structure so the
  materialised boolean is not mis-valued. `F3_ValueJoin` pins it.
- Families 4 (compound else-edge is the loop exit) and 8 (a discarded value
  before an if) are not covered. A minimal asm fixture does not reproduce them.
  The decompiler resolves the simplified shape. They need the enclosing
  structure of the larger original functions. Author those fixtures with their
  fix, and check each against the real function.

## Other tests

`TestCompile`, `TestClassBrowser`, `TestResource*`, `TestPolygonLoad`,
`TestPicDraw`, and `TestAllGamesLoad` predate this work. Some fail for reasons
unrelated to the decompiler (an SCI0 compile exception and pic pixel diffs
against stored bitmaps), so the default `RunTests.ps1` run skips them.
