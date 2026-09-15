# A\* Grid Pathfinding

Grid pathfinding for Unreal Engine 5.8. A\* and Jump Point Search over weighted, data-driven
tile types, queryable synchronously or on a worker thread, with actor path tracking that
notices when terrain changes cut a route.

Pure C++, depends only on `Core`, `CoreUObject` and `Engine`. No content, no third-party code.

## Install

Copy `AStarPathFinding/` into your project's `Plugins/` folder and add it to your `.uproject`:

```json
"Plugins": [ { "Name": "AStarPathFinding", "Enabled": true } ]
```

Regenerate project files and build. Verified against UE 5.8 on Linux, Editor, Development and
Shipping.

## Two ways to use it

### The manager, for gameplay

`UGridPathManager` is a world subsystem, so Blueprints reach it with **Get World Subsystem →
Grid Path Manager**. One grid, queried from anywhere.

```
Configure Grid (Width, Height, Tile Set, Cell Size, Origin)
Set Tile At / Update Walkable Tiles
Find Grid Path Sync  -> Result
Find Grid Path Async -> callback on the game thread
Register Actor Path / Un Register Actor Path / Replan Actor Path
World To Grid / Grid To World
```

`FGridPathResult` carries `Status`, `Path`, `Cost` and `Expanded`.

### The component, for seeing what it does

`UPathFinding` is an actor component that owns a grid and steps one search at a time, drawing
the frontier with debug primitives. It is a teaching and debugging tool, not the gameplay path.
`APathFindingGrid` is a `Blueprintable` actor that owns one.

## Tile types

Tiles are defined in a `UPathTileSet` data asset. The plugin ships no vocabulary of its own:

| Field | |
| --- | --- |
| `Id` | name used for lookup |
| `CostMultiplier` | scales the cost of entering. **Must be >= 1** |
| `bPassable` | whether it can be entered at all |
| `Color` | debug colour |

Cells hold an index into that list, so **order is meaningful and stable** - inserting in the
middle renumbers every cell that follows.

With no tile set assigned, the grid falls back to two tiles: index 0 open, index 1 impassable.

**Why the multiplier floor.** The heuristic estimates with the base move cost and no
multiplier, which is only admissible while nothing is cheaper than base. A multiplier below 1
makes A\* stop returning shortest paths.

Each query may override passability:

- `TilesToIgnore` - enter these even though they are impassable
- `RestrictedTiles` - never enter these even though they are passable

A restriction wins when a tile is named by both.

## Algorithms

| | |
| --- | --- |
| **A\*** | optimal on any grid |
| **Jump Point Search** | optimal, faster, but **only valid on uniform-cost grids with diagonals enabled** |

JPS prunes on the assumption that every move costs the same. A weighted or 4-connected grid
breaks that, so a query asking for JPS on one **falls back to A\*** and reports it through
`DidAlgorithmFallBack`. Selecting JPS on a grid with any weighted tile silently gets you A\*.

`HeuristicWeight` (default 1.0) trades optimality for speed. Above 1 the search commits to its
direction instead of exhausting cheaper alternatives, and the result is guaranteed no worse
than `weight x optimal`. Measured over random 30x30 mazes at 30% walls:

| weight | avg expansions | cost vs optimal |
| --- | --- | --- |
| 1.0 | 99.1 | +0.00% |
| 1.2 | 44.5 | +1.09% |
| 1.5 | 38.8 | +1.70% |
| 2.0 | 36.9 | +2.73% |

## Performance

One query, corner to corner, measured on one machine - treat as shape, not as a guarantee.

| Grid | Obstacles | A\* | JPS |
| --- | --- | --- | --- |
| 100x100 | 0% | 0.16 ms | 0.21 ms |
| 100x100 | 25% | 0.63 ms | 0.36 ms |
| 200x200 | 25% | 5.57 ms | 2.77 ms |
| 500x500 | 25% | 74.8 ms | 32.7 ms |

**JPS is worth 2-4x on maps with scattered obstacles, not the 40x a single-wall test
suggests, and it is slower than A\* on a completely empty grid.** Its cost is scan length, so
obstacles hurt it rather than help.

Against a 16 ms frame: roughly 40 queries at 100x100, 5 at 200x200, and **under one** at
500x500. Above about 150x150, use `Find Grid Path Async`.

**A\* explores more than people expect**, and that is the optimality guarantee rather than
waste: it must eliminate every cell cheaper than the answer before it can call the answer
shortest. On a map where walls make the real route 67% longer than the straight-line estimate,
that was 128 of 240 cells - against a proven floor of 122.

## Async

`FindGridPathAsync` **copies the grid** and runs on the thread pool, calling back on the game
thread. The game thread stays free to edit tiles while queries are in flight, and each answer
describes the grid as it was when that query started. The callback is dropped if the manager
is destroyed first.

Several queries may read one grid at once. The search keeps all state per-query and never
writes to the grid, which is what makes this safe.

## Actor paths

Register an actor with the path it is following and the manager reports when terrain changes
cut it:

```
Register Actor Path (Actor, Path, TilesToIgnore, RestrictedTiles, Algorithm, EightDirections)
Update Walkable Tiles  ->  fires On Actor Path Invalidated for affected actors
Replan Actor Path      ->  re-plans from the actor's current cell to its stored goal
```

The agent's query settings are stored with its path, so a re-plan reproduces that agent's
rules - one that can walk through doors is not re-planned as one that cannot.

**Re-planning is not automatic.** The manager reports and stops. Re-planning every affected
actor inside a terrain update would turn one tile change into an unbounded synchronous stall.
The caller decides, and can go async.

## Coordinates

Grid origin is `GridOrigin`, cells are `CellSize` across, and **grid +Y runs towards world
-Y**: `GridToWorld` is `Origin + (X * CellSize, -Y * CellSize, 0)`.

## Tests

```bash
UnrealEditor-Cmd YourProject.uproject -run=PathFindingTest -unattended -nopause -nullrhi
```

Exit code is the number of failures. Also writes PNGs of each scenario to
`Saved/PathFindingTests/` and prints a scaling benchmark. The same assertions run from
**Session Frontend → Automation** as `AStarPathFinding.Core`.

Everything under `WITH_DEV_AUTOMATION_TESTS`, so none of it ships.

## Known limitations

- **Square grids only.** No hex, no 3D, no navmesh.
- **The open set is a linear scan.** Fine at editor sizes; a binary heap would cut 500x500
  from about 261 ms to roughly 30 ms.
- **The debug visualiser draws every cell every tick** whether or not a search is running. At
  200x200 that is 40,000 debug primitives per frame. Turn `Draw Cells` off before judging
  performance in PIE.
- **JPS silently falls back** on weighted or 4-connected grids. Check `GetActiveAlgorithm`.
- **`SegmentPath` semantics are an assumption**: keep the first point, every Nth after, always
  the last.
