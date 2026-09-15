# Phase 4 — Weighted tile types and per-query filters

**Status:** DONE
**Last updated:** 2026-09-15

Replace the binary wall flag with named tile types carrying a traversal cost, and let each
query override passability. See [ADR-0013].

## Checklist

- [x] `FPathTileDef` — id, cost multiplier, passable flag, colour
- [x] `UPathTileSet : UDataAsset` — the vocabulary, with `IndexOfTile`
- [x] `FPathTileInfo` — plain snapshot of a definition, no `UObject`, worker-thread safe
- [x] `FPathGrid` stores a tile index per cell plus a resolved `TileTable`
- [x] Two-tile fallback table when no asset is set, preserving the old wall behaviour
- [x] `WallTile` resolves to the first impassable tile, so the legacy wall API still works
- [x] `FGridPathQuery::IgnoredTiles` / `RestrictedTiles`
- [x] `FGridSearch::CanEnter` applying restrict-then-ignore-then-definition
- [x] Entry cost scaled by the destination tile's multiplier, clamped at 1
- [x] `SetTileAt` / `GetTileAt` on the component
- [x] `TileSet`, `IgnoredTiles`, `RestrictedTiles` exposed under `Parameters|Tiles`
- [x] Visualiser draws untouched cells in their tile colour when a tile set is supplied
- [x] Checks: cost detour, restricted tile, ignored tile, restriction beats ignore
- [x] `-run=PathFindingTest` passes
- [x] `CompileAllBlueprints` — 0 errors, 0 warnings
- [x] Asset-driven path verified headlessly — `UPathTileSet` built in memory, pushed through
      the component, snapshot / wall resolution / mud routing / fallback all asserted
- [x] Sample `UPathTileSet` asset (`Content/DA_TileSet.uasset`) — generated headlessly via
      the Python commandlet, contents verified by reading back through UE
- [ ] PIE spot check with a tile set assigned — optional, the code path is covered above

## Two decisions worth knowing

**Cost multipliers are clamped at 1.** The heuristic estimates with the base move cost and no
multiplier. That is admissible only while no tile is *cheaper* than base — a multiplier below
1 would let the heuristic overestimate, and A\* would stop returning optimal paths. The clamp
is in `FGridSearch::Evaluate` and the property is `ClampMin=1`. Do not "improve" this into a
float scale without replacing the heuristic too.

**Restrict beats ignore.** When a tile appears in both lists the query refuses it.
`CanEnter` checks restrictions first, then ignores, then the definition. A check covers it,
because the precedence is arbitrary enough that someone will eventually flip it.

## Backward compatibility

With no `TileSet` assigned the grid builds a two-entry table — index 0 open, index 1
impassable — which is byte-for-byte the previous wall/empty behaviour. `ToggleWall` and
`SetWallAt` are now expressed in terms of `DefaultTile` and `WallTile` rather than a bool, and
the existing checks passed unmodified, which is what confirms the legacy path still works.

If a tile set *is* assigned, `WallTile` becomes the first impassable tile in it, so left-click
in the demo places whatever that is (`Hole`, in the intended sample data).

## Log

**2026-09-15**

- `Hole` stopped being a special case, as predicted in [ADR-0013]: impassability is now a flag
  on a definition, so there is no distinct "wall" concept in the grid at all — only tiles that
  happen to be impassable, and `EPathCellState::Wall` survives purely as a display value.
- The mud check asserts the exact detour length (7 cells), not just "avoided the mud". A
  weaker assertion would pass even if the costs were being applied to the wrong tile.
- Deliberately not done: a bitmask for the filter lists. `TArray<uint8>::Contains` over a
  handful of tile types is not worth optimising until a profile says so.

## Headless coverage of the asset path

The `UPathTileSet` → `FPathTileInfo` snapshot was initially assumed to need the editor, since
it starts from a data asset. It does not: the check builds a `UPathTileSet` with
`NewObject(GetTransientPackage())`, assigns it to the component, calls `RebuildGrid()` and
asserts the whole chain — default tile, `IndexOfTile`, the legacy wall API resolving to the
first impassable tile, mud routing through the *component* API, and the fallback table
returning when the asset is cleared.

No asset on disk is involved, so the check has no content dependency and cannot rot when demo
assets change. The only thing left needing the editor is demo content, not correctness.

## Not done

1. ~~The sample tile set asset.~~ **Done 2026-09-15**, and `.uasset` files *can* be authored
   without the editor after all — `-run=pythonscript` with `DataAssetFactory` works headlessly.
   `Content/DA_TileSet.uasset` holds:

   | Index | Id | Cost | Passable | Colour |
   | --- | --- | --- | --- | --- |
   | 0 | Basic | 1 | yes | white |
   | 1 | Hole | 1 | **no** | black |
   | 2 | Mud | 3 | yes | brown |
   | 3 | Door | 1 | yes | orange |
   | 4 | Concrete | 1 | yes | grey |

   Index 1 is the impassable one, so left-click keeps placing it. The asset references
   `/Script/AStarPathFinding.PathTileSet`, so it needs no core redirect.

   Still to do by hand: assign it to the `PathFinding` component (`Parameters|Tiles → Tile
   Set`), which is a Blueprint default change and therefore your call, not mine.

   Two verification traps hit while confirming this, both mine rather than the asset's:
   `strings` defaults to a 4-character minimum so the `Mud` id looked absent, and `unreal.log`
   output did not reach the filtered log. Reading the asset back through
   `EditorAssetLibrary.load_asset` was the only check that actually proved anything.

2. **PIE with a tile set assigned.** Now optional: both the fallback and asset-driven paths are
   covered headlessly. What PIE would still add is confirmation that tile *colours* render, and
   that is the only part no check can reach.

3. **No way to paint non-wall tiles in the demo.** `BP_Player` binds left-click to
   `ToggleWall`, so Mud, Door and Concrete cannot be placed by hand. The routing is verified,
   but not watchable. A `PaintTileIndex` property on the component would fix it in a few lines;
   the full palette UI belongs to phase 8.
