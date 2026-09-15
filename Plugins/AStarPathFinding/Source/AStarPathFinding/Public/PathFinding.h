#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GridPathCore.h"
#include "PathTileSet.h"
#include "PathFinding.generated.h"

/** Fired once the goal is reached. Path runs start -> goal. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPathFoundSignature, const TArray<FIntPoint>&, Path);
/** Fired once the open set is exhausted without reaching the goal. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPathFailedSignature);
/** Fired after every step, including the ones that end the search. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPathSteppedSignature, EPathStepResult, Result);

/**
 * Debug view over a single grid search: owns a grid, drives one FGridSearch a step at a
 * time, and draws the result with debug primitives. The algorithm itself lives in
 * FGridSearch (GridPathCore.h), which this component does not need to be running.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class ASTARPATHFINDING_API UPathFinding : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UPathFinding();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Number of horizontal cells in the grid
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters", meta = (UIMin = 1))
	int HorizontalCells = 10;

	// Number of vertical cells in the grid
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters", meta = (UIMin = 1))
	int VerticalCells = 10;

	// Size of each square cell in the grid
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters", meta = (UIMin = 0))
	float CellSize = 100.f;

	// Which search to run. Jump Point Search falls back to A* on a weighted or
	// 4-connected grid, where its pruning assumptions do not hold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters|Movement")
	EPathAlgorithm Algorithm = EPathAlgorithm::AStar;

	// Cost of a horizontal or vertical move between neighbouring cells
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters|Movement", meta = (UIMin = 1))
	int32 StraightCost = 10;

	// Cost of a diagonal move, normally a little above StraightCost
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters|Movement", meta = (UIMin = 1))
	int32 DiagonalCost = 14;

	// Whether the search may move diagonally. Off makes the heuristic Manhattan.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters|Movement")
	bool bAllowDiagonal = true;

	/**
	 * Tile vocabulary for this grid. Optional: with none set the grid falls back to a
	 * two-tile table (open / impassable), which is the plain wall behaviour.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters|Tiles")
	TObjectPtr<UPathTileSet> TileSet;

	/** Tile indices this search may cross even when the tile is marked impassable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters|Tiles")
	TArray<int32> IgnoredTiles;

	/** Tile indices this search must never enter, even when the tile is passable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters|Tiles")
	TArray<int32> RestrictedTiles;

	// Print algorithm status to the screen
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
	bool bShowDebugMessages = true;

	// Draw the cell planes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Draw")
	bool bDrawCells = true;

	// Draw each evaluated cell's weight
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Draw")
	bool bDrawWeights = true;

	// Draw an arrow from each evaluated cell towards the cell it was reached from
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Draw")
	bool bDrawParentArrows = true;

	// Draw the "A" and "B" labels on the start and goal cells
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Draw")
	bool bDrawStartEndLabels = true;

	// Z height the grid is drawn at
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Draw")
	float DrawHeight = 0.f;

	// Fraction of CellSize each drawn cell plane covers, leaving a gap between cells
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Draw", meta = (UIMin = 0.1, UIMax = 1.0))
	float CellDrawScale = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Draw|Colors")
	FColor EmptyColor = FColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Draw|Colors")
	FColor WallColor = FColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Draw|Colors")
	FColor OpenColor = FColor::Green;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Draw|Colors")
	FColor ClosedColor = FColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Draw|Colors")
	FColor PathColor = FColor::Blue;

	UPROPERTY(BlueprintAssignable, Category = "PathFinding|Events")
	FPathFoundSignature OnPathFound;

	UPROPERTY(BlueprintAssignable, Category = "PathFinding|Events")
	FPathFailedSignature OnPathFailed;

	UPROPERTY(BlueprintAssignable, Category = "PathFinding|Events")
	FPathSteppedSignature OnStepped;

	UFUNCTION(BlueprintCallable)
	void ToggleWall(FVector WorldCoord);

	UFUNCTION(BlueprintCallable)
	void ToggleBeginEnd(FVector WorldCoord);

	UFUNCTION(BlueprintCallable)
	void NextIteration();

	UFUNCTION(BlueprintCallable)
	void ResetCells();

	// --- Editing, in grid coordinates ---

	// Flips a cell between Empty and Wall. Ignored once the search has started.
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Edit")
	void ToggleWallAt(FIntPoint Coord);

	// Sets a cell to Wall or back to Empty. Ignored once the search has started.
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Edit")
	void SetWallAt(FIntPoint Coord, bool bWall);

	// Places the start cell, then the goal cell, then clears whichever was clicked again.
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Edit")
	void ToggleBeginEndAt(FIntPoint Coord);

	// Sets a cell's tile type by index into the tile set. Ignored once the search has started.
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Edit")
	void SetTileAt(FIntPoint Coord, int32 TileIndex);

	// Tile index in a cell, or INDEX_NONE if Coord is off the grid.
	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	int32 GetTileAt(FIntPoint Coord) const;

	// Moves the start cell to Coord, replacing any previous one. False if Coord is unusable.
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Edit")
	bool SetStartCell(FIntPoint Coord);

	// Moves the goal cell to Coord, replacing any previous one. False if Coord is unusable.
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Edit")
	bool SetEndCell(FIntPoint Coord);

	// --- Running ---

	// Advances the search by one cell and reports what happened.
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Run")
	EPathStepResult StepOnce();

	// Steps until the search finishes or MaxIterations is hit.
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Run")
	EPathStepResult SolveAll(int32 MaxIterations = 10000);

	// Resizes the grid to the current HorizontalCells/VerticalCells and clears it.
	UFUNCTION(BlueprintCallable, Category = "PathFinding|Grid")
	void RebuildGrid();

	// --- Queries ---

	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	bool IsValidCoord(FIntPoint Coord) const;

	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	EPathCellState GetCellState(FIntPoint Coord) const;

	// Copies out a cell's full A* data. False if Coord is off the grid.
	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	bool GetCellData(FIntPoint Coord, FPathCell& OutCell) const;

	// The final path in grid coordinates, start -> goal. Empty until the path is found.
	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	TArray<FIntPoint> GetFinalPath() const;

	// The final path as world locations, ready to drive an actor along.
	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	TArray<FVector> GetFinalPathWorld() const;

	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	EPathStepResult GetStatus() const;

	// Total cost of the final path, counting every step. 0 until a path is found.
	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	int32 GetPathCost() const;

	// The algorithm actually running, which may differ from Algorithm if it fell back.
	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	EPathAlgorithm GetActiveAlgorithm() const;

	// Start cell coordinate, or (-1, -1) when unset.
	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	FIntPoint GetStartCoord() const { return StartCoord; }

	// Goal cell coordinate, or (-1, -1) when unset.
	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	FIntPoint GetEndCoord() const { return EndCoord; }

	UFUNCTION(BlueprintPure, Category = "PathFinding|Query")
	FColor GetStateColor(EPathCellState State) const;

	// --- Coordinate conversion ---

	// World location to grid coordinate. May return an off-grid coordinate.
	UFUNCTION(BlueprintPure, Category = "PathFinding|Grid")
	FIntPoint WorldToGrid(FVector WorldLocation) const;

	// Centre of a cell in world space, at DrawHeight.
	UFUNCTION(BlueprintPure, Category = "PathFinding|Grid")
	FVector GridToWorld(FIntPoint Coord) const;

private:

	// Durable grid data. Never written to while Search is running.
	FPathGrid Grid;

	// The one search this component visualises. A second search would get its own.
	FGridSearch Search;

	FIntPoint StartCoord = FIntPoint(-1, -1);
	FIntPoint EndCoord = FIntPoint(-1, -1);

	// True once Search.Begin has been called, which is also when editing locks
	bool bSearchBegun = false;

	FGridPathQuery MakeQuery() const;

	// Draws the cells in the world
	void DrawCells() const;
	// Prints to the screen when bShowDebugMessages is on
	void ShowMessage(const FString& Message) const;
};
