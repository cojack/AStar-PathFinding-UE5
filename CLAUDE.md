# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Unreal Engine 5.8 project. The pathfinder lives in a self-contained plugin,
`Plugins/AStarPathFinding/` (pure C++, deps: `Core`, `CoreUObject`, `Engine`); `Source/TP1/`
is the primary game module and now declares nothing. Demo content stays in `Content/`.

Long-term goal is a distributable grid-pathfinding plugin — multiple algorithms, sync and
async queries, weighted tile types. See `docs/PHASES.md` for where that stands; phases 1 and
2 are done.

## Commands

The engine is an **installed build** at `~/.bin/UnrealEngine-5.8.2`, so only the project
module compiles. Adjust the path if the engine moves.

```bash
# Build the editor target (what you want 99% of the time)
~/.bin/UnrealEngine-5.8.2/Engine/Build/BatchFiles/Linux/Build.sh \
    TP1Editor Linux Development -Project="$PWD/TP1.uproject" -WaitMutex

# Build the packaged game target
~/.bin/UnrealEngine-5.8.2/Engine/Build/BatchFiles/Linux/Build.sh \
    TP1 Linux Development -Project="$PWD/TP1.uproject" -WaitMutex

# Regenerate IDE project files
~/.bin/UnrealEngine-5.8.2/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh \
    -project="$PWD/TP1.uproject" -game

# Open the editor
~/.bin/UnrealEngine-5.8.2/Engine/Binaries/Linux/UnrealEditor "$PWD/TP1.uproject"
```

### Tests

**Close the editor before building.** If the editor is running it holds
`libUnrealEditor-AStarPathFinding.so` open, so UBT links to `...-0001.so`, `...-0002.so`
instead and the base library stays stale. The build still reports `Result: Succeeded`, the
commandlet still runs — against old code. Check first:

```bash
pgrep -x UnrealEditor && echo "close the editor first"
```

Use `pgrep -x` (process name), not `pgrep -f` — a pattern match finds the wrapper shell of the
command running it and reports a false positive. If suffixed libraries already exist, delete
them and rebuild:

```bash
rm -f Plugins/AStarPathFinding/Binaries/Linux/libUnrealEditor-AStarPathFinding-0*.so
```

Two checks, both headless, both fast. Run them after any change to the C++ signatures.

```bash
# 1. Algorithm behaviour. Exit code = number of failures.
~/.bin/UnrealEngine-5.8.2/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/TP1.uproject" \
    -run=PathFindingTest -unattended -nopause -nullrhi -nosplash

# 2. The Blueprint bindings still resolve (~66s). Expect "0 errors and 0 warnings".
~/.bin/UnrealEngine-5.8.2/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/TP1.uproject" \
    -run=CompileAllBlueprints -unattended -nopause -nullrhi -nosplash
```

Check 2 is not just a pass/fail on errors — **compare against the previous run's warnings**.
The plugin move passed with 0 errors while silently introducing a dangling package import
that only showed up as a new warning line.

The assertions live in `PathFindingChecks::Run()` (`Private/PathFindingChecks.h`), returning
one string per failure. The commandlet and the automation test `TP1.PathFinding` are both thin
wrappers over it — add new checks there, not in either wrapper.

The automation test also runs from **Tools → Session Frontend → Automation** (filter `TP1`).
Note `Automation RunTests` does **not** complete headlessly in this environment; it stalls
after `FindWorkersResponseMessage`, and does so for stock engine tests too, so it is the
harness and not this project. That is why the commandlet exists.

`-run=PathFindingTest` also writes PNGs of each scenario to
`Saved/PathFindingTests/` — cyan start, magenta goal, dark walls, red expanded, green queued,
blue final path, tile colours underneath. Encoding is pure CPU, so it works under `-nullrhi`.
They are the fastest way to see what a change did to the *shape* of the search, which the
pass/fail line cannot show.

To confirm a change actually reached the binary, remember `TEXT()` literals are **wide**:
`strings -a` cannot see them. Use `strings -a -e l`.

There is no linter; the build is the check.

## Architecture

The algorithm and the thing that draws it are separate, and that separation is the load-bearing
design decision — see `docs/ADR.md` ADR-0012.

### `GridPathCore.{h,cpp}` — the algorithm

Plain C++, no `UObject`, no engine lifecycle. Three types:

- **`FPathGrid`** — durable facts about the world: `Width`, `Height`, and a tile index per
  cell. Row-major, `Index = Y * Width + X`. **Const for the entire duration of a search.**
  `CoordToIndex` returns `INDEX_NONE` for anything off-grid and is the only bounds check the
  rest needs. `TileTable` holds resolved `FPathTileInfo` values — cost multiplier, passable,
  colour — snapshotted from a `UPathTileSet` so the search never touches a `UObject`.
  **There is no wall flag**: a wall is a tile whose definition says `bPassable = false`.
- **`FGridPathQuery`** — what one search wants: start, goal, `bAllowDiagonal`, costs, and the
  `IgnoredTiles` / `RestrictedTiles` filters. Owns the distance policy via `Distance()`: octile
  when diagonals are allowed, Manhattan when they are not. That fallback is required for
  admissibility — do not "simplify" it away.
- **`FGridSearch`** — everything the search mutates: `Weight[]` (f), `StartDist[]` (g),
  `Parent[]`, `Membership[]`, plus the open and closed sets. One per query, never shared.

Two rules in the search are load-bearing and easy to break by accident:

- **Tile cost multipliers are clamped at 1.** The heuristic estimates with the base move cost
  and no multiplier, which is admissible only while nothing is cheaper than base. A multiplier
  below 1 makes A* stop returning optimal paths. Changing this means replacing the heuristic.
- **`CanEnter` checks restrictions, then ignores, then the definition.** A tile named in both
  filter lists is refused.

Because the grid is read-only during a search, **several searches can run over one grid**, which
is what phase 6's async queries need. A check in `PathFindingChecks.h` steps two crossing
searches interleaved and asserts each matches its solo result — if you change the core, that is
the check that catches you.

Notable behaviour: **the goal is never expanded.** `ExpandNeighbours` returns `true` the moment
it touches `GoalIndex`, before evaluating it, so the goal has no parent and is not on the parent
chain. `BuildPath` walks back from `ClosedCells.Last()`, reverses, then appends the goal by hand.
Keep that in mind before touching path reconstruction.

### `UPathFinding` — the debug view

An `UActorComponent` owning one `FPathGrid` and one `FGridSearch`, stepping it and drawing the
result with debug primitives from `TickComponent`. No meshes, no persistence — nothing survives
PIE.

- **Cell state is derived, never stored.** `GetCellState` assembles `EPathCellState` from three
  sources: the cell's tile being impassable → `Wall`, the start/goal coordinates → `Path`,
  otherwise search membership. Changing what a state means changes rendering only, not the
  search.
- **`TileSet` is optional.** With none assigned the grid falls back to a two-entry table (open,
  impassable) that reproduces the old wall behaviour exactly — which is why the pre-tile checks
  still pass unmodified. With one assigned, `WallTile` becomes its first impassable tile, so
  `ToggleWall` places that.
- `StepOnce()` is the entry point and returns `EPathStepResult`; `NextIteration()` is a void
  wrapper kept for the existing Blueprints. Editing locks on `bSearchBegun`.
- `ResetCells()` clears walls *and* the search — it is the UI's "RESET CELLS", so it resets
  everything, not just the algorithm.
- **World space.** The grid origin is world origin, not the owning actor. `GridToWorld` is
  `(X * CellSize, -Y * CellSize, DrawHeight)` — **Y is negated**, so grid +Y runs towards world
  -Y. `WorldToGrid` is its inverse and may return off-grid coordinates by design.

### `APathFindingGrid`

A `Blueprintable` actor owning a `UPathFinding`, so a grid can be placed in a level and
subclassed in the Blueprint Editor. No logic.

## Blueprint contract — do not break

`Content/Blueprints/*.uasset` bind to C++ symbols by name, and those assets cannot be edited
outside the Unreal Editor. **These seven names and signatures are frozen**; renaming or
re-typing any of them silently breaks the existing Blueprints:

| Symbol | Used by |
| --- | --- |
| `ToggleWall(FVector)`, `ToggleBeginEnd(FVector)` | `BP_Player` |
| `HorizontalCells`, `VerticalCells`, `CellSize` | `BP_Player` |
| `NextIteration()`, `ResetCells()` | `WBP_PathFinding` |

Everything else is additive: grid-coordinate editing (`ToggleWallAt`, `SetWallAt`,
`SetStartCell`, …), running (`StepOnce`, `SolveAll`, `RebuildGrid`), queries (`GetCellState`,
`GetCellData`, `GetFinalPath`, …), conversions (`WorldToGrid`, `GridToWorld`) and the
`OnPathFound` / `OnPathFailed` / `OnStepped` events.

Add new Blueprint surface alongside the frozen symbols; never reshape them.

### Core redirects

The classes moved from `/Script/TP1` into `/Script/AStarPathFinding`, so
`Config/DefaultEngine.ini` carries `[CoreRedirects]` entries — two `ClassRedirects` plus a
`PackageRedirects` for the module package itself. They are load-bearing: without them the
assets lose the component with no compile error. If reflected types are ever added back to the
`TP1` module, the package redirect must be removed first or it will misdirect them.

## Docs

`docs/ADR.md` records architectural decisions (supersede, never delete). `docs/PHASES.md`
tracks phase status, with per-phase detail in `docs/phase-<N>/README.md`. Tick checklists as
you implement, not afterwards.
