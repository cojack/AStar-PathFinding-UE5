#include "GridPathCore.h"

//////////////////////  FPathGrid

TArray<FPathTileInfo> FPathGrid::DefaultTileTable()
{
	// 0 open, 1 impassable - the binary wall model everything used before tile types
	return { FPathTileInfo(1, true, FColor::White), FPathTileInfo(1, false, FColor::Black) };
}

void FPathGrid::SetTileTable(const TArray<FPathTileInfo>& InTable)
{
	TileTable = InTable.Num() > 0 ? InTable : DefaultTileTable();

	DefaultTile = 0;

	// "Wall" means the first tile nothing can enter. Without one, the legacy wall API is a
	// no-op rather than silently writing a walkable tile.
	const int32 FirstImpassable = TileTable.IndexOfByPredicate(
		[](const FPathTileInfo& Info) { return !Info.bPassable; });
	WallTile = FirstImpassable == INDEX_NONE ? DefaultTile : (uint8)FirstImpassable;

	ClearTiles();
}

void FPathGrid::Resize(int32 InWidth, int32 InHeight)
{
	Width = FMath::Max(1, InWidth);
	Height = FMath::Max(1, InHeight);

	if (TileTable.Num() == 0)
	{
		SetTileTable(DefaultTileTable());
	}

	Tiles.Empty();
	Tiles.SetNum(Width * Height);
	ClearTiles();
}

void FPathGrid::ClearTiles()
{
	for (int32 i = 0; i < Tiles.Num(); i++)
	{
		Tiles[i] = DefaultTile;
	}
}

uint8 FPathGrid::TileAt(int32 Index) const
{
	return Tiles.IsValidIndex(Index) ? Tiles[Index] : DefaultTile;
}

void FPathGrid::SetTileAtIndex(int32 Index, uint8 Tile)
{
	if (Tiles.IsValidIndex(Index) && TileTable.IsValidIndex(Tile))
	{
		Tiles[Index] = Tile;
	}
}

void FPathGrid::SetTile(FIntPoint Coord, uint8 Tile)
{
	SetTileAtIndex(CoordToIndex(Coord), Tile);
}

const FPathTileInfo& FPathGrid::TileInfoFor(uint8 Tile) const
{
	static const FPathTileInfo Fallback;
	if (TileTable.Num() == 0)
	{
		return Fallback;
	}

	return TileTable.IsValidIndex(Tile) ? TileTable[Tile] : TileTable[0];
}

const FPathTileInfo& FPathGrid::TileInfoAt(int32 Index) const
{
	return TileInfoFor(TileAt(Index));
}

bool FPathGrid::IsValidCoord(FIntPoint Coord) const
{
	return Coord.X >= 0 && Coord.X < Width
		&& Coord.Y >= 0 && Coord.Y < Height;
}

int32 FPathGrid::CoordToIndex(FIntPoint Coord) const
{
	if (!IsValidCoord(Coord))
	{
		return INDEX_NONE;
	}

	const int32 Index = Coord.Y * Width + Coord.X;

	return Tiles.IsValidIndex(Index) ? Index : INDEX_NONE;
}

FIntPoint FPathGrid::IndexToCoord(int32 Index) const
{
	const int32 SafeWidth = FMath::Max(1, Width);
	return FIntPoint(Index % SafeWidth, Index / SafeWidth);
}

bool FPathGrid::IsBlocked(FIntPoint Coord) const
{
	return IsBlockedIndex(CoordToIndex(Coord));
}

bool FPathGrid::IsBlockedIndex(int32 Index) const
{
	return Tiles.IsValidIndex(Index) && !TileInfoAt(Index).bPassable;
}

void FPathGrid::SetBlocked(FIntPoint Coord, bool bBlocked)
{
	SetTile(Coord, bBlocked ? WallTile : DefaultTile);
}

//////////////////////  FGridPathQuery

int32 FGridPathQuery::Distance(FIntPoint A, FIntPoint B) const
{
	const int32 DifX = FMath::Abs(A.X - B.X);
	const int32 DifY = FMath::Abs(A.Y - B.Y);

	if (!bAllowDiagonal)
	{
		// Manhattan, otherwise the heuristic would under-run a grid that cannot cut corners
		return (DifX + DifY) * StraightCost;
	}

	// Diagonal moves cover both axes at once, the rest are straight
	const int32 MovDiag = FMath::Min(DifX, DifY);
	const int32 MovStraight = FMath::Abs(DifX - DifY);

	return MovDiag * DiagonalCost + MovStraight * StraightCost;
}

//////////////////////  FGridSearch

void FGridSearch::Reset()
{
	Weight.Empty();
	StartDist.Empty();
	Parent.Empty();
	Membership.Empty();

	OpenCells.Empty();
	ClosedCells.Empty();

	StartIndex = INDEX_NONE;
	GoalIndex = INDEX_NONE;

	bStarted = false;
	bEnded = false;
	Status = EPathStepResult::NotStarted;
}

bool FGridSearch::Begin(const FPathGrid& Grid, const FGridPathQuery& InQuery)
{
	Reset();

	Query = InQuery;
	StartIndex = Grid.CoordToIndex(Query.Start);
	GoalIndex = Grid.CoordToIndex(Query.Goal);

	if (StartIndex == INDEX_NONE || GoalIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 CellCount = Grid.Num();
	Weight.Init(-1, CellCount);
	StartDist.Init(-1, CellCount);
	Parent.Init(INDEX_NONE, CellCount);
	Membership.Init(EPathCellState::Empty, CellCount);

	StartDist[StartIndex] = 0;

	return true;
}

EPathStepResult FGridSearch::Step(const FPathGrid& Grid)
{
	if (bEnded || StartIndex == INDEX_NONE || GoalIndex == INDEX_NONE)
	{
		return Status;
	}

	if (!bStarted)
	{
		bStarted = true;
		// The start can already touch the goal, in which case there is nothing to expand
		Status = ExpandNeighbours(Grid, StartIndex) ? EPathStepResult::PathFound : EPathStepResult::InProgress;
	}
	else
	{
		Status = SelectLightest(Grid);
	}

	if (Status == EPathStepResult::PathFound)
	{
		PaintPath();
		bEnded = true;
	}
	else if (Status == EPathStepResult::NoPath)
	{
		bEnded = true;
	}

	return Status;
}

EPathStepResult FGridSearch::Solve(const FPathGrid& Grid, int32 MaxIterations)
{
	for (int32 i = 0; i < MaxIterations && !bEnded; i++)
	{
		Step(Grid);
	}

	return Status;
}

bool FGridSearch::CanEnter(const FPathGrid& Grid, int32 Index) const
{
	const uint8 Tile = Grid.TileAt(Index);

	// A restriction always wins: it is the query refusing a tile it could otherwise use
	if (Query.RestrictedTiles.Contains(Tile))
	{
		return false;
	}

	// An ignored tile is one this query walks through regardless of its definition
	if (Query.IgnoredTiles.Contains(Tile))
	{
		return true;
	}

	return Grid.TileInfoFor(Tile).bPassable;
}

bool FGridSearch::ExpandNeighbours(const FPathGrid& Grid, int32 CenterIndex)
{
	const FIntPoint CenterCoord = Grid.IndexToCoord(CenterIndex);

	for (int32 j = -1; j <= 1; j++)
	{
		for (int32 i = -1; i <= 1; i++)
		{
			if (i == 0 && j == 0)
			{
				continue; // Skip the center cell
			}

			if (!Query.bAllowDiagonal && i != 0 && j != 0)
			{
				continue; // Skip the corners
			}

			const int32 NeighbourIndex = Grid.CoordToIndex({ CenterCoord.X + i, CenterCoord.Y + j });

			if (NeighbourIndex == INDEX_NONE)
			{
				continue;
			}

			if (NeighbourIndex == GoalIndex)
			{
				return true; // Goal found. Deliberately not evaluated, so it has no parent
			}

			if (!CanEnter(Grid, NeighbourIndex))
			{
				continue;
			}

			const EPathCellState State = Membership[NeighbourIndex];
			if (State != EPathCellState::Empty && State != EPathCellState::Open)
			{
				continue; // Already expanded
			}

			Evaluate(Grid, CenterIndex, NeighbourIndex);

			OpenCells.AddUnique(NeighbourIndex);
		}
	}

	return false;
}

void FGridSearch::Evaluate(const FPathGrid& Grid, int32 FromIndex, int32 ToIndex)
{
	const FIntPoint FromCoord = Grid.IndexToCoord(FromIndex);
	const FIntPoint ToCoord = Grid.IndexToCoord(ToIndex);

	// Entering a tile costs the base move scaled by that tile's multiplier. Clamped at 1
	// because a cheaper-than-base tile would make the heuristic overestimate and the
	// result would stop being optimal.
	const int32 Multiplier = FMath::Max(1, Grid.TileInfoAt(ToIndex).CostMultiplier);
	const int32 NewStartDist = StartDist[FromIndex] + Query.Distance(FromCoord, ToCoord) * Multiplier;

	if (StartDist[ToIndex] < 0 || NewStartDist < StartDist[ToIndex])
	{
		StartDist[ToIndex] = NewStartDist;
		Weight[ToIndex] = NewStartDist + Query.Distance(ToCoord, Query.Goal);
		Parent[ToIndex] = FromIndex;
		Membership[ToIndex] = EPathCellState::Open;
	}
}

// ponytail: linear scan over the open set, fine for editor-sized grids.
// Swap for a binary heap keyed on Weight if the grid ever gets big.
EPathStepResult FGridSearch::SelectLightest(const FPathGrid& Grid)
{
	if (OpenCells.Num() == 0)
	{
		return EPathStepResult::NoPath; // Nothing left to expand
	}

	int32 LightestIndex = OpenCells[0];

	for (int32 i = 0; i < OpenCells.Num(); i++)
	{
		const int32 Candidate = OpenCells[i];

		if (Weight[LightestIndex] > Weight[Candidate])
		{
			LightestIndex = Candidate;
		}
		// On a tie, prefer the cell closer to the goal
		else if (Weight[LightestIndex] == Weight[Candidate])
		{
			const int32 LightToGoal = Query.Distance(Grid.IndexToCoord(LightestIndex), Query.Goal);
			const int32 OtherToGoal = Query.Distance(Grid.IndexToCoord(Candidate), Query.Goal);
			if (LightToGoal > OtherToGoal)
			{
				LightestIndex = Candidate;
			}
		}
	}

	ClosedCells.Add(LightestIndex);
	Membership[LightestIndex] = EPathCellState::Closed;
	OpenCells.Remove(LightestIndex);

	return ExpandNeighbours(Grid, LightestIndex) ? EPathStepResult::PathFound : EPathStepResult::InProgress;
}

void FGridSearch::PaintPath()
{
	if (ClosedCells.Num() == 0)
	{
		return; // The start touched the goal directly, nothing in between
	}

	for (int32 Index = ClosedCells.Last(); Index != INDEX_NONE; Index = Parent[Index])
	{
		Membership[Index] = EPathCellState::Path;
	}
}

TArray<FIntPoint> FGridSearch::BuildPath(const FPathGrid& Grid) const
{
	TArray<FIntPoint> Path;

	if (Status != EPathStepResult::PathFound)
	{
		return Path;
	}

	if (ClosedCells.Num() > 0)
	{
		for (int32 Index = ClosedCells.Last(); Index != INDEX_NONE; Index = Parent[Index])
		{
			Path.Add(Grid.IndexToCoord(Index));
		}
		Algo::Reverse(Path); // Walked goal -> start, hand it back start -> goal
	}
	else if (StartIndex != INDEX_NONE)
	{
		Path.Add(Grid.IndexToCoord(StartIndex));
	}

	// The goal is never expanded, so it is never on the parent chain
	if (GoalIndex != INDEX_NONE)
	{
		Path.Add(Grid.IndexToCoord(GoalIndex));
	}

	return Path;
}

EPathCellState FGridSearch::GetMembership(int32 Index) const
{
	return Membership.IsValidIndex(Index) ? Membership[Index] : EPathCellState::Empty;
}

int32 FGridSearch::GetWeight(int32 Index) const
{
	return Weight.IsValidIndex(Index) ? Weight[Index] : -1;
}

int32 FGridSearch::GetStartDist(int32 Index) const
{
	return StartDist.IsValidIndex(Index) ? StartDist[Index] : -1;
}

int32 FGridSearch::GetParent(int32 Index) const
{
	return Parent.IsValidIndex(Index) ? Parent[Index] : INDEX_NONE;
}
