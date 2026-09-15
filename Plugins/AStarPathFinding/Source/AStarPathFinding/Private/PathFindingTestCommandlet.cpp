#include "PathFindingTestCommandlet.h"
#include "PathFindingChecks.h"
#include "PathFindingImageDump.h"

DEFINE_LOG_CATEGORY_STATIC(LogPathFindingTest, Log, All);

int32 UPathFindingTestCommandlet::Main(const FString& Params)
{
#if WITH_DEV_AUTOMATION_TESTS
	const TArray<FString> Failures = PathFindingChecks::Run();

	for (const FString& Failure : Failures)
	{
		UE_LOG(LogPathFindingTest, Error, TEXT("FAIL  %s"), *Failure);
	}

	if (Failures.Num() == 0)
	{
		UE_LOG(LogPathFindingTest, Display, TEXT("PathFinding checks passed."));
	}
	else
	{
		UE_LOG(LogPathFindingTest, Error, TEXT("PathFinding checks: %d failure(s)."), Failures.Num());
	}

	// Leave something to look at. Headless runs otherwise produce only pass/fail, and the
	// interesting part of a pathfinder is the shape of what it explored.
	{
		const auto Shot = [](const TCHAR* Name, FPathGrid& G, FGridPathQuery Q)
		{
			FGridSearch S;
			S.Begin(G, Q);
			S.Solve(G, 1000000);

			const FString Path = PathFindingImageDump::Save(G, S, Q.Start, Q.Goal, Name);
			UE_LOG(LogPathFindingTest, Display, TEXT("IMAGE %-14s expanded=%4d cost=%5d  %s"),
				Name, S.GetExpandedCount(), S.GetPathCost(G),
				Path.IsEmpty() ? TEXT("(failed to write)") : *Path);
		};

		const auto Plain = [](int32 Size, bool bWall)
		{
			FPathGrid G;
			G.Resize(Size, Size);
			if (bWall)
			{
				for (int32 y = 0; y < Size - 3; y++)
				{
					G.SetBlocked({ Size / 2, y }, true);
				}
			}
			return G;
		};

		FGridPathQuery Corner;
		Corner.Start = { 0, 0 };
		Corner.Goal = { 24, 24 };

		FPathGrid Open1 = Plain(25, false);
		Corner.Algorithm = EPathAlgorithm::AStar;
		Shot(TEXT("astar-open"), Open1, Corner);

		FPathGrid Open2 = Plain(25, false);
		Corner.Algorithm = EPathAlgorithm::JumpPointSearch;
		Shot(TEXT("jps-open"), Open2, Corner);

		FPathGrid Wall1 = Plain(25, true);
		Corner.Algorithm = EPathAlgorithm::AStar;
		Shot(TEXT("astar-wall"), Wall1, Corner);

		FPathGrid Wall2 = Plain(25, true);
		Corner.Algorithm = EPathAlgorithm::JumpPointSearch;
		Shot(TEXT("jps-wall"), Wall2, Corner);

		// The reported maze: 18x10, 4-connected, start top-right, goal top-left
		FPathGrid Maze;
		Maze.Resize(18, 10);
		const int32 WallColumns[] = { 3, 5, 7, 9, 11, 13 };
		for (int32 w = 0; w < 6; w++)
		{
			for (int32 y = 0; y < 10; y++)
			{
				const bool bGap = (w % 2 == 0) ? (y == 9) : (y == 0);
				if (!bGap)
				{
					Maze.SetBlocked({ WallColumns[w], y }, true);
				}
			}
		}
		FGridPathQuery MazeQuery;
		MazeQuery.Start = { 17, 0 };
		MazeQuery.Goal = { 0, 0 };
		MazeQuery.bAllowDiagonal = false;
		Shot(TEXT("reported-maze"), Maze, MazeQuery);

		// Weighted terrain: mud across the direct row
		FPathGrid Mud;
		Mud.SetTileTable({ FPathTileInfo(1, true, FColor::White),
						   FPathTileInfo(1, false, FColor(25, 25, 25)),
						   FPathTileInfo(6, true, FColor(120, 72, 32)) });
		Mud.Resize(16, 8);
		for (int32 x = 2; x < 14; x++)
		{
			Mud.SetTile({ x, 3 }, 2);
			Mud.SetTile({ x, 4 }, 2);
		}
		FGridPathQuery MudQuery;
		MudQuery.Start = { 1, 1 };
		MudQuery.Goal = { 14, 6 };
		Shot(TEXT("weighted-mud"), Mud, MudQuery);

		UE_LOG(LogPathFindingTest, Display, TEXT("IMAGE output directory: %s"),
			*PathFindingImageDump::OutputDir());
	}

	return Failures.Num();
#else
	UE_LOG(LogPathFindingTest, Error, TEXT("Built without WITH_DEV_AUTOMATION_TESTS, nothing to run."));
	return 1;
#endif
}
