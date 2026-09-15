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

## Not done

PIE with JPS selected. The algorithm is verified against A\* far more thoroughly than eyes
could manage, but the fall-back message and the dropdown have never been seen in the editor.
