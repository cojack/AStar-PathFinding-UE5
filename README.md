# A\* Pathfinding in Unreal Engine 5

Started as a single-file A\* demo. Now a self-contained grid pathfinding plugin with a demo
project wrapped around it.

**The plugin lives in [`Plugins/AStarPathFinding/`](Plugins/AStarPathFinding/README.md)** —
that README is the one to read for installation and API. This page is about the repository.

## What is here

| | |
| --- | --- |
| `Plugins/AStarPathFinding/` | the plugin. Pure C++, depends only on Core, CoreUObject, Engine |
| `Content/` | the demo: a grid you can draw walls on and step a search through |
| `Source/TP1/` | the game module, which now declares nothing |
| `docs/` | [phases](docs/PHASES.md) and [architecture decisions](docs/ADR.md) |

## Features

- **A\*** and **Jump Point Search**, both optimal, selectable per query
- **Weighted tile types** from a data asset, with per-query ignore and restrict filters
- **Synchronous and asynchronous** queries; async copies the grid and runs on the thread pool
- **Actor path tracking** that reports when terrain changes cut a route, and re-plans on request
- **Heuristic weight** to trade optimality for speed where that is the better deal
- **Step-by-step visualiser** drawing the frontier, weights and parent links

## Running the demo

Generate project files, build, open `Content/Maps/MainMap` and press Play.

- **Left click** — toggle a wall
- **Right click** — place the start, then the goal
- **NEXT STEP** — advance the search. With `Steps Per Iteration` at 9999 it solves in one click
- **RESET CELLS** — clear everything

Properties are on the `PathFinding` component of `BP_Player`, in the **Components** panel
(not the event graph): algorithm, movement costs, diagonals, heuristic weight, tile set,
debug draw toggles and colours.

## Tests

```bash
UnrealEditor-Cmd TP1.uproject -run=PathFindingTest -unattended -nopause -nullrhi
```

Exit code is the number of failures. It also writes PNGs of each scenario to
`Saved/PathFindingTests/` and prints a scaling benchmark.

Checks cover A\* against an independent Dijkstra, JPS against A\* over random mazes, expansion
counts against the theoretical floor, concurrent queries over one shared grid on real threads,
and the actor registry against a real world with a real spawned actor.

## Visualisation

![Initialize cells](ReadMeContent/PlaceCells.gif)

![Step by step iterations](ReadMeContent/Iterations.gif)

![](ReadMeContent/Step1.png)
![](ReadMeContent/Step2.png)
![](ReadMeContent/Step3.png)
![](ReadMeContent/Step4.png)

## License

Intended for learning. Free for personal and educational use.
