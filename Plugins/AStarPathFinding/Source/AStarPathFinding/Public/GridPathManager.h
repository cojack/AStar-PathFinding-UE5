#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridPathCore.h"
#include "PathTileSet.h"
#include "GridPathManager.generated.h"

/** Fired on the game thread when an async query finishes. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FGridPathCompletedSignature, const FGridPathResult&, Result);

/**
 * One grid, queried from anywhere. A world subsystem, so Blueprints reach it without an
 * actor to hang it off: Get World Subsystem -> Grid Path Manager.
 *
 * Async queries copy the grid and run on a worker thread, which is only safe because the
 * search keeps all its state per-query and never writes to the grid (see docs/ADR.md
 * ADR-0012). The copy means the game thread may keep editing tiles while queries are in
 * flight - each one answers about the grid as it was when it started.
 */
UCLASS()
class ASTARPATHFINDING_API UGridPathManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- Grid setup ---

	/** Sizes the grid and adopts a tile vocabulary. Clears everything. */
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Manager")
	void ConfigureGrid(int32 Width, int32 Height, UPathTileSet* TileSet);

	UFUNCTION(BlueprintCallable, Category = "PathFinding|Manager")
	void SetTileAt(FIntPoint Coord, int32 TileIndex);

	/** Bulk tile update, for terrain that changes at runtime. */
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Manager")
	void UpdateWalkableTiles(const TArray<FIntPoint>& Coords, int32 TileIndex);

	UFUNCTION(BlueprintPure, Category = "PathFinding|Manager")
	int32 GetTileAt(FIntPoint Coord) const;

	UFUNCTION(BlueprintPure, Category = "PathFinding|Manager")
	bool IsValidCoord(FIntPoint Coord) const;

	UFUNCTION(BlueprintPure, Category = "PathFinding|Manager")
	FIntPoint GetGridSize() const;

	// --- Queries ---

	/**
	 * Runs the query on the calling thread and returns the answer. Simple, and a stall if
	 * the grid is large - see docs/PHASES.md for measured timings before using this on a
	 * big grid every frame.
	 */
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Query", meta = (AutoCreateRefTerm = "TilesToIgnore,RestrictedTiles"))
	FGridPathResult FindGridPathSync(FIntPoint Start, FIntPoint Goal,
		EPathAlgorithm Algorithm,
		const TArray<int32>& TilesToIgnore,
		const TArray<int32>& RestrictedTiles,
		int32 SegmentPath = 1,
		bool bEightDirections = true,
		float HeuristicWeight = 1.0f);

	/**
	 * Copies the grid, runs the query on a worker thread, and calls OnCompleted on the game
	 * thread. The callback does not fire if the world goes away first.
	 */
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Query", meta = (AutoCreateRefTerm = "TilesToIgnore,RestrictedTiles"))
	void FindGridPathAsync(FIntPoint Start, FIntPoint Goal,
		EPathAlgorithm Algorithm,
		const TArray<int32>& TilesToIgnore,
		const TArray<int32>& RestrictedTiles,
		FGridPathCompletedSignature OnCompleted,
		int32 SegmentPath = 1,
		bool bEightDirections = true,
		float HeuristicWeight = 1.0f);

	/** Async queries started and not yet delivered. */
	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	int32 GetPendingQueryCount() const;

private:
	FPathGrid Grid;

	/** Incremented when an async query starts, decremented when its callback is queued. */
	FThreadSafeCounter PendingQueries;

	FGridPathQuery MakeQuery(FIntPoint Start, FIntPoint Goal, EPathAlgorithm Algorithm,
		const TArray<int32>& TilesToIgnore, const TArray<int32>& RestrictedTiles,
		bool bEightDirections, float HeuristicWeight) const;
};
