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
| 6 | Manager API: sync + async queries, segmentation, 4/8 directions | **DONE** | [phase-6](phase-6/README.md) |
| 7 | Actor path registration, walkable-tile updates, dynamic re-planning | **DONE** | [phase-7](phase-7/README.md) |
| 8 | Demo map, packaging, distribution docs | **NEXT** | |

Phase 3 is done, which unblocks 5, 6 and 7 — none of them were safe to build on a search
that wrote into the grid it was reading ([ADR-0012]).

## Open questions

These are blocking design decisions, not implementation details. Each one is cheaper to
settle before the phase that depends on it starts.

1. ~~**Tile type model.**~~ **Decided 2026-09-15: data-driven `UDataAsset`** ([ADR-0013]).
   The plugin ships no tile vocabulary; the demo supplies Basic/Mud/Door/Concrete/Hole as
   sample data.
2. **What does `Segment Path` mean?** Implemented as "keep every Nth waypoint, endpoints
   always", documented as an assumption in [phase-6](phase-6/README.md). Never observed in the
   reference product. `ThinPath` is the only thing to change if it is wrong. The reference UI exposes it as an integer (default 1)
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

## Scaling, measured 2026-09-16

One query, wall grid, corner to corner. `-run=PathFindingTest` prints these every run.

| Grid | Cells | A* expanded | A* | JPS |
| --- | --- | --- | --- | --- |
| 25x25 | 625 | 196 | 0.05 ms | 0.02 ms |
| 100x100 | 10,000 | 3,619 | 2.3 ms | 0.18 ms |
| 200x200 | 40,000 | 14,744 | 17.8 ms | 0.64 ms |
| 400x400 | 160,000 | 59,494 | 135 ms | 2.5 ms |
| 500x500 | 250,000 | 93,119 | 261 ms | 4.1 ms |

A* scales as roughly `cells^1.4`. Expansions themselves are linear, always about 37% of
cells; it is the *cost per expansion* that grows, from 0.26 to 2.8 microseconds, because
`SelectLightest` scans the whole open set and the open set grows with the map.

JPS expands 5 cells at every size. Its 4 ms at 500x500 is almost entirely allocating the
per-query buffers, not searching.

### A* against JPS on realistic maps

The single-wall grid above flatters JPS badly. With scattered obstacles, same map, both optimal:

| Grid | Obstacles | A* | JPS | speedup |
| --- | --- | --- | --- | --- |
| 100x100 | 0% | 0.16 ms | 0.21 ms | 0.8x, slower |
| 100x100 | 25% | 0.63 ms | 0.36 ms | 1.8x |
| 200x200 | 10% | 4.56 ms | 1.26 ms | 3.6x |
| 200x200 | 25% | 5.57 ms | 2.77 ms | 2.0x |
| 500x500 | 10% | 41.3 ms | 11.0 ms | 3.8x |
| 500x500 | 25% | 74.8 ms | 32.7 ms | 2.3x |

**JPS is worth 2-4x on real maps, not the 40x the single-wall case suggests, and it is slower
than A* on an empty grid.** Its cost is scan length in `Jump`, so obstacles hurt it: 500x500
went 3.97 ms at 0% obstacles to 35 ms at 30%.

Everything here is **synchronous**, on the calling thread. Budget against a 16 ms frame:

| Map | best per query | queries per frame |
| --- | --- | --- |
| 100x100, 25% obstacles | 0.36 ms | ~40 |
| 200x200, 25% obstacles | 2.8 ms | ~5 |
| 500x500, 25% obstacles | 33 ms | under one |

Consequences for the roadmap:

- Async queries (phase 6) stop being optional above roughly 150x150 on the game thread. This
  is the highest-value remaining work: no algorithm choice rescues 500x500 synchronously.
- The binary heap deferred in `SelectLightest` is now justified by measurement rather than
  suspicion. Constant-ish cost per expansion would put 500x500 near 30 ms instead of 261 ms.

## Where the time is not

Measured, so it stops being speculation:

- **Blueprint is not the overhead.** The benchmarks call `FGridSearch` directly from a
  commandlet - no BP, no UObject. Going through `UPathFinding::SolveAll`, which is what a
  Blueprint node calls, measured 1.0-1.1x the raw core at 100x100 and 200x200. A `UFUNCTION`
  invocation is microseconds against a search measured in milliseconds.
- **The per-step `OnStepped` broadcast is free** when nothing is bound, which was the
  suspected culprit and is not.

## Known gaps

- **The visualiser will dominate a large grid in PIE, and it is not the pathfinder.**
  `DrawCells` issues a `DrawDebugPlane` and a `DrawDebugString` per cell every tick, whether
  or not a search is running. At 200x200 that is 40,000 debug planes per frame. Unmeasured -
  debug drawing needs a real RHI and the checks run under `-nullrhi` - but structural.
  Anyone benchmarking in PIE must turn off `Draw Cells` / `Draw Weights` /
  `Draw Parent Arrows` first, or they will measure the renderer.

- ~~The automation tests have never executed.~~ **Closed 2026-09-15** by the commandlet in
  [ADR-0014]. `-run=PathFindingTest` passes, before and after the plugin move. The automation
  test itself still only runs from Session Frontend, which remains a harness limitation, not
  a coverage gap.
- **No test covers the editor integration** — that a placed `APathFindingGrid` ticks and
  draws, or that the new properties appear in the Details panel. Those need a human in PIE.
  The phase 2 redirects were confirmed that way on 2026-09-15; anything similar will have to
  be too.
