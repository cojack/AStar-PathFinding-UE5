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

/** Which search to run. */
UENUM(BlueprintType)
enum class EPathAlgorithm : uint8
{
	/** Best-first with an octile heuristic. Works on any grid. */
	AStar			UMETA(DisplayName = "A*"),
	/**
	 * Jump Point Search. Much faster on open grids, but only correct when every move costs
	 * the same and diagonals are allowed - it prunes on exactly those assumptions. A query
	 * asking for it on a weighted or 4-connected grid falls back to A*.
	 */
	JumpPointSearch	UMETA(DisplayName = "Jump Point Search")
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

	/** True when no passable tile costs more than the base move. Required by JPS. */
	bool HasUniformCost() const;

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

	EPathAlgorithm Algorithm = EPathAlgorithm::AStar;

	bool bAllowDiagonal = true;
	int32 StraightCost = 10;
	int32 DiagonalCost = 14;

	/**
	 * Scales the heuristic. 1.0 is plain A*: optimal, and obliged to rule out every cell
	 * cheaper than the answer, which is why it fans out around obstacles.
	 *
	 * Above 1.0 the heuristic overestimates, so the search commits to its current direction
	 * instead of re-examining cheaper alternatives. Far fewer expansions, and paths may be
	 * longer - bounded at this factor times optimal. 1.2 is a common trade.
	 */
	float HeuristicWeight = 1.0f;

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

	/** The algorithm actually running, which may differ from the one the query asked for. */
	EPathAlgorithm GetAlgorithm() const { return Algorithm; }
	/** True when the requested algorithm was not applicable and A* was substituted. */
	bool DidAlgorithmFallBack() const { return bAlgorithmFellBack; }

	/** Total cost of the built path, counting every step including the last. */
	int32 GetPathCost(const FPathGrid& Grid) const;

	/** How many cells the search has expanded. The real measure of how hard it worked. */
	int32 GetExpandedCount() const { return ClosedCells.Num(); }

	/** How many cells are currently waiting in the open set. */
	int32 GetOpenCount() const { return OpenCells.Num(); }

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

	EPathAlgorithm Algorithm = EPathAlgorithm::AStar;
	bool bAlgorithmFellBack = false;

	/** Whether this query may enter a cell, applying the restrict and ignore filters. */
	bool CanEnter(const FPathGrid& Grid, int32 Index) const;

	void Expand(const FPathGrid& Grid, int32 CenterIndex);
	void ExpandAStar(const FPathGrid& Grid, int32 CenterIndex);
	void ExpandJumpPoints(const FPathGrid& Grid, int32 CenterIndex);

	/** Relaxes ToIndex when reaching it through FromIndex is cheaper. */
	void Relax(const FPathGrid& Grid, int32 FromIndex, int32 ToIndex, int32 StepCost);

	EPathStepResult SelectLightest(const FPathGrid& Grid);
	void PaintPath();

	// --- Jump Point Search ---

	/** Scans from FromIndex along (dx, dy) for the next jump point. INDEX_NONE if none. */
	int32 Jump(const FPathGrid& Grid, int32 FromIndex, int32 dx, int32 dy) const;
	/** Whether an obstacle beside Coord forces a neighbour that pruning would otherwise drop. */
	bool HasForcedNeighbour(const FPathGrid& Grid, FIntPoint Coord, int32 dx, int32 dy) const;
	/** Directions still worth scanning from Coord, given the direction it was reached from. */
	void PrunedDirections(const FPathGrid& Grid, FIntPoint Coord, int32 dx, int32 dy,
		TArray<FIntPoint>& OutDirections) const;
};
