# `scic` CLI work: handover

Read this first in a new session. The design is in `docs/scic-cli/plan.md`.
Update this file in the same commit as each step.

## State

- Branch: `feat/scic-cli`, based on `master` at `0dc1fef5`. Not pushed.
- Current step: F1 (next to start).
- Baseline on `0dc1fef5`: the Release build passes with 34 warnings; the
  unit suite passes 217 of 217 tests in about 4 minutes.

## Progress

The steps are the PRs of plan section 9, in this order. There is one commit
for each step, and a follow-up commit if the review finds a problem.

| Step | Status | Commit | Review |
|---|---|---|---|
| F1 Result foundation | not started | | |
| A1 Deferred writes | not started | | |
| A2 Patch writer, size check | not started | | |
| B1 GameSession, core log | not started | | |
| B2 Script text loader | not started | | |
| B3 Engine on the session | not started | | |
| F2 Engine errors as values | not started | | |
| S1 Compile destination | not started | | |
| S2 CompileBatch | not started | | |
| S3 ScriptCatalog | not started | | |
| S4 DecompileRun | not started | | |
| C1 CLI project, `script list` | not started | | |
| C2 `script decompile` | not started | | |
| C3 `script compile` | not started | | |
| C4 CI and documents | not started | | |
| E1 Core library (optional) | not started | | |

## How to work a step

1. Implement the step. Keep CRLF line endings (AGENTS.md).
2. Build: `MSBuild.exe SCICompanion.sln -m -p:Configuration=Release -p:Platform=Win32`
   (full path: `E:\Apps\Dev\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`).
   Use `-t:Rebuild` after a change to a widely included header, or the stale
   precompiled header gives access violations in unrelated tests.
3. Test: `.\UnitTests\RunTests.ps1` (unit), `-Integration`, or `-Filter`.
   Read the counts in `TestResults\UnitTests.trx`.
4. Negative check: make the new test fail on purpose (revert the fix or
   break it), confirm it fails, then restore.
5. Commit the step with this file updated. Message: `<type>(scic): <step> <summary>`.
6. Adversarial review: one subagent in an isolated git worktree, told the
   exact commit SHA, told to refute the step, to build with warnings
   visible, and to run the tests. Fix real findings in a follow-up commit.
   Then remove the worktree (`git worktree remove --force`, `git worktree
   prune`) and its branch.

## Decisions and deviations from the plan

- The open questions Q4 to Q11 use the plan's recommendations.

## Next action

Start F1: vendor tl::expected v1.3.1 and add `Src\Core\Result.h` (plan
sections 6 and 9).
