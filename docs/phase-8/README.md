# Phase 8 — Packaging and distribution

**Status:** PARTIAL — the plugin is distributable, the demo does not yet show most of it
**Last updated:** 2026-09-16

## Checklist

- [x] Builds as a packaged game, Development **and Shipping**
- [x] Test and commandlet code compiles out entirely outside the editor
- [x] No dependency on the host project
- [x] Only `Core`, `CoreUObject`, `Engine`
- [x] Automation test renamed `TP1.PathFinding` -> `AStarPathFinding.Core`
- [x] `.uplugin` metadata: description, category, docs URL
- [x] Plugin README: install, API, tile types, algorithms, performance, limitations
- [x] Repository README rewritten for the plugin
- [ ] `CreatedBy` in the `.uplugin` — left blank rather than invented
- [ ] Demo showing the manager, async queries, actor re-planning or tile painting
- [ ] PIE confirmation of the Blueprint delegates from phases 6 and 7

## What "distributable" was verified to mean

Shipping was the check worth doing, and it had never been run before this phase. Both the
Game target in Development and in Shipping build clean, and in each only
`Module.AStarPathFinding.cpp` compiles — every test, commandlet and image-dump file disappears
behind `WITH_DEV_AUTOMATION_TESTS`. Nothing test-related ships.

The only reference to the host project anywhere in the plugin was the automation test's own
name, `"TP1.PathFinding"`. A plugin registering a test under its host's namespace is wrong the
moment someone drops it into a different project, so it is now `AStarPathFinding.Core`.

## The honest gap

**The demo demonstrates phase 1.** It draws walls, places endpoints and steps a search — the
same thing it did before any of this work. It shows nothing of the manager, async queries,
weighted tiles, actor registration or re-planning, which is most of what the plugin now does.

Someone evaluating this would see a step-by-step A\* visualiser and conclude that is the
product.

Closing that is content work: widgets, an event graph, a tile palette and an agent that walks
a path and re-plans when a wall drops in front of it. Widget Blueprint graphs cannot be
authored from the command line, so it needs editor time rather than more commits.

`PaintTileIndex` — letting left-click place any tile rather than only walls — is the smallest
useful step and unblocks demonstrating weighted terrain at all.
