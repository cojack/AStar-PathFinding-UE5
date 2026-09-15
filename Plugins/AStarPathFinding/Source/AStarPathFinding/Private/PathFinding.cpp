#include "PathFinding.h"
#include "Kismet/KismetSystemLibrary.h"

// Sets default values for this component's properties
UPathFinding::UPathFinding()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// Called when the game starts
void UPathFinding::BeginPlay()
{
	Super::BeginPlay();

	RebuildGrid();
}

// Called every frame
void UPathFinding::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	DrawCells();
}

//////////////////////

void UPathFinding::ToggleWall(FVector WorldCoord)
{
	ToggleWallAt(WorldToGrid(WorldCoord));
}

void UPathFinding::ToggleBeginEnd(FVector WorldCoord)
{
	ToggleBeginEndAt(WorldToGrid(WorldCoord));
}

void UPathFinding::NextIteration()
{
	// One step by default. Raise StepsPerIteration and this same button solves outright.
	if (StepsPerIteration <= 1)
	{
		StepOnce();
		return;
	}

	SolveAll(StepsPerIteration);
}

FGridPathQuery UPathFinding::MakeQuery() const
{
	FGridPathQuery Query;
	Query.Start = StartCoord;
	Query.Goal = EndCoord;
	Query.Algorithm = Algorithm;
	Query.HeuristicWeight = FMath::Max(1.0f, HeuristicWeight);
	Query.bAllowDiagonal = bAllowDiagonal;
	Query.StraightCost = StraightCost;
	Query.DiagonalCost = DiagonalCost;

	for (int32 Tile : IgnoredTiles)
	{
		Query.IgnoredTiles.Add((uint8)Tile);
	}

	for (int32 Tile : RestrictedTiles)
	{
		Query.RestrictedTiles.Add((uint8)Tile);
	}

	return Query;
}

EPathStepResult UPathFinding::StepOnce()
{
	if (!bSearchBegun)
	{
		if (!Grid.IsValidCoord(StartCoord) || !Grid.IsValidCoord(EndCoord))
		{
			ShowMessage(TEXT("Please place start and end positions (Right click)"));
			OnStepped.Broadcast(EPathStepResult::NotStarted);
			return EPathStepResult::NotStarted;
		}

		Search.Begin(Grid, MakeQuery());
		bSearchBegun = true;

		if (Search.DidAlgorithmFallBack())
		{
			ShowMessage(TEXT("Jump Point Search needs a uniform-cost grid with diagonals, using A*"));
		}
	}

	const EPathStepResult Result = Search.Step(Grid);

	switch (Result)
	{
	case EPathStepResult::PathFound:
		ShowMessage(TEXT("Path found"));
		OnPathFound.Broadcast(GetFinalPath());
		break;

	case EPathStepResult::NoPath:
		ShowMessage(TEXT("No path was found"));
		OnPathFailed.Broadcast();
		break;

	default:
		break;
	}

	OnStepped.Broadcast(Result);
	return Result;
}

EPathStepResult UPathFinding::SolveAll(int32 MaxIterations)
{
	for (int32 i = 0; i < MaxIterations; i++)
	{
		const EPathStepResult Result = StepOnce();

		if (Result == EPathStepResult::NotStarted
			|| Result == EPathStepResult::PathFound
			|| Result == EPathStepResult::NoPath)
		{
			return Result;
		}
	}

	return GetStatus();
}

//////////////////////

void UPathFinding::RebuildGrid()
{
	HorizontalCells = FMath::Max(1, HorizontalCells);
	VerticalCells = FMath::Max(1, VerticalCells);

	// Snapshot the tile asset into plain data. The search never touches the UObject.
	TArray<FPathTileInfo> Table;
	if (TileSet)
	{
		for (const FPathTileDef& Def : TileSet->Tiles)
		{
			Table.Add(FPathTileInfo(Def.CostMultiplier, Def.bPassable, Def.Color));
		}
	}
	Grid.SetTileTable(Table);

	Grid.Resize(HorizontalCells, VerticalCells);

	ResetCells();

	ShowMessage(FString::Printf(TEXT("There are %i cells"), Grid.Num()));
}

void UPathFinding::ResetCells()
{
	Grid.ClearTiles();
	Search.Reset();

	StartCoord = FIntPoint(-1, -1);
	EndCoord = FIntPoint(-1, -1);

	bSearchBegun = false;
}

void UPathFinding::DrawCells() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (int32 Index = 0; Index < Grid.Num(); Index++)
	{
		const FIntPoint Coord = Grid.IndexToCoord(Index);
		const FVector Center = GridToWorld(Coord);
		const EPathCellState State = GetCellState(Coord);

		// Draw plane to represent cell with color based on cell state. An untouched cell
		// shows its tile's colour when a tile set is supplied, so terrain stays readable.
		if (bDrawCells)
		{
			FColor CellColor = GetStateColor(State);
			if (TileSet && (State == EPathCellState::Empty || State == EPathCellState::Wall))
			{
				CellColor = Grid.TileInfoAt(Index).Color;
			}

			UKismetSystemLibrary::DrawDebugPlane(World, FPlane(0.f, 0.f, -1.f, 0.f), Center,
				(CellSize / 2) * CellDrawScale, CellColor);
		}

		// Display cell weights or start/end labels
		if (Coord == StartCoord)
		{
			if (bDrawStartEndLabels)
			{
				UKismetSystemLibrary::DrawDebugString(World, Center, FString("A"));
			}
		}
		else if (Coord == EndCoord)
		{
			if (bDrawStartEndLabels)
			{
				UKismetSystemLibrary::DrawDebugString(World, Center, FString("B"));
			}
		}
		else if (State != EPathCellState::Empty && State != EPathCellState::Wall)
		{
			// Draw weight
			if (bDrawWeights)
			{
				UKismetSystemLibrary::DrawDebugString(World, Center, FString::FromInt(Search.GetWeight(Index)));
			}

			// Draw arrow to parent cell
			const int32 ParentIndex = Search.GetParent(Index);
			if (bDrawParentArrows && ParentIndex != INDEX_NONE)
			{
				const FVector ParentCenter = GridToWorld(Grid.IndexToCoord(ParentIndex));
				const FVector MiddlePoint = (Center + ParentCenter) / 2;
				UKismetSystemLibrary::DrawDebugArrow(World, Center, MiddlePoint, CellSize, FColor::White);
			}
		}
	}
}

void UPathFinding::ShowMessage(const FString& Message) const
{
	if (bShowDebugMessages && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, Message);
	}
}

//////////////////////

bool UPathFinding::IsValidCoord(FIntPoint Coord) const
{
	return Grid.IsValidCoord(Coord);
}

EPathCellState UPathFinding::GetCellState(FIntPoint Coord) const
{
	const int32 Index = Grid.CoordToIndex(Coord);
	if (Index == INDEX_NONE)
	{
		return EPathCellState::Empty;
	}

	if (Grid.IsBlockedIndex(Index))
	{
		return EPathCellState::Wall;
	}

	// Start and goal stay highlighted for the whole run
	if (Coord == StartCoord || Coord == EndCoord)
	{
		return EPathCellState::Path;
	}

	return Search.GetMembership(Index);
}

bool UPathFinding::GetCellData(FIntPoint Coord, FPathCell& OutCell) const
{
	const int32 Index = Grid.CoordToIndex(Coord);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	OutCell.Weight = Search.GetWeight(Index);
	OutCell.StartDist = Search.GetStartDist(Index);
	OutCell.ParentIndex = Search.GetParent(Index);
	OutCell.State = GetCellState(Coord);

	return true;
}

TArray<FIntPoint> UPathFinding::GetFinalPath() const
{
	return Search.BuildPath(Grid);
}

TArray<FVector> UPathFinding::GetFinalPathWorld() const
{
	TArray<FVector> WorldPath;

	for (const FIntPoint& Coord : GetFinalPath())
	{
		WorldPath.Add(GridToWorld(Coord));
	}

	return WorldPath;
}

EPathStepResult UPathFinding::GetStatus() const
{
	return Search.GetStatus();
}

int32 UPathFinding::GetPathCost() const
{
	return Search.GetPathCost(Grid);
}

EPathAlgorithm UPathFinding::GetActiveAlgorithm() const
{
	return Search.GetAlgorithm();
}

FColor UPathFinding::GetStateColor(EPathCellState State) const
{
	switch (State)
	{
	case EPathCellState::Wall:		return WallColor;
	case EPathCellState::Open:		return OpenColor;
	case EPathCellState::Closed:	return ClosedColor;
	case EPathCellState::Path:		return PathColor;
	default:						return EmptyColor;
	}
}

FIntPoint UPathFinding::WorldToGrid(FVector WorldLocation) const
{
	if (CellSize <= 0.f)
	{
		return FIntPoint(-1, -1);
	}

	return FIntPoint(
		FMath::FloorToInt32((WorldLocation.X + CellSize / 2.f) / CellSize),
		FMath::FloorToInt32((-WorldLocation.Y + CellSize / 2.f) / CellSize));
}

FVector UPathFinding::GridToWorld(FIntPoint Coord) const
{
	return FVector(Coord.X * CellSize, -Coord.Y * CellSize, DrawHeight);
}

//////////////////////

void UPathFinding::ToggleWallAt(FIntPoint Coord)
{
	SetWallAt(Coord, !Grid.IsBlocked(Coord));
}

void UPathFinding::SetWallAt(FIntPoint Coord, bool bWall)
{
	if (bSearchBegun) // Only edit walls if pathfinding hasn't started
	{
		return;
	}

	if (!Grid.IsValidCoord(Coord) || Coord == StartCoord || Coord == EndCoord)
	{
		return;
	}

	Grid.SetBlocked(Coord, bWall);
}

void UPathFinding::ToggleBeginEndAt(FIntPoint Coord)
{
	if (bSearchBegun) // Only edit start/end if pathfinding hasn't started
	{
		return;
	}

	if (!Grid.IsValidCoord(Coord) || Grid.IsBlocked(Coord))
	{
		return; // Ignore if cell is a wall
	}

	// Set as start if start isn't set
	if (!Grid.IsValidCoord(StartCoord) && Coord != EndCoord)
	{
		StartCoord = Coord;
		return;
	}

	// Set as goal if goal isn't set
	if (!Grid.IsValidCoord(EndCoord) && Coord != StartCoord)
	{
		EndCoord = Coord;
		return;
	}

	// Remove start cell
	if (Coord == StartCoord)
	{
		StartCoord = FIntPoint(-1, -1);
		return;
	}

	// Remove goal cell
	if (Coord == EndCoord)
	{
		EndCoord = FIntPoint(-1, -1);
		return;
	}
}

void UPathFinding::SetTileAt(FIntPoint Coord, int32 TileIndex)
{
	if (bSearchBegun) // Only edit tiles if pathfinding hasn't started
	{
		return;
	}

	if (!Grid.IsValidCoord(Coord) || Coord == StartCoord || Coord == EndCoord)
	{
		return;
	}

	Grid.SetTile(Coord, (uint8)TileIndex);
}

int32 UPathFinding::GetTileAt(FIntPoint Coord) const
{
	const int32 Index = Grid.CoordToIndex(Coord);
	return Index == INDEX_NONE ? INDEX_NONE : (int32)Grid.TileAt(Index);
}

bool UPathFinding::SetStartCell(FIntPoint Coord)
{
	if (bSearchBegun || !Grid.IsValidCoord(Coord) || Coord == EndCoord || Grid.IsBlocked(Coord))
	{
		return false;
	}

	StartCoord = Coord;
	return true;
}

bool UPathFinding::SetEndCell(FIntPoint Coord)
{
	if (bSearchBegun || !Grid.IsValidCoord(Coord) || Coord == StartCoord || Grid.IsBlocked(Coord))
	{
		return false;
	}

	EndCoord = Coord;
	return true;
}
