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

		// A serpentine forcing a long detour: the reported "why does it check the whole
		// empty space" case. Direct distance 234, real route 390, so every cell whose
		// estimate is under 390 has to be ruled out first.
		const auto Serpentine = []()
		{
			FPathGrid G;
			G.Resize(20, 12);
			for (int32 y = 0; y <= 9; y++)  { G.SetBlocked({ 15, y }, true); }
			for (int32 y = 2; y <= 11; y++) { G.SetBlocked({ 10, y }, true); }
			for (int32 y = 0; y <= 9; y++)  { G.SetBlocked({ 5, y }, true); }
			return G;
		};

		FGridPathQuery Snake;
		Snake.Start = { 19, 0 };
		Snake.Goal = { 0, 11 };

		FPathGrid Snake1 = Serpentine();
		Shot(TEXT("serpentine-astar"), Snake1, Snake);

		FPathGrid Snake2 = Serpentine();
		Snake.HeuristicWeight = 1.5f;
		Shot(TEXT("serpentine-weighted"), Snake2, Snake);
		Snake.HeuristicWeight = 1.0f;

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

	// Scaling benchmark. Logged, never asserted - wall-clock assertions are flaky on
	// shared machines, but a regression is obvious the moment you read the numbers.
	{
		const auto Bench = [](int32 Size, bool bWall, EPathAlgorithm Algo, const TCHAR* Label)
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

			FGridPathQuery Q;
			Q.Start = { 0, 0 };
			Q.Goal = { Size - 1, Size - 1 };
			Q.Algorithm = Algo;

			const double T0 = FPlatformTime::Seconds();
			FGridSearch S;
			S.Begin(G, Q);
			S.Solve(G, 100000000);
			const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;

			UE_LOG(LogPathFindingTest, Display,
				TEXT("BENCH %-8s %4dx%-4d cells=%7d  expanded=%6d  %9.2f ms  status=%d"),
				Label, Size, Size, G.Num(), S.GetExpandedCount(), Ms, (int32)S.GetStatus());
		};

		for (int32 Size : { 50, 100, 200, 400 })
		{
			Bench(Size, true, EPathAlgorithm::AStar, TEXT("A*wall"));
		}
		for (int32 Size : { 50, 100, 200, 400 })
		{
			Bench(Size, true, EPathAlgorithm::JumpPointSearch, TEXT("JPSwall"));
		}
	}

	return Failures.Num();
#else
	UE_LOG(LogPathFindingTest, Error, TEXT("Built without WITH_DEV_AUTOMATION_TESTS, nothing to run."));
	return 1;
#endif
}
