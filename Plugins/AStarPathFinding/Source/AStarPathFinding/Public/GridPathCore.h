#pragma once

#include "CoreMinimal.h"
#include "Algo/Reverse.h"
#include "GridPathCore.generated.h"

/** Display state of a grid cell. Derived from grid data plus search state, never stored. */
UENUM(BlueprintType)
enum class EPathCellState : uint8
{
	/** Free, never looked at. */
	Empty	UMETA(DisplayName = "Empty"),
	/** Blocked, never entered. */
	Wall	UMETA(DisplayName = "Wall"),
	/** In the open set: weighted, waiting to be selected. */
	Open	UMETA(DisplayName = "Open"),
	/** In the closed set: already expanded. */
	Closed	UMETA(DisplayName = "Closed"),
	/** Start cell, goal cell, or part of the final path. */
	Path	UMETA(DisplayName = "Path")
};

/** Outcome of a single pathfinding step. */
UENUM(BlueprintType)
enum class EPathStepResult : uint8
{
	/** Start and/or goal is missing, nothing ran. */
	NotStarted	UMETA(DisplayName = "Not Started"),
	/** The search advanced by one cell. */
	InProgress	UMETA(DisplayName = "In Progress"),
	/** The goal was reached. */
	PathFound	UMETA(DisplayName = "Path Found"),
	/** The open set ran dry, the goal is unreachable. */
	NoPath		UMETA(DisplayName = "No Path")
};

/** A snapshot of one cell's A* data, assembled on demand for Blueprint. */
USTRUCT(BlueprintType)
struct FPathCell
{
	GENERATED_BODY()

	/** A* f score: StartDist plus the heuristic to the goal. -1 until evaluated. */
	UPROPERTY(BlueprintReadOnly, Category = "PathFinding")
	int32 Weight = -1;

	/** A* g score: cheapest known cost from the start. -1 until evaluated. */
	UPROPERTY(BlueprintReadOnly, Category = "PathFinding")
	int32 StartDist = -1;

	UPROPERTY(BlueprintReadOnly, Category = "PathFinding")
	EPathCellState State = EPathCellState::Empty;

	/** Index of the cell this one was reached from, INDEX_NONE if none. */
	UPROPERTY(BlueprintReadOnly, Category = "PathFinding")
	int32 ParentIndex = INDEX_NONE;
};

/**
 * A tile type as the search sees it: a plain snapshot of one FPathTileDef, with no UObject
 * behind it, so a grid can be read from a worker thread without touching the asset.
 */
struct FPathTileInfo
{
	int32 CostMultiplier = 1;
	bool bPassable = true;
	FColor Color = FColor::White;

	FPathTileInfo() = default;
	FPathTileInfo(int32 InCostMultiplier, bool bInPassable, FColor InColor)
		: CostMultiplier(InCostMultiplier), bPassable(bInPassable), Color(InColor) {}
};

/**
 * Durable grid data: what is true about the world regardless of who is pathing through it.
 * Read-only while a search runs, which is what lets several searches share one grid.
 *
 * Every cell holds an index into TileTable. With no tile set supplied the table falls back
 * to two entries - 0 open, 1 impassable - which is exactly the old wall/empty behaviour.
 */
struct ASTARPATHFINDING_API FPathGrid
{
	int32 Width = 0;
	int32 Height = 0;

	/** Per cell, an index into TileTable. */
	TArray<uint8> Tiles;

	/** Resolved tile definitions. Never empty: DefaultTileTable() is used if none is set. */
	TArray<FPathTileInfo> TileTable;

	/** Tile written by the plain "clear this cell" and "wall this cell" paths. */
	uint8 DefaultTile = 0;
	uint8 WallTile = 1;

	static TArray<FPathTileInfo> DefaultTileTable();

	/** Replaces the tile vocabulary and re-resolves DefaultTile / WallTile. Clears the grid. */
	void SetTileTable(const TArray<FPathTileInfo>& InTable);

	void Resize(int32 InWidth, int32 InHeight);

	/** Sets every cell back to DefaultTile. */
	void ClearTiles();

	int32 Num() const { return Tiles.Num(); }
	bool IsValidCoord(FIntPoint Coord) const;
	int32 CoordToIndex(FIntPoint Coord) const;
	FIntPoint IndexToCoord(int32 Index) const;

	uint8 TileAt(int32 Index) const;
	void SetTileAtIndex(int32 Index, uint8 Tile);
	void SetTile(FIntPoint Coord, uint8 Tile);

	/** Definition for a tile id. Falls back to the first entry if the id is out of range. */
	const FPathTileInfo& TileInfoFor(uint8 Tile) const;
	/** Definition for the tile in a given cell. */
	const FPathTileInfo& TileInfoAt(int32 Index) const;

	/** Impassable by its own definition, ignoring any query filter. */
	bool IsBlocked(FIntPoint Coord) const;
	bool IsBlockedIndex(int32 Index) const;

	/** Legacy binary wall API, in terms of DefaultTile and WallTile. */
	void SetBlocked(FIntPoint Coord, bool bBlocked);
};

/** Everything one search needs to know that is not the grid itself. */
struct ASTARPATHFINDING_API FGridPathQuery
{
	FIntPoint Start = FIntPoint(-1, -1);
	FIntPoint Goal = FIntPoint(-1, -1);

	bool bAllowDiagonal = true;
	int32 StraightCost = 10;
	int32 DiagonalCost = 14;

	/** Tiles this query may enter even when their definition says impassable. */
	TArray<uint8> IgnoredTiles;

	/** Tiles this query must never enter, even when their definition says passable. */
	TArray<uint8> RestrictedTiles;

	/** Grid distance under this query's movement rules. Manhattan when diagonals are off. */
	int32 Distance(FIntPoint A, FIntPoint B) const;
};

/**
 * Per-query working state. Holds every value the search mutates, so the grid stays const
 * and two searches can run over the same grid without touching each other.
 */
struct ASTARPATHFINDING_API FGridSearch
{
	/** Sizes the buffers and seeds the start. False if start or goal is off-grid. */
	bool Begin(const FPathGrid& Grid, const FGridPathQuery& InQuery);

	/** Advances by one cell. The first call seeds the open set from the start. */
	EPathStepResult Step(const FPathGrid& Grid);

	/** Steps until the search finishes or MaxIterations is hit. */
	EPathStepResult Solve(const FPathGrid& Grid, int32 MaxIterations = 10000);

	void Reset();

	EPathStepResult GetStatus() const { return Status; }
	bool HasEnded() const { return bEnded; }
	bool HasStarted() const { return bStarted; }

	/** The final path, start -> goal. Empty unless the status is PathFound. */
	TArray<FIntPoint> BuildPath(const FPathGrid& Grid) const;

	// Per-cell inspection, for drawing and for Blueprint queries
	EPathCellState GetMembership(int32 Index) const;
	int32 GetWeight(int32 Index) const;
	int32 GetStartDist(int32 Index) const;
	int32 GetParent(int32 Index) const;

private:
	FGridPathQuery Query;

	int32 StartIndex = INDEX_NONE;
	int32 GoalIndex = INDEX_NONE;

	TArray<int32> Weight;			// f
	TArray<int32> StartDist;		// g
	TArray<int32> Parent;
	TArray<EPathCellState> Membership;	// Empty / Open / Closed / Path only

	TArray<int32> OpenCells;
	TArray<int32> ClosedCells;

	bool bStarted = false;
	bool bEnded = false;
	EPathStepResult Status = EPathStepResult::NotStarted;

	/** Whether this query may enter a cell, applying the restrict and ignore filters. */
	bool CanEnter(const FPathGrid& Grid, int32 Index) const;
	bool ExpandNeighbours(const FPathGrid& Grid, int32 CenterIndex);
	void Evaluate(const FPathGrid& Grid, int32 FromIndex, int32 ToIndex);
	EPathStepResult SelectLightest(const FPathGrid& Grid);
	void PaintPath();
};
