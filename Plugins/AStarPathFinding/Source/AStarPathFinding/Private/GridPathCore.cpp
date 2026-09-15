#include "GridPathCore.h"

//////////////////////  FPathGrid

TArray<FPathTileInfo> FPathGrid::DefaultTileTable()
{
	// 0 open, 1 impassable - the binary wall model everything used before tile types
	return { FPathTileInfo(1, true, FColor::White), FPathTileInfo(1, false, FColor::Black) };
}

bool FPathGrid::HasUniformCost() const
{
	for (const FPathTileInfo& Info : TileTable)
	{
		if (Info.bPassable && Info.CostMultiplier != 1)
		{
			return false;
		}
	}

	return true;
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

	Algorithm = EPathAlgorithm::AStar;
	bAlgorithmFellBack = false;
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

	// JPS prunes on the assumption that every move costs the same and that diagonals are
	// available. Neither holds on a weighted or 4-connected grid, and running it anyway
	// returns confidently wrong paths, so substitute A* and say so.
	Algorithm = Query.Algorithm;
	if (Algorithm == EPathAlgorithm::JumpPointSearch
		&& (!Query.bAllowDiagonal || !Grid.HasUniformCost()))
	{
		Algorithm = EPathAlgorithm::AStar;
		bAlgorithmFellBack = true;
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

		if (StartIndex == GoalIndex)
		{
			Status = EPathStepResult::PathFound;
		}
		else
		{
			Expand(Grid, StartIndex);
			Status = EPathStepResult::InProgress;
		}
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
	if (Index == INDEX_NONE)
	{
		return false;
	}

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

void FGridSearch::Expand(const FPathGrid& Grid, int32 CenterIndex)
{
	if (Algorithm == EPathAlgorithm::JumpPointSearch)
	{
		ExpandJumpPoints(Grid, CenterIndex);
	}
	else
	{
		ExpandAStar(Grid, CenterIndex);
	}
}

void FGridSearch::ExpandAStar(const FPathGrid& Grid, int32 CenterIndex)
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

			const FIntPoint NeighbourCoord(CenterCoord.X + i, CenterCoord.Y + j);
			const int32 NeighbourIndex = Grid.CoordToIndex(NeighbourCoord);

			if (!CanEnter(Grid, NeighbourIndex))
			{
				continue;
			}

			if (Membership[NeighbourIndex] == EPathCellState::Closed)
			{
				continue; // Already expanded
			}

			Relax(Grid, CenterIndex, NeighbourIndex, Query.Distance(CenterCoord, NeighbourCoord));
		}
	}
}

void FGridSearch::Relax(const FPathGrid& Grid, int32 FromIndex, int32 ToIndex, int32 StepCost)
{
	// Entering a tile costs the move scaled by that tile's multiplier. Clamped at 1 because
	// a cheaper-than-base tile would make the heuristic overestimate and lose optimality.
	const int32 Multiplier = FMath::Max(1, Grid.TileInfoAt(ToIndex).CostMultiplier);
	const int32 NewStartDist = StartDist[FromIndex] + StepCost * Multiplier;

	if (StartDist[ToIndex] < 0 || NewStartDist < StartDist[ToIndex])
	{
		StartDist[ToIndex] = NewStartDist;
		Weight[ToIndex] = NewStartDist + Query.Distance(Grid.IndexToCoord(ToIndex), Query.Goal);
		Parent[ToIndex] = FromIndex;
		Membership[ToIndex] = EPathCellState::Open;
		OpenCells.AddUnique(ToIndex);
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

	// The goal is an ordinary node: reaching it is only settled once it is selected, which
	// is what makes the total cost - including the final step - actually optimal.
	if (LightestIndex == GoalIndex)
	{
		return EPathStepResult::PathFound;
	}

	Expand(Grid, LightestIndex);

	return EPathStepResult::InProgress;
}

void FGridSearch::PaintPath()
{
	for (int32 Index = GoalIndex; Index != INDEX_NONE; Index = Parent[Index])
	{
		Membership[Index] = EPathCellState::Path;
	}
}

//////////////////////  Jump Point Search

void FGridSearch::PrunedDirections(const FPathGrid& Grid, FIntPoint Coord, int32 dx, int32 dy,
	TArray<FIntPoint>& OutDirections) const
{
	const auto Blocked = [&](int32 X, int32 Y)
	{
		return !CanEnter(Grid, Grid.CoordToIndex({ X, Y }));
	};

	if (dx != 0 && dy != 0)
	{
		// Natural successors of a diagonal move
		OutDirections.Add({ dx, 0 });
		OutDirections.Add({ 0, dy });
		OutDirections.Add({ dx, dy });

		// Forced by an obstacle behind us on either axis
		if (Blocked(Coord.X - dx, Coord.Y))
		{
			OutDirections.Add({ -dx, dy });
		}
		if (Blocked(Coord.X, Coord.Y - dy))
		{
			OutDirections.Add({ dx, -dy });
		}
	}
	else if (dx != 0)
	{
		OutDirections.Add({ dx, 0 });

		if (Blocked(Coord.X, Coord.Y + 1))
		{
			OutDirections.Add({ dx, 1 });
		}
		if (Blocked(Coord.X, Coord.Y - 1))
		{
			OutDirections.Add({ dx, -1 });
		}
	}
	else
	{
		OutDirections.Add({ 0, dy });

		if (Blocked(Coord.X + 1, Coord.Y))
		{
			OutDirections.Add({ 1, dy });
		}
		if (Blocked(Coord.X - 1, Coord.Y))
		{
			OutDirections.Add({ -1, dy });
		}
	}
}

bool FGridSearch::HasForcedNeighbour(const FPathGrid& Grid, FIntPoint Coord, int32 dx, int32 dy) const
{
	const auto Open = [&](int32 X, int32 Y)
	{
		return CanEnter(Grid, Grid.CoordToIndex({ X, Y }));
	};

	if (dx != 0 && dy != 0)
	{
		return (!Open(Coord.X - dx, Coord.Y) && Open(Coord.X - dx, Coord.Y + dy))
			|| (!Open(Coord.X, Coord.Y - dy) && Open(Coord.X + dx, Coord.Y - dy));
	}

	if (dx != 0)
	{
		return (!Open(Coord.X, Coord.Y + 1) && Open(Coord.X + dx, Coord.Y + 1))
			|| (!Open(Coord.X, Coord.Y - 1) && Open(Coord.X + dx, Coord.Y - 1));
	}

	return (!Open(Coord.X + 1, Coord.Y) && Open(Coord.X + 1, Coord.Y + dy))
		|| (!Open(Coord.X - 1, Coord.Y) && Open(Coord.X - 1, Coord.Y + dy));
}

int32 FGridSearch::Jump(const FPathGrid& Grid, int32 FromIndex, int32 dx, int32 dy) const
{
	FIntPoint Coord = Grid.IndexToCoord(FromIndex);

	while (true)
	{
		Coord = FIntPoint(Coord.X + dx, Coord.Y + dy);
		const int32 Index = Grid.CoordToIndex(Coord);

		if (!CanEnter(Grid, Index))
		{
			return INDEX_NONE;
		}

		if (Index == GoalIndex || HasForcedNeighbour(Grid, Coord, dx, dy))
		{
			return Index;
		}

		// A diagonal run is a jump point if either straight component finds one
		if (dx != 0 && dy != 0)
		{
			if (Jump(Grid, Index, dx, 0) != INDEX_NONE || Jump(Grid, Index, 0, dy) != INDEX_NONE)
			{
				return Index;
			}
		}
	}
}

void FGridSearch::ExpandJumpPoints(const FPathGrid& Grid, int32 CenterIndex)
{
	const FIntPoint CenterCoord = Grid.IndexToCoord(CenterIndex);

	TArray<FIntPoint> Directions;
	const int32 ParentIndex = Parent[CenterIndex];

	if (ParentIndex == INDEX_NONE)
	{
		// The start has no arrival direction, so every direction is worth scanning
		for (int32 j = -1; j <= 1; j++)
		{
			for (int32 i = -1; i <= 1; i++)
			{
				if (i != 0 || j != 0)
				{
					Directions.Add({ i, j });
				}
			}
		}
	}
	else
	{
		const FIntPoint ParentCoord = Grid.IndexToCoord(ParentIndex);
		PrunedDirections(Grid, CenterCoord,
			FMath::Clamp(CenterCoord.X - ParentCoord.X, -1, 1),
			FMath::Clamp(CenterCoord.Y - ParentCoord.Y, -1, 1),
			Directions);
	}

	for (const FIntPoint& Direction : Directions)
	{
		const int32 JumpIndex = Jump(Grid, CenterIndex, Direction.X, Direction.Y);

		if (JumpIndex == INDEX_NONE || Membership[JumpIndex] == EPathCellState::Closed)
		{
			continue;
		}

		Relax(Grid, CenterIndex, JumpIndex,
			Query.Distance(CenterCoord, Grid.IndexToCoord(JumpIndex)));
	}
}

TArray<FIntPoint> FGridSearch::BuildPath(const FPathGrid& Grid) const
{
	TArray<FIntPoint> Path;

	if (Status != EPathStepResult::PathFound || GoalIndex == INDEX_NONE)
	{
		return Path;
	}

	// Walk the parent chain back from the goal
	TArray<FIntPoint> Chain;
	for (int32 Index = GoalIndex; Index != INDEX_NONE; Index = Parent[Index])
	{
		Chain.Add(Grid.IndexToCoord(Index));
	}
	Algo::Reverse(Chain);

	// Consecutive chain entries are adjacent under A*, but a whole straight or diagonal run
	// apart under JPS, so fill the gaps. A no-op for A*.
	Path.Add(Chain[0]);
	for (int32 i = 1; i < Chain.Num(); i++)
	{
		FIntPoint Current = Chain[i - 1];
		const FIntPoint Target = Chain[i];
		const int32 StepX = FMath::Clamp(Target.X - Current.X, -1, 1);
		const int32 StepY = FMath::Clamp(Target.Y - Current.Y, -1, 1);

		while (Current != Target)
		{
			Current = FIntPoint(Current.X + StepX, Current.Y + StepY);
			Path.Add(Current);
		}
	}

	return Path;
}

int32 FGridSearch::GetPathCost(const FPathGrid& Grid) const
{
	const TArray<FIntPoint> Path = BuildPath(Grid);

	int32 Cost = 0;
	for (int32 i = 1; i < Path.Num(); i++)
	{
		const int32 Multiplier = FMath::Max(1, Grid.TileInfoAt(Grid.CoordToIndex(Path[i])).CostMultiplier);
		Cost += Query.Distance(Path[i - 1], Path[i]) * Multiplier;
	}

	return Cost;
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
