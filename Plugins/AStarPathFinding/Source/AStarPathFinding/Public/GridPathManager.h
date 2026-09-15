#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridPathCore.h"
#include "PathTileSet.h"
#include "GridPathManager.generated.h"

/** Fired on the game thread when an async query finishes. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FGridPathCompletedSignature, const FGridPathResult&, Result);

/** Fired when terrain changes cut a registered actor's path. BlockedAt is the first bad cell. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FActorPathInvalidatedSignature,
	AActor*, Actor, FIntPoint, BlockedAt);

/** What the manager remembers about one actor following a path. */
USTRUCT(BlueprintType)
struct FRegisteredActorPath
{
	GENERATED_BODY()

	/** The full route, start to goal, before any thinning. */
	UPROPERTY(BlueprintReadOnly, Category = "PathFinding")
	TArray<FIntPoint> Path;

	/** Kept so a re-plan reproduces the original agent's rules, not a generic query. */
	UPROPERTY(BlueprintReadOnly, Category = "PathFinding")
	EPathAlgorithm Algorithm = EPathAlgorithm::AStar;

	UPROPERTY(BlueprintReadOnly, Category = "PathFinding")
	TArray<int32> TilesToIgnore;

	UPROPERTY(BlueprintReadOnly, Category = "PathFinding")
	TArray<int32> RestrictedTiles;

	UPROPERTY(BlueprintReadOnly, Category = "PathFinding")
	bool bEightDirections = true;
};

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

	/** Sizes the grid, adopts a tile vocabulary and sets the world mapping. Clears everything. */
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Manager")
	void ConfigureGrid(int32 Width, int32 Height, UPathTileSet* TileSet,
		float CellSize = 100.f, FVector GridOrigin = FVector::ZeroVector);

	/** World location to grid coordinate. May return an off-grid coordinate. */
	UFUNCTION(BlueprintPure, Category = "PathFinding|Manager")
	FIntPoint WorldToGrid(FVector WorldLocation) const;

	/** Centre of a cell in world space. */
	UFUNCTION(BlueprintPure, Category = "PathFinding|Manager")
	FVector GridToWorld(FIntPoint Coord) const;

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

	// --- Actor paths ---

	/**
	 * Remembers that Actor is following Path, so terrain changes can report when it is cut.
	 * The query settings are stored too, so a re-plan reproduces this agent's rules rather
	 * than a generic query.
	 */
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Actors", meta = (AutoCreateRefTerm = "TilesToIgnore,RestrictedTiles"))
	void RegisterActorPath(AActor* Actor, const TArray<FIntPoint>& Path,
		const TArray<int32>& TilesToIgnore,
		const TArray<int32>& RestrictedTiles,
		EPathAlgorithm Algorithm = EPathAlgorithm::AStar,
		bool bEightDirections = true);

	UFUNCTION(BlueprintCallable, Category = "PathFinding|Actors")
	void UnRegisterActorPath(AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "PathFinding|Actors")
	TArray<FIntPoint> GetActorPath(AActor* Actor) const;

	UFUNCTION(BlueprintPure, Category = "PathFinding|Actors")
	bool IsActorRegistered(AActor* Actor) const;

	/** Registered actors, after dropping any that have been destroyed. */
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Actors")
	int32 GetRegisteredActorCount();

	/**
	 * Re-plans from the actor's current cell to the end of its stored path, using the
	 * settings it registered with, and replaces the registration on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Actors")
	FGridPathResult ReplanActorPath(AActor* Actor, int32 SegmentPath = 1);

	/** Checks every registered path against the grid now and reports the ones that are cut. */
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Actors")
	TArray<AActor*> RevalidateActorPaths();

	/** Fired for each registered actor whose path a terrain change has cut. */
	UPROPERTY(BlueprintAssignable, Category = "PathFinding|Actors")
	FActorPathInvalidatedSignature OnActorPathInvalidated;

private:
	FPathGrid Grid;

	float CellSize = 100.f;
	FVector GridOrigin = FVector::ZeroVector;

	/** Weak, so a destroyed actor drops out rather than keeping a stale entry alive. */
	TMap<TWeakObjectPtr<AActor>, FRegisteredActorPath> ActorPaths;

	/** Incremented when an async query starts, decremented when its callback is queued. */
	FThreadSafeCounter PendingQueries;

	/** Drops entries whose actor has gone away. */
	void PruneActorPaths();

	FGridPathQuery QueryForRegistration(const FRegisteredActorPath& Registration,
		FIntPoint Start, FIntPoint Goal) const;

	FGridPathQuery MakeQuery(FIntPoint Start, FIntPoint Goal, EPathAlgorithm Algorithm,
		const TArray<int32>& TilesToIgnore, const TArray<int32>& RestrictedTiles,
		bool bEightDirections, float HeuristicWeight) const;
};
