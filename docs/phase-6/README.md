# Phase 6 — Manager API: sync and async queries

**Status:** DONE (async callback path needs a PIE confirmation)
**Last updated:** 2026-09-16

A single grid queryable from anywhere, synchronously or on a worker thread. This is the
phase the re-entrant core in [ADR-0012] existed for.

## Checklist

- [x] `FGridPathResult` — status, path, cost, expansions
- [x] `RunGridPathQuery` — one call, reads the grid only, no UObjects, worker-thread safe
- [x] `ThinPath` — the `SegmentPath` knob, keeps endpoints
- [x] `UGridPathManager : UWorldSubsystem`, reachable from Blueprint with no actor
- [x] `ConfigureGrid`, `SetTileAt`, `UpdateWalkableTiles`, `GetTileAt`, `GetGridSize`
- [x] `FindGridPathSync`
- [x] `FindGridPathAsync` with a game-thread callback
- [x] Per-query filters, algorithm, 4/8 directions, heuristic weight, segmentation
- [x] `GetPendingQueryCount`
- [x] Checks: thinning, and 16 concurrent queries over one shared grid on real threads
- [x] `CompileAllBlueprints` clean
- [ ] PIE confirmation that the async callback actually fires from a Blueprint

## The async contract

`FindGridPathAsync` **copies the grid** and hands the copy to the worker. The game thread
stays free to edit tiles while queries are in flight, and each answer describes the grid as
it was when that query started. That is the honest contract for anything asynchronous, and
it avoids a lock on every tile read.

The callback fires on the game thread via `AsyncTask(ENamedThreads::GameThread, ...)`, and is
dropped if the manager has gone away, so a world teardown mid-query cannot call into a dead
object.

Copying costs about one byte per cell - 250 KB at 500x500 - against a query measured in tens
of milliseconds at that size. Not worth avoiding.

## What the threading check actually proves

Sixteen queries, half A* and half JPS, all reading **one grid by reference** on the thread
pool, compared against the same sixteen run one at a time. Status, cost, path and expansion
count must match exactly.

Sharing by reference rather than by copy is deliberate: a copy would prove nothing, since the
failure being guarded against is the search writing into the grid it reads. If any write had
survived phases 3 to 5, these results would diverge.

## `Segment Path`

Implemented as **keep the first point, every Nth after it, and always the last**. 1 returns
every cell.

**This is an assumption.** The reference product exposes `Segment Path` as an integer
defaulting to 1 and its behaviour was never observed - only inferred from screenshots. The
reading is that it thins a path for callers steering an actor that does not need cell-by-cell
waypoints. If it turns out to mean something else, `ThinPath` is the only thing to change.

## Not done

The async callback has never been observed firing from an actual Blueprint. The query
machinery underneath is covered by the threading check, but the delegate marshalling back to
the game thread needs PIE. Phase 7 will exercise it in earnest.
