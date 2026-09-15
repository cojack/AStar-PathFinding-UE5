# Phase 7 — Actor paths and dynamic re-planning

**Status:** DONE (Blueprint delegate needs a PIE confirmation)
**Last updated:** 2026-09-16

The manager remembers which actor is following which route, notices when terrain changes cut
one, and can re-plan from where the actor actually is.

## Checklist

- [x] World mapping on the manager: `ConfigureGrid` takes cell size and origin, plus
      `WorldToGrid` / `GridToWorld`
- [x] `FirstBlockedOnPath` — pure, thread-safe, honours the query's own filters
- [x] `RegisterActorPath` / `UnRegisterActorPath` / `GetActorPath` / `IsActorRegistered`
- [x] `GetRegisteredActorCount`, pruning destroyed actors
- [x] `RevalidateActorPaths`, returning the cut ones
- [x] `UpdateWalkableTiles` revalidates automatically
- [x] `OnActorPathInvalidated` delegate
- [x] `ReplanActorPath`, from the actor's current cell to its stored goal
- [x] Checks against a real `UWorld` with a real spawned actor
- [x] `CompileAllBlueprints` clean
- [ ] PIE confirmation that `OnActorPathInvalidated` reaches a Blueprint

## Three decisions

**Registrations are weak.** `TMap<TWeakObjectPtr<AActor>, ...>`, pruned on access. A destroyed
actor drops out on its own rather than keeping an entry alive or leaving a dangling pointer.

**Invalidation is pushed, not polled.** `UpdateWalkableTiles` revalidates immediately, so a
terrain change reports cut routes in the same call that caused them. No per-tick scan, and
nothing to forget to call.

**Re-planning is explicit.** The manager reports that a route is cut and stops there;
`ReplanActorPath` is a separate call. Re-planning every affected actor automatically inside a
terrain update would turn one tile change into an unbounded synchronous stall - with the
measured timings in `docs/PHASES.md`, twenty agents on a 200x200 grid would be a 56 ms hitch
from a single call. The caller decides, and can use `FindGridPathAsync` instead.

**The query is stored with the registration.** A re-plan reproduces that agent's rules -
its algorithm, its ignored and restricted tiles, its direction set - rather than a generic
query. An agent that can walk through doors must not be re-planned as one that cannot.

## Thinned paths and blocking

`FirstBlockedOnPath` sees only the points it is given. A thinned path samples the route, so a
wall dropped between two samples is missed. The manager therefore stores the **full** path and
hands out the thinned one, so revalidation always sees every cell.

## Log

**2026-09-16**

- UHT rejects `TArray<int32>()` as a C++ default argument, so `RegisterActorPath` puts its
  array parameters before the defaulted scalars. `AutoCreateRefTerm` already makes those pins
  optional in Blueprint, so nothing is lost.
- The registry check builds a real `UWorld` with `UWorld::CreateWorld`, pulls the subsystem out
  of it and spawns a real actor. It covers register, count, revalidate, the wall being dropped
  on the route, re-planning around it, and unregistering. Far better than testing the
  bookkeeping against a mock.

## Not done

`OnActorPathInvalidated` has never been observed reaching a Blueprint. The C++ path is covered
by the registry check; Blueprint delegate binding is not, and neither is phase 6's async
callback. Both want the same PIE session.
