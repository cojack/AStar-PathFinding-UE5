# Phase 2 — Extract into `Plugins/AStarPathFinding`

**Status:** DONE
**Last updated:** 2026-09-15

Move the pathfinder out of the `TP1` game module into a self-contained plugin, so it can be
dropped into another project by copying a folder. Pure C++; demo content stays in the
project. See [ADR-0010].

## Checklist

- [x] Run `-run=CompileAllBlueprints` and record the baseline — 574 Blueprints, 0 errors
- [x] Add a commandlet test entry point so the move has an automated before/after ([ADR-0014])
- [x] `Plugins/AStarPathFinding/AStarPathFinding.uplugin`
- [x] `Source/AStarPathFinding/AStarPathFinding.Build.cs` — deps `Core`, `CoreUObject`, `Engine` only
- [x] Minimal module implementation (`IMPLEMENT_MODULE(FDefaultModuleImpl, ...)`)
- [x] Move the six sources into `Public/` and `Private/`
- [x] `TP1_API` → `ASTARPATHFINDING_API`
- [x] Register the plugin in `TP1.uproject`
- [x] `[CoreRedirects]` class redirects in `Config/DefaultEngine.ini`
- [x] Package redirect for `/Script/TP1` — needed, and not anticipated; see the log
- [x] Plugin depends only on `Core`, `CoreUObject`, `Engine` (no `InputCore`/`EnhancedInput`)
- [x] Rebuild: `Result: Succeeded`, both `libUnrealEditor-TP1.so` and `libUnrealEditor-AStarPathFinding.so` link
- [x] `-run=PathFindingTest` still passes after the move
- [x] `CompileAllBlueprints` — 0 errors, 0 warnings, `BP_Player` and `WBP_PathFinding` both compile
- [x] Update `CLAUDE.md` paths and build commands
- [x] Open `MainMap` in the editor and confirm the component survived — confirmed 2026-09-15

## Layout

```
Plugins/AStarPathFinding/
├── AStarPathFinding.uplugin
└── Source/AStarPathFinding/
    ├── AStarPathFinding.Build.cs
    ├── Public/    PathFinding.h, PathFindingGrid.h
    └── Private/   PathFinding.cpp, PathFindingGrid.cpp, AStarPathFindingModule.cpp,
                   PathFindingChecks.h, PathFindingTests.cpp,
                   PathFindingTestCommandlet.{h,cpp}
```

`Source/TP1/` keeps only `TP1.{h,cpp}` and `TP1.Build.cs` — the primary game module, which the
`.uproject` requires but which now declares nothing.

## Log

**2026-09-15**

- `git mv` moved only `PathFinding.{h,cpp}`; the other six files were created earlier the same
  session and never committed, so git refused them as "not under version control". Finished
  with plain `mv`. Worth knowing before reading a confusing `ls` — the first attempt looked
  like it had scattered the whole module.
- Build succeeded first try after the move.
- `CompileAllBlueprints` reported 0 errors **but a new warning**, absent from the baseline:
  `VerifyImport: Failed to find script package for import object 'Package /Script/TP1'`.
  The class redirects were not enough. With every reflected type gone, `TP1` declares no
  `UCLASS`/`USTRUCT`/`UENUM`, so UHT stops emitting a `/Script/TP1` package — and the assets
  hold a package-level import distinct from their class imports. A `+PackageRedirects` entry
  cleared it. Final: 0 errors, 0 warnings, 0 dangling imports.
- The warning only surfaced because the baseline run was compared against, rather than
  checking for "0 errors" alone. A pass with a new warning is still a regression.

## Manual confirmation

**2026-09-15.** `MainMap` opened in the editor: the `PathFinding` component is present on the
placed `BP_Player`. PIE verified end to end — walls place, start/end place, `NEXT STEP`
iterates to a found path. The core redirects work against real instances, not just against
the class resolver, which is the part `CompileAllBlueprints` could not prove.
