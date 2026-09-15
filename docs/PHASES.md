# Phases

**Last updated:** 2026-09-15

Target: turn a single-file A\* teaching demo into a reusable, distributable grid pathfinding
plugin. Feature target is derived from the reference product described in [ADR-0011].

| # | Phase | Status | Detail |
| --- | --- | --- | --- |
| 1 | Blueprint exposure of the A\* component | **DONE** | [phase-1](phase-1/README.md) |
| 2 | Extract into `Plugins/AStarPathFinding` | **DONE** | [phase-2](phase-2/README.md) |
| 3 | Re-entrant search core (grid data vs. per-query state) | **DONE** | [phase-3](phase-3/README.md) |
| 4 | Weighted tile types, per-query ignore/restrict filters | **DONE** | [phase-4](phase-4/README.md) |
| 5 | Algorithm strategy: A\* + Jump Point Search | **DONE** | [phase-5](phase-5/README.md) |
| 6 | Manager API: sync + async queries, segmentation, 4/8 directions | **NEXT** | |
| 7 | Actor path registration, walkable-tile updates, dynamic re-planning | TODO | |
| 8 | Demo map, packaging, distribution docs | TODO | |

Phase 3 is done, which unblocks 5, 6 and 7 — none of them were safe to build on a search
that wrote into the grid it was reading ([ADR-0012]).

## Open questions

These are blocking design decisions, not implementation details. Each one is cheaper to
settle before the phase that depends on it starts.

1. ~~**Tile type model.**~~ **Decided 2026-09-15: data-driven `UDataAsset`** ([ADR-0013]).
   The plugin ships no tile vocabulary; the demo supplies Basic/Mud/Door/Concrete/Hole as
   sample data.
2. **What does `Segment Path` mean?** (still open) The reference UI exposes it as an integer (default 1)
   on every query. Most likely the path is returned in chunks of N tiles, or simplified to
   every Nth waypoint. Behaviour was inferred from screenshots, never observed. Blocks
   phase 6.
3. ~~**`Door` semantics.**~~ **Resolved by the phase 4 design.** A door is an ordinary
   passable tile; "conditionally passable" is expressed by a query restricting or ignoring
   that tile type, not by anything special in the tile itself.
4. **Does the visualiser stay?** Current plan is yes, as a debug layer over the new core
   ([ADR-0012]), which also preserves the frozen Blueprint contract ([ADR-0002]).

## Usability backlog

Small friction found by using the plugin rather than by testing it. Feeds phase 8.

- **2026-09-15, fixed.** `bAllowDiagonal` sat under `Parameters|Costs` and was undiscoverable;
  moved to `Parameters|Movement` with the two cost properties.

## Answered questions

- **"Why does A* check every square?"** Traced and measured, see [ADR-0016]. Correct
  behaviour, within 7% of the provable floor. `HeuristicWeight` added for callers who would
  rather have the speed than the guarantee.

## Known gaps

- ~~The automation tests have never executed.~~ **Closed 2026-09-15** by the commandlet in
  [ADR-0014]. `-run=PathFindingTest` passes, before and after the plugin move. The automation
  test itself still only runs from Session Frontend, which remains a harness limitation, not
  a coverage gap.
- **No test covers the editor integration** — that a placed `APathFindingGrid` ticks and
  draws, or that the new properties appear in the Details panel. Those need a human in PIE.
  The phase 2 redirects were confirmed that way on 2026-09-15; anything similar will have to
  be too.
