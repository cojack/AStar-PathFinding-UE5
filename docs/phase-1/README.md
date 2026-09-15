# Phase 1 — Blueprint exposure of the A\* component

**Status:** DONE
**Last updated:** 2026-09-15

Expose everything the existing A\* implementation can do to the Blueprint Editor, without
breaking the Blueprints that already bind to it.

## Checklist

- [x] Freeze and document the existing Blueprint-facing symbols ([ADR-0002])
- [x] Replace `TArray<S_Cell*>` with `TArray<FPathCell>` linked by index ([ADR-0001])
- [x] Fix the cell leak (`new S_Cell` was never deleted) — falls out of the above
- [x] `EPathCellState` enum replacing `ColorNum` 0–4 ([ADR-0003])
- [x] `EPathStepResult` enum so Blueprint can see what a step did
- [x] `FPathCell` as a `USTRUCT(BlueprintType)`
- [x] `FIntPoint` as the coordinate type ([ADR-0004])
- [x] Expose movement costs: `StraightCost`, `DiagonalCost`, `bAllowDiagonal`
- [x] Manhattan heuristic fallback when diagonals are disabled (admissibility)
- [x] Expose all five state colours
- [x] Expose debug-draw toggles: cells, weights, parent arrows, start/end labels, height, scale
- [x] Expose `bShowDebugMessages`
- [x] Grid-coordinate editing: `ToggleWallAt`, `SetWallAt`, `ToggleBeginEndAt`, `SetStartCell`, `SetEndCell`
- [x] Running: `StepOnce`, `SolveAll`, `RebuildGrid`
- [x] Queries: `IsValidCoord`, `GetCellState`, `GetCellData`, `GetFinalPath`, `GetFinalPathWorld`, `GetStatus`, `GetStartCoord`, `GetEndCoord`, `GetStateColor`
- [x] Conversions: `WorldToGrid`, `GridToWorld`
- [x] Events: `OnPathFound`, `OnPathFailed`, `OnStepped`
- [x] `APathFindingGrid` Blueprintable actor ([ADR-0008])
- [x] Upgrade to UE 5.8 ([ADR-0005])
- [x] Automation tests covering the algorithm ([ADR-0009])
- [x] `.gitignore` for build artifacts
- [x] `CLAUDE.md`
- [x] Behaviour checks actually executed — via the commandlet in [ADR-0014], not the
      automation harness, which still cannot run headlessly here

## Log

**2026-09-15**

- Read the existing implementation end to end before touching it. Confirmed by `strings` on
  the `.uasset` files which C++ symbols `BP_Player`, `WBP_PathFinding` and `MainMap` bind to,
  which set the additive-only constraint for the whole phase ([ADR-0002]).
- Verified against engine source rather than assuming: `FIntPoint` is
  `USTRUCT(BlueprintType)`, `EngineIncludeOrderVersion.Unreal5_8` and
  `BuildSettingsVersion.V7` exist, `EAutomationTestFlags` is an enum class in 5.8.
- Found two real bugs while porting, both fixed and recorded: an exhausted open set never
  terminated the search ([ADR-0006]), and a goal adjacent to the start reported "No path was
  found" ([ADR-0007]).
- First build **failed** — not on the code. `BuildSettingsVersion.V5` overrides warning
  levels that an installed 5.8 engine forbids, because the project shares build products with
  `UnrealEditor`. Bumping the target shims to `V7` / `Unreal5_8` fixed it ([ADR-0005]). The
  earlier decision to leave them alone was wrong and is recorded as such.
- Second build succeeded: `Result: Succeeded`, 9 actions, no warnings from project code.
- `-run=CompileAllBlueprints`: 574 Blueprints, **0 errors, 0 warnings, 0 failed to load**.
  `BP_Player` and `WBP_PathFinding` both compiled, proving the frozen contract held.
- Automation tests could not be run. `Automation RunTests` stalls after
  `FindWorkersResponseMessage` at 300s and at 900s. Ran a stock engine test
  (`WorldMetrics.TestZeroState`) the same way as a control — it stalls identically, so the
  cause is the harness in this environment, not this project. The test is registered and
  discoverable (`'AStarPathFinding.Core'` is listed among 6121 tests). Left unticked deliberately:
  written and compiled is not the same as passing.

## Deliberate simplifications

- `SelectLightestCell` scans the open set linearly instead of using a heap. Marked with a
  `ponytail:` comment naming the upgrade path. Fine for editor-sized grids; phase 5 replaces it.
- The visualiser still mutates the grid while searching. Correct for a single stepped search,
  and the reason phase 3 exists ([ADR-0012]).
