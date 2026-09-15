#include "GridPathManager.h"
#include "Async/Async.h"

void UGridPathManager::ConfigureGrid(int32 Width, int32 Height, UPathTileSet* TileSet,
	float InCellSize, FVector InGridOrigin)
{
	CellSize = FMath::Max(KINDA_SMALL_NUMBER, InCellSize);
	GridOrigin = InGridOrigin;
	ActorPaths.Empty();

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

	// Terrain just moved under anything that was following a route through it
	RevalidateActorPaths();
}

FIntPoint UGridPathManager::WorldToGrid(FVector WorldLocation) const
{
	const FVector Local = WorldLocation - GridOrigin;
	return FIntPoint(
		FMath::FloorToInt32((Local.X + CellSize / 2.f) / CellSize),
		FMath::FloorToInt32((-Local.Y + CellSize / 2.f) / CellSize));
}

FVector UGridPathManager::GridToWorld(FIntPoint Coord) const
{
	return GridOrigin + FVector(Coord.X * CellSize, -Coord.Y * CellSize, 0.f);
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

//////////////////////  Actor paths

void UGridPathManager::RegisterActorPath(AActor* Actor, const TArray<FIntPoint>& Path,
	const TArray<int32>& TilesToIgnore, const TArray<int32>& RestrictedTiles,
	EPathAlgorithm Algorithm, bool bEightDirections)
{
	if (!Actor)
	{
		return;
	}

	FRegisteredActorPath Registration;
	Registration.Path = Path;
	Registration.Algorithm = Algorithm;
	Registration.TilesToIgnore = TilesToIgnore;
	Registration.RestrictedTiles = RestrictedTiles;
	Registration.bEightDirections = bEightDirections;

	ActorPaths.Add(Actor, MoveTemp(Registration));
}

void UGridPathManager::UnRegisterActorPath(AActor* Actor)
{
	ActorPaths.Remove(Actor);
}

TArray<FIntPoint> UGridPathManager::GetActorPath(AActor* Actor) const
{
	if (const FRegisteredActorPath* Found = ActorPaths.Find(Actor))
	{
		return Found->Path;
	}

	return TArray<FIntPoint>();
}

bool UGridPathManager::IsActorRegistered(AActor* Actor) const
{
	return Actor != nullptr && ActorPaths.Contains(Actor);
}

void UGridPathManager::PruneActorPaths()
{
	for (auto It = ActorPaths.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

int32 UGridPathManager::GetRegisteredActorCount()
{
	PruneActorPaths();
	return ActorPaths.Num();
}

FGridPathQuery UGridPathManager::QueryForRegistration(const FRegisteredActorPath& Registration,
	FIntPoint Start, FIntPoint Goal) const
{
	return MakeQuery(Start, Goal, Registration.Algorithm, Registration.TilesToIgnore,
		Registration.RestrictedTiles, Registration.bEightDirections, 1.0f);
}

TArray<AActor*> UGridPathManager::RevalidateActorPaths()
{
	PruneActorPaths();

	TArray<AActor*> Invalidated;

	for (const TPair<TWeakObjectPtr<AActor>, FRegisteredActorPath>& Entry : ActorPaths)
	{
		AActor* Actor = Entry.Key.Get();
		if (!Actor)
		{
			continue;
		}

		const FGridPathQuery Query = QueryForRegistration(Entry.Value,
			Entry.Value.Path.Num() > 0 ? Entry.Value.Path[0] : FIntPoint(-1, -1),
			Entry.Value.Path.Num() > 0 ? Entry.Value.Path.Last() : FIntPoint(-1, -1));

		const int32 Blocked = FirstBlockedOnPath(Grid, Entry.Value.Path, Query);
		if (Blocked != INDEX_NONE)
		{
			Invalidated.Add(Actor);
			OnActorPathInvalidated.Broadcast(Actor, Entry.Value.Path[Blocked]);
		}
	}

	return Invalidated;
}

FGridPathResult UGridPathManager::ReplanActorPath(AActor* Actor, int32 SegmentPath)
{
	FGridPathResult Result;

	const FRegisteredActorPath* Found = Actor ? ActorPaths.Find(Actor) : nullptr;
	if (!Found || Found->Path.Num() == 0)
	{
		return Result;
	}

	// Re-plan from where the actor actually is, not from where its old route began
	const FIntPoint From = WorldToGrid(Actor->GetActorLocation());
	const FIntPoint To = Found->Path.Last();

	const FRegisteredActorPath Registration = *Found;
	Result = RunGridPathQuery(Grid, QueryForRegistration(Registration, From, To), SegmentPath);

	if (Result.Status == EPathStepResult::PathFound)
	{
		FRegisteredActorPath Updated = Registration;
		Updated.Path = Result.Path;
		ActorPaths.Add(Actor, MoveTemp(Updated));
	}

	return Result;
}
