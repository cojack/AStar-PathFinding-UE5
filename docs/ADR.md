# Architecture Decision Record

Newest last. Decisions are never deleted — supersede them and mark the old one
`Superseded by ADR-XXXX`.

---

## ADR-0001 — Grid cells are values linked by index, not heap pointers

**Status:** Implemented (2026-09-15)

**Context.** The original grid was `TArray<S_Cell*>` filled with bare `new S_Cell`, never
deleted, with cells referring to each other through raw `S_Cell* Parent`. Finding a cell's
coordinate meant `Cells.Find(Cell)`, an O(n) scan. None of it was visible to Blueprint.

**Decision.** Store `TArray<FPathCell>` by value. Cells refer to each other by `int32`
index (`ParentIndex`, `INDEX_NONE` when absent). `CoordToIndex` / `IndexToCoord` replace
pointer lookups.

**Consequences.** Fixes the leak. Coordinate lookup becomes O(1). `FPathCell` can be a
`USTRUCT(BlueprintType)`, so cell data is readable from Blueprint. Indices survive a
reallocation where pointers would dangle — which is what makes [ADR-0011] possible at all.
Cost: every cell access goes through a bounds-checked index instead of a pointer deref.

---

## ADR-0002 — The existing Blueprint-facing symbols are frozen

**Status:** Implemented (2026-09-15)

**Context.** `Content/Blueprints/*.uasset` bind to C++ symbols by name. Those assets are
binary and can only be edited inside the Unreal Editor, so a rename in C++ silently breaks
them — the node disappears and the Blueprint fails to compile.

Verified bindings: `BP_Player` → `ToggleWall(FVector)`, `ToggleBeginEnd(FVector)`,
`HorizontalCells`, `VerticalCells`, `CellSize`. `WBP_PathFinding` → `NextIteration()`,
`ResetCells()`.

**Decision.** Those seven names and signatures are frozen. All new API is additive.
`NextIteration()` stays `void` and delegates to the new `StepOnce()`.

**Consequences.** Some naming is inconsistent — `ToggleWall` takes world coordinates while
`ToggleWallAt` takes grid coordinates — and that is accepted permanently. Any future change
to these symbols requires editing the assets in the editor first.

**Verified by:** `-run=CompileAllBlueprints` → 574 Blueprints, 0 errors, 0 warnings.

---

## ADR-0003 — One `EPathCellState` enum instead of the `ColorNum` integer

**Status:** Implemented (2026-09-15)

**Context.** `S_Cell::ColorNum` held 0–4 and meant three things at once: open/closed set
membership, the wall flag, and the debug colour. Reading the algorithm meant memorising
which integer was which.

**Decision.** `UENUM(BlueprintType) EPathCellState { Empty, Wall, Open, Closed, Path }`,
mapping 1:1 onto the old integers. Colour is derived from state via `GetStateColor`, with
the five colours exposed as properties.

**Consequences.** State is self-documenting and Blueprint-visible. The conflation itself
remains — state still drives both search and rendering — so changing a state's meaning
changes both. This is superseded in spirit by [ADR-0012], which separates per-query search
state from the grid.

---

## ADR-0004 — `FIntPoint` as the grid coordinate type

**Status:** Implemented (2026-09-15)

**Context.** Grid coordinates were a private `S_Coord { int X, Y; }`, invisible to Blueprint.
Exposing the grid API needed a Blueprint-visible coordinate type.

**Decision.** Use the engine's `FIntPoint`. Verified `USTRUCT(BlueprintType)` in
`NoExportTypes.h` for 5.8.

**Consequences.** No custom struct to declare, document or redirect, and it composes with
existing engine maths. Ties us to 32-bit coordinates, which is far beyond any practical grid.

---

## ADR-0005 — Upgrade to UE 5.8, including the build-settings shims

**Status:** Implemented (2026-09-15)

**Context.** `TP1.uproject` declared `EngineAssociation 5.4` while the only installed engine
is 5.8.2. The targets pinned `BuildSettingsVersion.V5` and
`EngineIncludeOrderVersion.Unreal5_4`.

**Decision.** `EngineAssociation` → `5.8`, `DefaultBuildSettings` → `V7`,
`IncludeOrderVersion` → `Unreal5_8`.

Bumping the shims was initially judged unnecessary — both values are still accepted by 5.8 —
but the build proved otherwise. Against an **installed** engine the project shares a build
environment with `UnrealEditor`, and `V5` differs from it on
`UndefinedIdentifierWarningLevel`, `UnreachableCodeWarningLevel`, `ReturnTypeWarningLevel`
and `DanglingWarningLevel`. UBT rejects that outright:

> TP1Editor modifies the values of properties: [...]. This is not allowed, as TP1Editor has
> build products in common with UnrealEditor.

**Consequences.** The project no longer builds on 5.4. Stricter warning levels now apply —
undefined identifiers, unreachable code, missing returns and dangling references are errors.
The alternative (`BuildEnvironment = TargetBuildEnvironment.Unique`) was rejected as a way to
keep stale settings alive at the cost of a full engine rebuild.

---

## ADR-0006 — An exhausted open set ends the search

**Status:** Implemented (2026-09-15)

**Context.** The original returned "no path found" whenever the open set was empty but never
set `bPathEnded`, so every further click re-printed the message. The open set can only shrink
to empty once, so the condition is terminal.

**Decision.** `NoPath` sets `bPathEnded` and latches `Status`, exactly as `PathFound` does.

**Consequences.** `SolveAll` terminates instead of spinning to its iteration cap. A
deliberate behaviour change from the original: repeated `NextIteration()` calls after failure
are now silent.

---

## ADR-0007 — The first step honours a goal adjacent to the start

**Status:** Implemented (2026-09-15)

**Context.** `WeightSurroundingCells` returns `true` on touching the goal, but the original
first step discarded that return value. With the goal adjacent to the start, nothing was ever
added to the open set, and the *next* step reported "No path was found" — a wrong answer on a
trivially solvable grid.

**Decision.** The first step uses the return value and can resolve to `PathFound` directly.
`SelectFinalPath` and `GetFinalPath` handle an empty closed set, returning `[start, goal]`.

**Consequences.** A real bug fix, not a refactor. Covered by the `Adjacent` case in
`PathFindingTests.cpp`.

---

## ADR-0008 — A `Blueprintable` actor wrapping the component

**Status:** Implemented (2026-09-15)

**Context.** `UPathFinding` is an actor component. Using it meant hand-building an actor
first; there was nothing to drag into a level or subclass in the Blueprint Editor.

**Decision.** `APathFindingGrid`, a `Blueprintable, BlueprintType` actor with a scene root
and a `UPathFinding` subobject. No logic of its own.

**Consequences.** A grid can be placed and subclassed directly. The component stays usable
standalone, so `BP_Player` is unaffected.

---

## ADR-0009 — Verification: automation tests plus a Blueprint compile pass

**Status:** Implemented (2026-09-15)

**Context.** The project had no tests. The two risks are the algorithm being wrong and the
frozen Blueprint bindings ([ADR-0002]) breaking silently.

**Decision.** Two independent checks:

1. `TP1.PathFinding` automation test (`WITH_DEV_AUTOMATION_TESTS`) for algorithm behaviour.
2. `-run=CompileAllBlueprints` for the asset bindings.

**Consequences.** Check 2 works headlessly, takes ~66s, and passes.

Check 1 **cannot be run headlessly in this environment** — `Automation RunTests` stalls after
`FindWorkersResponseMessage`, and does so for stock engine tests too
(`WorldMetrics.TestZeroState`), so the cause is the harness, not this project. The test is
registered and discoverable (`'TP1.PathFinding'` appears among 6121 listed tests) but only
runs from **Session Frontend → Automation**.

**Superseded in part by ADR-0014**, which adds a commandlet entry point so the same checks do
run headlessly.

---

## ADR-0010 — Extract the pathfinder into a plugin, with core redirects

**Status:** Implemented (2026-09-15)

**Context.** The code lives in the `TP1` game module and cannot be reused elsewhere. The
target is a distributable plugin.

**Decision.** Move `PathFinding.*`, `PathFindingGrid.*` and `PathFindingTests.cpp` into
`Plugins/AStarPathFinding/Source/AStarPathFinding/` (Runtime, `LoadingPhase: Default`,
depending only on `Core`, `CoreUObject`, `Engine`). `TP1_API` → `ASTARPATHFINDING_API`.
`TP1` stays as the primary game module.

Moving a class between modules changes its path from `/Script/TP1.PathFinding` to
`/Script/AStarPathFinding.PathFinding`, which would strip the component from `BP_Player` and
`MainMap`. Handled with a core redirect in `Config/DefaultEngine.ini`:

```ini
[CoreRedirects]
+ClassRedirects=(OldName="/Script/TP1.PathFinding",NewName="/Script/AStarPathFinding.PathFinding")
```

The plugin ships as pure C++; the demo content stays in the project.

**Consequences.** Reuse becomes a folder copy. The redirects are permanent — they cannot be
removed without re-saving the assets in the editor.

The class redirects alone were **not sufficient**. After the move, `CompileAllBlueprints`
still reported 0 errors but emitted a new warning:

> `WBP_PathFinding.uasset: VerifyImport: Failed to find script package for import object
> 'Package /Script/TP1'`

Cause: with every reflected type moved out, the `TP1` module declares no `UCLASS`, `USTRUCT`
or `UENUM` at all, so UHT stops emitting a `/Script/TP1` package — while the assets still
hold a *package-level* import to it, separate from their class imports. Fixed with a package
redirect alongside the class ones:

```ini
+PackageRedirects=(OldName="/Script/TP1",NewName="/Script/AStarPathFinding")
```

If reflected types are ever added back to `TP1`, that redirect must be removed or it will
misdirect them. Final state: 0 errors, 0 warnings, 0 dangling imports.

---

## ADR-0011 — Target feature set: A\* and JPS first

**Status:** Implemented (2026-09-15)

**Context.** The reference product (Fab listing `c76da69d-73b9-42a1-b958-a29c3eaf07c8`)
advertises a `UGPFManager` with five algorithms (A\*, Dijkstra, Greedy Best First, Theta\*,
Jump Point Search), sync and async queries, per-query obstacle and restriction lists, path
segmentation, a 4/8-direction toggle, actor path registration and runtime walkable-tile
updates, over weighted tile types (Basic, Mud, Door, Concrete, Hole).

Feature parity across all five algorithms up front would delay the core work that every one
of them depends on.

**Decision.** Implement A\* and Jump Point Search first. Dijkstra and Greedy Best First are
near-free once the search core is generic (they are A\* with the heuristic forced to zero and
the cost forced to zero respectively) and follow later. Theta\* needs grid line-of-sight and
is deferred.

**Consequences.** The search core must be written generically from the start — cost and
heuristic as policy, not as hardcoded arithmetic — or the cheap algorithms stop being cheap.
JPS additionally constrains the grid to uniform cost within a tile type.

---

## ADR-0012 — Per-query search state moves out of the grid

**Status:** Implemented (2026-09-15)

**Context.** `FPathCell` holds `Weight`, `StartDist`, `ParentIndex` and `State`, all mutated
as the search runs. The grid *is* the search's scratch space. Two concurrent queries would
corrupt each other, which makes the advertised `Find Grid Path Async` impossible, and it is
already why a second search needs `ResetCells()`.

**Decision.** Split the data. The grid holds only durable per-tile facts (tile type, and
therefore cost and passability). Each query allocates its own working buffer for `g`, `f`,
parent and open/closed membership, reading the grid without writing to it.

**Consequences.** Unlocks async queries, concurrent queries, and multiple agents planning in
the same frame. `ResetCells()` between searches stops being necessary. Costs an allocation
per query, mitigated by pooling the buffers on the manager.

The step-by-step visualiser keeps a long-lived working buffer of its own rather than being
deleted — it is the debug view over one query, not the query itself.

This supersedes the search-state half of [ADR-0003]. `EPathCellState` survives as a *display*
type only: `UPathFinding::GetCellState` assembles it on demand from grid passability, the
start/goal coordinates and search membership. Nothing stores it.

Verified by a check that steps two searches with crossing start/goal pairs **interleaved**
over one shared grid and requires each path to equal the one that query produces alone, with
the grid's walls intact afterwards.

---

## ADR-0013 — Tiles carry a type, and type carries cost

**Status:** Implemented (2026-09-15)

**Context.** The current model is binary: a cell is `Empty` or `Wall`. The target model has
named tile types (Basic, Mud, Door, Concrete, Hole) with differing traversal cost, and
per-query lists selecting types to ignore or to forbid.

**Decision.** Data-driven: a `UDataAsset` maps a tile id to its traversal cost and flags. The
plugin ships no tile vocabulary of its own; the demo supplies Basic, Mud, Door, Concrete and
Hole as sample data.

Rejected: a fixed `UENUM`, which the reference product appears to use. It is simpler and gives
native Blueprint dropdowns, but a plugin that hardcodes `Mud` and `Door` imposes its
vocabulary on every consuming project, and the enum would leak into grid storage, the
per-query filters and the Blueprint API at once — making a later migration a breaking change.

**Consequences.** Consumers define their own tile types without recompiling. Costs one
indirection per tile cost lookup, and some setup before anything works. `Hole` stopped being a
special case exactly as expected: impassability is a flag on a definition, there is no wall
concept left in the grid, and `EPathCellState::Wall` survives only as a display value.

Two constraints fell out of the implementation:

- **Cost multipliers must be >= 1.** The heuristic estimates using the base move cost with no
  multiplier, which is admissible only while no tile is cheaper than base. A multiplier below 1
  makes the heuristic overestimate and A\* stops returning optimal paths. Clamped in
  `FGridSearch::Evaluate` and at the property. Moving to a float scale means replacing the
  heuristic too.
- **The grid never holds the asset.** `UPathTileSet` is snapshotted into plain `FPathTileInfo`
  values when the grid is built, so nothing on a worker thread touches a `UObject` — preserving
  the property [ADR-0012] exists to establish.

A tile set is optional: with none assigned the grid falls back to a two-entry table (open,
impassable), which is exactly the previous behaviour and is what let the phase 1-3 checks pass
unmodified.

**Assumption, not verified:** the reference product's `Tile to ignore` is read as "treat this
tile as passable regardless of its definition", and `Restricted` as "never enter this tile".
Both were inferred from screenshots; the semantics were never observed. Restriction wins when a
tile appears in both lists.

---

## ADR-0014 — A commandlet entry point for the behaviour checks

**Status:** Implemented (2026-09-15)

**Context.** [ADR-0009] left the algorithm effectively untested: the automation test could not
be executed headlessly here, and phase 2 was about to move every source file between modules —
exactly the change that needs an automated before/after.

**Decision.** `UPathFindingTestCommandlet`, run as
`UnrealEditor-Cmd <project>.uproject -run=PathFindingTest`, returning the failure count as its
exit code. The assertions live in one shared `PathFindingChecks::Run()` returning a list of
failure strings; the automation test and the commandlet are both thin wrappers over it, so
neither can drift from the other.

**Consequences.** The logic is verifiable in CI and from a terminal, without the automation
harness. Both entry points are compiled out by `WITH_DEV_AUTOMATION_TESTS`, so nothing ships.
Cost: one extra header and two small files.

First run passed, which is also the first time these checks had ever executed — covering the
two bugs fixed in [ADR-0006] and [ADR-0007]. Re-run after the plugin move: still passing.

---

## ADR-0015 — The goal is an ordinary node

**Status:** Implemented (2026-09-15)

**Context.** Inherited from the original implementation: `WeightSurroundingCells` returned true
the instant it *touched* the goal, before evaluating it. The goal therefore never had a parent,
never entered the open set, and its own entry cost was never counted.

Two consequences, the second only visible once a second algorithm existed:

1. Paths were not strictly optimal. Entering the goal diagonally costs `DiagonalCost` where a
   straight approach costs `StraightCost`, and the search could not prefer the cheaper one
   because it never priced that step.
2. A\* and JPS could not be compared. Neither reported a true total, so an equivalence check
   between them would have been meaningless — and equivalence against A\* is the only practical
   way to verify a JPS implementation.

**Decision.** The goal is relaxed, opened and closed like any other cell. The search ends when
the goal is *selected* from the open set, which is textbook A\* termination.

**Consequences.** Total path cost is genuinely optimal and `GetPathCost` means something. Both
algorithms are comparable, which is what makes the randomised check in phase 5 possible.

Net deletion, not addition: the `GoalParent` tracking, the empty-closed-set branch in
`SelectFinalPath`, and the append-the-goal-by-hand branch in `BuildPath` all disappeared. Path
reconstruction is now a plain walk of the parent chain from the goal.

**Behaviour change:** a goal adjacent to the start now takes two steps rather than one — the
first expands the start and opens the goal, the second selects it. The check for [ADR-0007]
was updated to match, and it caught the change on the first run. The bug ADR-0007 fixed cannot
recur, because the case it special-cased no longer exists.

An impassable goal now correctly reports `NoPath` instead of being reachable, since `CanEnter`
applies to it like any other cell.

---

## ADR-0016 — An optional heuristic weight

**Status:** Implemented (2026-09-15)

**Context.** Repeated reports that A\* "checks every single square" before committing. A
step-by-step trace of the 25x25 wall grid confirmed the behaviour exactly: 11 straight
diagonal steps to the wall, then 124 of 196 expansions jumping *backwards* down the diagonal
it had just walked, reaching the gap only at step 185 of 197 and then running to the goal in
11 steps with no backtracking at all.

That is correct A\*. The exit route costs 396 and A\* processes in cost order, so the exit
sits at the back of the queue until everything cheaper is drained. Measured against an
independent Dijkstra, 183 of those expansions are obligatory for any admissible heuristic;
A\* did 196, within 7%.

Correct, and still unsatisfying: the observation is that the search abandons a direction it
was committed to in order to re-examine cells it already passed. There is no implementation
fix, because the fanning-out *is* the optimality proof.

**Decision.** `FGridPathQuery::HeuristicWeight`, default `1.0`, clamped at `1.0`. Above 1 the
heuristic deliberately overestimates, so the search commits to its current direction rather
than revisiting cheaper alternatives.

**Consequences.** Optimality is traded for work. Weighted A\* guarantees the result is no
worse than `weight x optimal`, and the check pins that bound rather than the speed-up - the
bound is the contract, the speed-up is a bonus that depends on the map.

Measured over 52 solvable random 30x30 mazes at 30% wall density:

| weight | avg expansions | cost vs optimal | paths made worse | worst case |
| --- | --- | --- | --- | --- |
| 1.0 | 99.1 | +0.00% | 0 of 52 | - |
| 1.2 | 44.5 | +1.09% | 25 of 52 | +6% |
| 1.5 | 38.8 | +1.70% | 29 of 52 | +9% |
| 2.0 | 36.9 | +2.73% | 37 of 52 | +11% |

1.2 halves the search for about 1% longer paths; past 1.5 the returns nearly vanish. Default
stays 1.0 so nothing changes unless asked - a plugin that silently returns non-optimal paths
would be worse than one that searches too hard.

This does not replace JPS, which expands 5 cells on the same wall grid at the *optimal* cost.
The weight exists for grids where JPS does not apply, notably 4-connected ones.

### Why the tied region is so large

A follow-up report showed hundreds of cells displaying an identical weight, and asked why the
search covers the whole empty middle. The answer is a property of the octile metric rather
than anything in the implementation: with diagonal movement, straight and diagonal steps can
be interleaved in any order, so there is not one shortest route but a very large family of
them, and **every cell on any of them has the same f**. The heuristic cannot separate them.

Reproduced as `serpentine-astar`, a 20x12 grid with three walls whose gaps alternate ends:

| | |
| --- | --- |
| Direct distance | 234 |
| Real route | 390, 67% longer |
| Cells tied at exactly the direct distance | 91 of 240 |
| Cells any admissible A* must expand | 122 |
| What A* expanded | 128 |
| With `HeuristicWeight` 1.5 | 102, still cost 390 here |

The tied blob is 38% of the grid, and because the walls make the real answer 67% more
expensive than the estimate, all of it sits below the final cost and has to be eliminated.
The worse the detour, the larger the region that must be ruled out - which is why obstacle-
heavy maps look worst.

A weighted run can drop *below* the admissible floor (102 against 122) precisely because it is
no longer admissible; it happened to keep the optimal cost on this map, which is luck, not a
guarantee.
