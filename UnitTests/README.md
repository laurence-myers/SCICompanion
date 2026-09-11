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

| Fixture | Script | Family | Warning it reproduces |
|---|---|---|---|
| `D0_Plain` | 901 | (smoke) | none; decompiles clean |
| `F1_LoopHeadContinue` | 900 | 1 | Inconsistent then/else branches |
| `F5_EmptyLeadingWhile` | 905 | 5 | Unable to replace node in follow nodes |
| `F6_EmptyTrailingFor` | 906 | 6 | Can't find follow node for structure |
| `F7_UnknownClass` | 907 | 7 | Unexpected opcode (class 40) |

Each family test pins the current (broken) behaviour: the function falls back
to assembly with the family's warning. When a fix lands, flip the block marked
`PART B` to assert a clean decompile.

`TemplateGame_FallbackBaseline` guards against new fallbacks. The template game
has 7 known fallbacks in 5 scripts (ScrollableInventory, SaveRestoreDialog,
Controls, Gauge, System). Lower `BASELINE` when a fix removes some.

### Families not covered yet

Families 2, 3, 4 and 8 are not covered. A minimal asm fixture does not
reproduce them. The decompiler resolves the simplified shape. These bugs need
the enclosing structure of the larger original functions. Author those fixtures
with their fix. Check each fixture against the real function.

## Other tests

`TestCompile`, `TestClassBrowser`, `TestResource*`, `TestPolygonLoad`,
`TestPicDraw`, and `TestAllGamesLoad` predate this work. Some fail for reasons
unrelated to the decompiler (an SCI0 compile exception and pic pixel diffs
against stored bitmaps), so the default `RunTests.ps1` run skips them.
