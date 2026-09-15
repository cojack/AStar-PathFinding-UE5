# Phase 5 — Algorithm strategy: A\* and Jump Point Search

**Status:** DONE
**Last updated:** 2026-09-15

Add an algorithm selector and implement Jump Point Search alongside A\*. See [ADR-0011]
for why these two first, and [ADR-0015] for the goal-expansion change this forced.

## Checklist

- [x] `EPathAlgorithm { AStar, JumpPointSearch }`, selectable on the query and the component
- [x] JPS: jump scanning, forced-neighbour detection, direction pruning
- [x] Path reconstruction interpolates the runs between jump points
- [x] JPS validity check with fall back to A\*, reported via `DidAlgorithmFallBack`
- [x] `FPathGrid::HasUniformCost`
- [x] The goal is expanded like any other node ([ADR-0015])
- [x] `GetPathCost` on the search and the component
- [x] Randomised A\* vs JPS equivalence over 40 grids
- [x] JPS path contiguity and walkability asserted
- [x] Fall-back checks: weighted grid, 4-connected grid, valid grid
- [x] `-run=PathFindingTest` passes
- [x] `CompileAllBlueprints` — 0 errors, 0 warnings
- [ ] PIE spot check with JPS selected

## JPS is not a drop-in replacement

Jump Point Search prunes the search on two assumptions: **every move costs the same**, and
**diagonal movement is available**. Phase 4 broke the first one, and `bAllowDiagonal` can
break the second.

Running JPS anyway does not fail loudly — it returns a confidently wrong path. So `Begin`
checks `HasUniformCost()` and `bAllowDiagonal`, substitutes A\* when either fails, and records
it. The component prints a message; `GetActiveAlgorithm()` reports what actually ran.

This is worth knowing before wiring JPS to a UI dropdown: selecting it on a grid with Mud on
it silently gets you A\*, which is the correct outcome but not an obvious one.

## The goal used to be free

The original code returned "found" the moment the search *touched* the goal, without ever
expanding it. The goal's own entry cost was therefore never counted, which has two effects:

1. Paths were not strictly optimal. Approaching the goal diagonally costs 14 where a straight
   approach costs 10, and the search had no way to prefer the cheaper one.
2. A\* and JPS could not be compared, because neither total was the real total.

The goal is now an ordinary node: it is relaxed, it enters the open set, and the search ends
when it is *selected*. That makes the cost genuinely optimal and both algorithms comparable.
See [ADR-0015].

It also deleted code rather than adding it. `GoalParent`, the empty-closed-set special case in
`SelectFinalPath`, and the "append the goal by hand" branch in `BuildPath` are all gone —
`PaintPath` and `BuildPath` now just walk the parent chain from the goal.

## Log

**2026-09-15**

- Randomised equivalence is doing the real work here. Hand-written JPS cases pass with subtly
  wrong pruning; 40 random 12x12 grids at 25% wall density do not. The check asserts equal cost
  rather than equal paths, since several distinct paths can share the optimal cost, and it
  asserts `Solvable > 5` so it cannot pass by never finding a path at all.
- Contiguity is checked separately, because interpolation between jump points is the part most
  likely to produce a path that looks right in cost but skips through a wall.
- The corner-cutting convention had to match A\*, which permits it. JPS forced-neighbour rules
  differ between the cutting and non-cutting variants, and mixing them would have produced
  paths that disagreed on cost in exactly the cases the random check covers.
- One check failed on the first run — the `Adjacent` case still asserted the old one-step
  behaviour. That is the regression net working: the goal-expansion change was supposed to
  alter that behaviour, and the check said so.

## Verified against an independent oracle

A reported case of "it checks every single square" prompted a direct check rather than an
explanation. The maze from that report is reconstructed in `PathFindingChecks.h`: 18x10,
4-connected, start top-right, goal top-left. The geometry was derived from the reported
numbers themselves - a constant 170 along the top row and 190 one row below the start only
hold for an 18-wide grid under Manhattan costs.

What it asserts:

- **Cost matches an independent Dijkstra** written inline in the check, sharing no code with
  `FGridSearch`. "A* agrees with A*" would prove nothing.
- **Every displayed weight is exactly `g + Manhattan h`**, so the numbers drawn on screen are
  accounted for cell by cell.
- **Every expanded cell has `f <= FinalCost`**, and **everything still queued has
  `f >= FinalCost`**. That pair is the whole A* guarantee, and it is why the search looks
  wasteful: it must rule out every cell cheaper than the answer before it can claim the answer
  is optimal. On that maze, 107 of 180 cells.

The behaviour was confirmed correct. One real bug surfaced - in the check, not the algorithm:
the invariant included the **start cell**, which is never relaxed and so has `g = 0` and no
`f` at all. That is why the editor draws "A" there rather than a number. The check now asserts
that explicitly instead of tripping over it.

Measured work, same grid, same optimal cost:

| | expanded | open | cost |
| --- | --- | --- | --- |
| A* open 25x25 | 24 | 94 | 336 |
| JPS open 25x25 | 1 | 0 | 336 |
| A* around a wall | 196 | 45 | 396 |
| JPS around a wall | 5 | 0 | 396 |

## Images

`-run=PathFindingTest` writes a PNG per scenario to `Saved/PathFindingTests/`
(`astar-open`, `jps-open`, `astar-wall`, `jps-wall`, `reported-maze`, `weighted-mud`).
`FImageUtils::SaveImageByExtension` is CPU-side encoding, so no GPU or PIE is needed.

`astar-wall.png` is the clearest artefact this project has produced: A* colours the entire
region above the wall red before the path emerges, which is the reported "it checks every
single square", drawn. `jps-wall.png` is the same grid and the same cost with five expansions.

`weighted-mud.png` covers what cannot currently be seen in PIE at all, since the demo has no
way to paint non-wall tiles: the path runs along the edge of the mud band and cuts down past
its end rather than crossing it.

## Why A* looks wasteful, measured

A second report, that searching the region above the wall was obviously pointless when the
route runs along the bottom. Measured rather than argued, on the 25x25 wall grid:

| | expansions |
| --- | --- |
| Reachable cells | 603 |
| With no heuristic (Dijkstra) | 441 |
| Floor: cells any admissible A* is obliged to expand | 183 |
| What this A* does | 196 |
| JPS, same grid, same cost | 5 |

A* is within 7% of the floor; the excess is cells with `f` exactly equal to the final cost,
which tie-breaking may touch either way.

The heuristic is a straight-line estimate and cannot see the wall. A cell at (11,10) has
`g = 150` and `h = 192`, total 342 against a final cost of 396 - it looks promising and only
walking there disproves it. Every one of those 183 cells must be ruled out before 396 can be
claimed optimal. Skipping them is not a smarter A*, it is a wrong one.

This is now a permanent check: A* must expand at least the floor and no more than floor + 40,
and the heuristic must at least halve the work versus none. If `h` ever stops discriminating,
paths stay correct and only the cost of finding them explodes - a silent failure this is the
only check that would catch.

The honest conclusion for a product: this question will be asked by users too. The answer is
JPS, which is why it exists. A weighted heuristic remains available as an option that trades
the optimality guarantee for far fewer expansions on 4-connected grids.

## Not done

PIE with JPS selected. The algorithm is verified against A\* far more thoroughly than eyes
could manage, but the fall-back message and the dropdown have never been seen in the editor.
