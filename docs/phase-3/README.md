# Phase 3 — Re-entrant search core

**Status:** DONE
**Last updated:** 2026-09-15

Separate durable grid data from per-query search state, so a search reads the grid without
writing to it. Prerequisite for async queries, multiple agents, and every algorithm after
A*. See [ADR-0012].

## Checklist

- [x] `FPathGrid` — durable data only (dimensions, passability), no search state
- [x] `FGridPathQuery` — start, goal, diagonal flag, costs, and the distance policy
- [x] `FGridSearch` — per-query buffers (`g`, `f`, parent, membership, open/closed sets)
- [x] Grid passed as `const&` everywhere in the search
- [x] Plain C++ types, no `UObject` — safe to hand to a worker thread in phase 6
- [x] `EPathCellState` / `EPathStepResult` / `FPathCell` moved to `GridPathCore.h`
- [x] `UPathFinding` reduced to a visualiser over one `FGridSearch`
- [x] Cell display state derived (`Wall` from grid, start/goal from coords, rest from membership)
- [x] Every phase-1 Blueprint signature unchanged
- [x] A check that two searches share one grid without interfering
- [x] `-run=PathFindingTest` passes
- [x] `CompileAllBlueprints` — 0 errors, 0 warnings
- [x] PIE spot check — confirmed 2026-09-15

## Shape

```
FPathGrid       Width, Height, Blocked[]        durable, const during a search
FGridPathQuery  Start, Goal, costs, diagonal    what this search wants
FGridSearch     Weight[] StartDist[] Parent[]   per-query scratch, never shared
                Membership[] Open[] Closed[]
UPathFinding    Grid + one FGridSearch          debug view, drawing, Blueprint API
```

`ResetCells()` clears the grid and the search. `RebuildGrid()` resizes and then resets.
Editing locks on `bSearchBegun` rather than on cell state.

## Log

**2026-09-15**

- The split fell out cleanly because phase 1 had already made cells values linked by index
  ([ADR-0001]) — the arrays just moved from the grid into the search. The original pointer
  layout would have made this considerably worse.
- `EPathCellState` survives as a *display* type, assembled on demand in
  `UPathFinding::GetCellState` from three sources: grid passability, the start/goal
  coordinates, and search membership. Nothing stores it any more.
- Regression net held. `PathFindingChecks::Run()` was not modified while the core was
  rewritten underneath it — it only touches the public component API — so the pass is
  meaningful rather than circular.
- Added a concurrency check afterwards, because nothing existing would have caught the
  failure this phase exists to prevent: two searches with crossing start/goal pairs are
  stepped **interleaved** over one shared grid, and each resulting path must equal the path
  that query produces when run alone. The grid's walls are asserted intact at the end.
- `FGridSearch::Solve` is used by that check, so the core's batch entry point is not dead
  code waiting for phase 6.

## Manual confirmation

**2026-09-15.** PIE verified: wall, start/goal, open and closed colours all correct; weights
and parent arrows correct; final path drawn; `Allow Diagonal` off produces a square path.
Deriving cell state instead of storing it changed nothing on screen, which was the goal.

## Usability finding

`bAllowDiagonal` was filed under `Parameters|Costs` and could not be found — it is a movement
rule, not a cost, so nobody looks for it there. The three related properties (`StraightCost`,
`DiagonalCost`, `bAllowDiagonal`) are now grouped under **`Parameters|Movement`**.

Metadata only, no behaviour change, checks re-run green. Worth recording because this is the
first piece of evidence about how the *plugin's* editor surface reads to someone who did not
write it — the kind of thing phase 8 needs and that no automated check will ever report.
