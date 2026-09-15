#include "GridPathManager.h"
#include "Async/Async.h"

void UGridPathManager::ConfigureGrid(int32 Width, int32 Height, UPathTileSet* TileSet)
{
	TArray<FPathTileInfo> Table;
	if (TileSet)
	{
		for (const FPathTileDef& Def : TileSet->Tiles)
		{
			Table.Add(FPathTileInfo(Def.CostMultiplier, Def.bPassable, Def.Color));
		}
	}

	Grid.SetTileTable(Table);
	Grid.Resize(Width, Height);
}

void UGridPathManager::SetTileAt(FIntPoint Coord, int32 TileIndex)
{
	Grid.SetTile(Coord, (uint8)TileIndex);
}

void UGridPathManager::UpdateWalkableTiles(const TArray<FIntPoint>& Coords, int32 TileIndex)
{
	for (const FIntPoint& Coord : Coords)
	{
		Grid.SetTile(Coord, (uint8)TileIndex);
	}
}

int32 UGridPathManager::GetTileAt(FIntPoint Coord) const
{
	const int32 Index = Grid.CoordToIndex(Coord);
	return Index == INDEX_NONE ? INDEX_NONE : (int32)Grid.TileAt(Index);
}

bool UGridPathManager::IsValidCoord(FIntPoint Coord) const
{
	return Grid.IsValidCoord(Coord);
}

FIntPoint UGridPathManager::GetGridSize() const
{
	return FIntPoint(Grid.Width, Grid.Height);
}

FGridPathQuery UGridPathManager::MakeQuery(FIntPoint Start, FIntPoint Goal,
	EPathAlgorithm Algorithm, const TArray<int32>& TilesToIgnore,
	const TArray<int32>& RestrictedTiles, bool bEightDirections, float HeuristicWeight) const
{
	FGridPathQuery Query;
	Query.Start = Start;
	Query.Goal = Goal;
	Query.Algorithm = Algorithm;
	Query.bAllowDiagonal = bEightDirections;
	Query.HeuristicWeight = FMath::Max(1.0f, HeuristicWeight);

	for (int32 Tile : TilesToIgnore)
	{
		Query.IgnoredTiles.Add((uint8)Tile);
	}

	for (int32 Tile : RestrictedTiles)
	{
		Query.RestrictedTiles.Add((uint8)Tile);
	}

	return Query;
}

FGridPathResult UGridPathManager::FindGridPathSync(FIntPoint Start, FIntPoint Goal,
	EPathAlgorithm Algorithm, const TArray<int32>& TilesToIgnore,
	const TArray<int32>& RestrictedTiles, int32 SegmentPath, bool bEightDirections,
	float HeuristicWeight)
{
	return RunGridPathQuery(Grid,
		MakeQuery(Start, Goal, Algorithm, TilesToIgnore, RestrictedTiles, bEightDirections, HeuristicWeight),
		SegmentPath);
}

void UGridPathManager::FindGridPathAsync(FIntPoint Start, FIntPoint Goal,
	EPathAlgorithm Algorithm, const TArray<int32>& TilesToIgnore,
	const TArray<int32>& RestrictedTiles, FGridPathCompletedSignature OnCompleted,
	int32 SegmentPath, bool bEightDirections, float HeuristicWeight)
{
	// Copy the grid so the game thread stays free to edit tiles while this runs. The answer
	// describes the grid as it was when the query started, which is the honest contract for
	// anything asynchronous.
	FPathGrid Snapshot = Grid;
	const FGridPathQuery Query = MakeQuery(Start, Goal, Algorithm, TilesToIgnore,
		RestrictedTiles, bEightDirections, HeuristicWeight);

	PendingQueries.Increment();

	TWeakObjectPtr<UGridPathManager> WeakThis(this);

	Async(EAsyncExecution::ThreadPool,
		[Snapshot = MoveTemp(Snapshot), Query, SegmentPath, OnCompleted, WeakThis]()
		{
			FGridPathResult Result = RunGridPathQuery(Snapshot, Query, SegmentPath);

			AsyncTask(ENamedThreads::GameThread,
				[Result = MoveTemp(Result), OnCompleted, WeakThis]()
				{
					if (!WeakThis.IsValid())
					{
						return; // world went away, nothing left to answer to
					}

					WeakThis->PendingQueries.Decrement();
					OnCompleted.ExecuteIfBound(Result);
				});
		});
}

int32 UGridPathManager::GetPendingQueryCount() const
{
	return PendingQueries.GetValue();
}
