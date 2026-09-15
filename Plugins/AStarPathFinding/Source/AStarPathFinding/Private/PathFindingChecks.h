#pragma once

#include "CoreMinimal.h"
#include "PathFinding.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Behaviour checks for UPathFinding, shared by the automation test and the test commandlet
 * so neither drifts from the other. Run() returns one string per failure; empty means pass.
 */
namespace PathFindingChecks
{
	inline UPathFinding* MakeGrid(int32 Width, int32 Height, bool bAllowDiagonal = true)
	{
		UPathFinding* Grid = NewObject<UPathFinding>(GetTransientPackage());
		Grid->HorizontalCells = Width;
		Grid->VerticalCells = Height;
		Grid->bAllowDiagonal = bAllowDiagonal;
		Grid->bShowDebugMessages = false;
		Grid->RebuildGrid();
		return Grid;
	}

	inline TArray<FString> Run()
	{
		TArray<FString> Failures;

		const auto Check = [&Failures](bool bOk, const TCHAR* What)
		{
			if (!bOk)
			{
				Failures.Add(What);
			}
		};

		const auto CheckEq = [&Failures](int32 Actual, int32 Expected, const TCHAR* What)
		{
			if (Actual != Expected)
			{
				Failures.Add(FString::Printf(TEXT("%s: expected %d, got %d"), What, Expected, Actual));
			}
		};

		// An open grid: the diagonal is optimal, so 4 moves means 5 cells
		{
			UPathFinding* Grid = MakeGrid(5, 5);
			Check(Grid->SetStartCell({ 0, 0 }), TEXT("Diagonal: start placed"));
			Check(Grid->SetEndCell({ 4, 4 }), TEXT("Diagonal: end placed"));
			Check(Grid->SolveAll() == EPathStepResult::PathFound, TEXT("Diagonal: path found"));

			const TArray<FIntPoint> Path = Grid->GetFinalPath();
			CheckEq(Path.Num(), 5, TEXT("Diagonal: path length"));
			if (Path.Num() > 0)
			{
				Check(Path[0] == FIntPoint(0, 0), TEXT("Diagonal: path starts at the start cell"));
				Check(Path.Last() == FIntPoint(4, 4), TEXT("Diagonal: path ends at the end cell"));
			}
		}

		// Same corners without diagonals: Manhattan, 8 moves means 9 cells
		{
			UPathFinding* Grid = MakeGrid(5, 5, /*bAllowDiagonal*/ false);
			Check(Grid->SetStartCell({ 0, 0 }), TEXT("Straight: start placed"));
			Check(Grid->SetEndCell({ 4, 4 }), TEXT("Straight: end placed"));
			Check(Grid->SolveAll() == EPathStepResult::PathFound, TEXT("Straight: path found"));
			CheckEq(Grid->GetFinalPath().Num(), 9, TEXT("Straight: path length"));
		}

		// Start boxed in by walls
		{
			UPathFinding* Grid = MakeGrid(5, 5);
			Grid->SetWallAt({ 1, 0 }, true);
			Grid->SetWallAt({ 0, 1 }, true);
			Grid->SetWallAt({ 1, 1 }, true);
			Check(Grid->SetStartCell({ 0, 0 }), TEXT("Blocked: start placed"));
			Check(Grid->SetEndCell({ 4, 4 }), TEXT("Blocked: end placed"));
			Check(Grid->SolveAll() == EPathStepResult::NoPath, TEXT("Blocked: reports no path"));
			CheckEq(Grid->GetFinalPath().Num(), 0, TEXT("Blocked: path is empty"));
		}

		// Start already touching the end, so nothing is ever expanded
		{
			UPathFinding* Grid = MakeGrid(5, 5);
			Check(Grid->SetStartCell({ 0, 0 }), TEXT("Adjacent: start placed"));
			Check(Grid->SetEndCell({ 1, 0 }), TEXT("Adjacent: end placed"));
			// The goal is an ordinary node now: step one expands the start and opens the goal,
			// step two selects it. Two steps, and the final step's cost is actually counted.
			Check(Grid->StepOnce() == EPathStepResult::InProgress, TEXT("Adjacent: first step expands"));
			Check(Grid->StepOnce() == EPathStepResult::PathFound, TEXT("Adjacent: second step settles the goal"));

			const TArray<FIntPoint> Path = Grid->GetFinalPath();
			CheckEq(Path.Num(), 2, TEXT("Adjacent: path length"));
			if (Path.Num() > 0)
			{
				Check(Path[0] == FIntPoint(0, 0), TEXT("Adjacent: path starts at the start cell"));
				Check(Path.Last() == FIntPoint(1, 0), TEXT("Adjacent: path ends at the end cell"));
			}
		}

		// Walls cannot sit on the start or end cell, and editing stops once the search runs
		{
			UPathFinding* Grid = MakeGrid(5, 5);
			Grid->SetStartCell({ 0, 0 });
			Grid->SetEndCell({ 4, 4 });

			Grid->SetWallAt({ 0, 0 }, true);
			Check(Grid->GetCellState({ 0, 0 }) == EPathCellState::Path, TEXT("Guard: start did not become a wall"));

			Grid->StepOnce();
			Grid->SetWallAt({ 2, 2 }, true);
			Check(Grid->GetCellState({ 2, 2 }) == EPathCellState::Empty, TEXT("Guard: no walls once started"));
		}

		// Two searches share one grid without touching each other. This is the whole point
		// of keeping per-query state out of the grid, and nothing else here would catch it.
		{
			FPathGrid SharedGrid;
			SharedGrid.Resize(9, 9);
			SharedGrid.SetBlocked({ 4, 3 }, true);
			SharedGrid.SetBlocked({ 4, 4 }, true);
			SharedGrid.SetBlocked({ 4, 5 }, true);

			FGridPathQuery QueryA;
			QueryA.Start = { 0, 0 };
			QueryA.Goal = { 8, 8 };

			FGridPathQuery QueryB;
			QueryB.Start = { 8, 0 };
			QueryB.Goal = { 0, 8 };

			// Interleave them so any shared state would corrupt both
			FGridSearch SearchA;
			FGridSearch SearchB;
			Check(SearchA.Begin(SharedGrid, QueryA), TEXT("Concurrent: A began"));
			Check(SearchB.Begin(SharedGrid, QueryB), TEXT("Concurrent: B began"));

			for (int32 i = 0; i < 500 && !(SearchA.HasEnded() && SearchB.HasEnded()); i++)
			{
				SearchA.Step(SharedGrid);
				SearchB.Step(SharedGrid);
			}

			Check(SearchA.GetStatus() == EPathStepResult::PathFound, TEXT("Concurrent: A found a path"));
			Check(SearchB.GetStatus() == EPathStepResult::PathFound, TEXT("Concurrent: B found a path"));

			// Each interleaved result must match what that query produces on its own
			FGridSearch SoloA;
			SoloA.Begin(SharedGrid, QueryA);
			SoloA.Solve(SharedGrid);
			Check(SoloA.BuildPath(SharedGrid) == SearchA.BuildPath(SharedGrid),
				TEXT("Concurrent: interleaved A matches solo A"));

			FGridSearch SoloB;
			SoloB.Begin(SharedGrid, QueryB);
			SoloB.Solve(SharedGrid);
			Check(SoloB.BuildPath(SharedGrid) == SearchB.BuildPath(SharedGrid),
				TEXT("Concurrent: interleaved B matches solo B"));

			// The grid itself must be untouched by any of that
			Check(SharedGrid.IsBlocked({ 4, 4 }), TEXT("Concurrent: grid walls intact"));
		}

		// Weighted terrain: an expensive shortcut loses to a longer cheap detour
		{
			FPathGrid TileGrid;
			// 0 basic, 1 impassable, 2 mud at five times the cost
			TileGrid.SetTileTable({ FPathTileInfo(1, true, FColor::White),
									FPathTileInfo(1, false, FColor::Black),
									FPathTileInfo(5, true, FColor::Green) });
			TileGrid.Resize(5, 2);
			TileGrid.SetTile({ 1, 0 }, 2);
			TileGrid.SetTile({ 2, 0 }, 2);
			TileGrid.SetTile({ 3, 0 }, 2);

			FGridPathQuery Query;
			Query.Start = { 0, 0 };
			Query.Goal = { 4, 0 };
			Query.bAllowDiagonal = false;

			FGridSearch Search;
			Check(Search.Begin(TileGrid, Query), TEXT("Mud: began"));
			Check(Search.Solve(TileGrid) == EPathStepResult::PathFound, TEXT("Mud: path found"));

			const TArray<FIntPoint> Path = Search.BuildPath(TileGrid);
			// Straight through costs 3 x 50, the detour along the free row costs 5 x 10
			Check(!Path.Contains(FIntPoint(2, 0)), TEXT("Mud: detoured around the expensive tiles"));
			CheckEq(Path.Num(), 7, TEXT("Mud: detour length"));
		}

		// A restricted tile is refused even though its definition says it is passable
		{
			FPathGrid DoorGrid;
			// 0 basic, 1 impassable, 2 door (passable, normal cost)
			DoorGrid.SetTileTable({ FPathTileInfo(1, true, FColor::White),
									FPathTileInfo(1, false, FColor::Black),
									FPathTileInfo(1, true, FColor::Orange) });
			DoorGrid.Resize(5, 3);
			for (int32 x = 0; x < 5; x++)
			{
				DoorGrid.SetTile({ x, 1 }, 1); // wall straight across
			}
			DoorGrid.SetTile({ 2, 1 }, 2); // one door through it

			FGridPathQuery Query;
			Query.Start = { 0, 0 };
			Query.Goal = { 0, 2 };
			Query.bAllowDiagonal = false;

			FGridSearch Through;
			Through.Begin(DoorGrid, Query);
			Check(Through.Solve(DoorGrid) == EPathStepResult::PathFound, TEXT("Door: open door is used"));
			Check(Through.BuildPath(DoorGrid).Contains(FIntPoint(2, 1)), TEXT("Door: path goes through the door"));

			Query.RestrictedTiles = { 2 };
			FGridSearch Refused;
			Refused.Begin(DoorGrid, Query);
			Check(Refused.Solve(DoorGrid) == EPathStepResult::NoPath, TEXT("Door: restricted door blocks the path"));
		}

		// An ignored tile is crossed even though its definition says it is impassable
		{
			FPathGrid SealedGrid;
			SealedGrid.SetTileTable({ FPathTileInfo(1, true, FColor::White),
									  FPathTileInfo(1, false, FColor::Black) });
			SealedGrid.Resize(5, 3);
			for (int32 x = 0; x < 5; x++)
			{
				SealedGrid.SetTile({ x, 1 }, 1); // sealed straight across, no gap
			}

			FGridPathQuery Query;
			Query.Start = { 0, 0 };
			Query.Goal = { 0, 2 };
			Query.bAllowDiagonal = false;

			FGridSearch Sealed;
			Sealed.Begin(SealedGrid, Query);
			Check(Sealed.Solve(SealedGrid) == EPathStepResult::NoPath, TEXT("Ignore: sealed wall blocks by default"));

			Query.IgnoredTiles = { 1 };
			FGridSearch Ghost;
			Ghost.Begin(SealedGrid, Query);
			Check(Ghost.Solve(SealedGrid) == EPathStepResult::PathFound, TEXT("Ignore: ignored wall is crossed"));

			// A restriction beats an ignore when a tile is named by both
			Query.RestrictedTiles = { 1 };
			FGridSearch Both;
			Both.Begin(SealedGrid, Query);
			Check(Both.Solve(SealedGrid) == EPathStepResult::NoPath, TEXT("Ignore: restriction wins over ignore"));
		}

		// A UPathTileSet drives cost and passability through the component. This is the
		// asset -> FPathTileInfo snapshot path, which the core-level checks never touch.
		{
			UPathFinding* Comp = MakeGrid(5, 2, /*bAllowDiagonal*/ false);

			UPathTileSet* TileSet = NewObject<UPathTileSet>(GetTransientPackage());
			const auto AddTile = [TileSet](FName Id, int32 Cost, bool bPassable, FColor Color)
			{
				FPathTileDef Def;
				Def.Id = Id;
				Def.CostMultiplier = Cost;
				Def.bPassable = bPassable;
				Def.Color = Color;
				TileSet->Tiles.Add(Def);
			};
			AddTile(TEXT("Basic"), 1, true, FColor::White);
			AddTile(TEXT("Hole"), 1, false, FColor::Black);
			AddTile(TEXT("Mud"), 3, true, FColor::Orange);

			Comp->TileSet = TileSet;
			Comp->RebuildGrid();

			CheckEq(Comp->GetTileAt({ 0, 0 }), 0, TEXT("TileSet: cells default to tile 0"));
			CheckEq(TileSet->IndexOfTile(TEXT("Mud")), 2, TEXT("TileSet: lookup by id"));

			// The legacy wall API must resolve to the first impassable tile in the set
			Comp->SetWallAt({ 1, 1 }, true);
			CheckEq(Comp->GetTileAt({ 1, 1 }), 1, TEXT("TileSet: wall API places the impassable tile"));
			Check(Comp->GetCellState({ 1, 1 }) == EPathCellState::Wall, TEXT("TileSet: impassable reads as Wall"));
			Comp->SetWallAt({ 1, 1 }, false);

			// Mud across the direct row, so the free row below should win
			Comp->SetTileAt({ 1, 0 }, 2);
			Comp->SetTileAt({ 2, 0 }, 2);
			Comp->SetTileAt({ 3, 0 }, 2);
			Check(Comp->SetStartCell({ 0, 0 }), TEXT("TileSet: start placed"));
			Check(Comp->SetEndCell({ 4, 0 }), TEXT("TileSet: goal placed"));
			Check(Comp->SolveAll() == EPathStepResult::PathFound, TEXT("TileSet: path found"));
			Check(!Comp->GetFinalPath().Contains(FIntPoint(2, 0)), TEXT("TileSet: routed around mud"));

			// Clearing the asset must fall back to the built-in open/impassable table
			Comp->TileSet = nullptr;
			Comp->RebuildGrid();
			CheckEq(Comp->GetTileAt({ 1, 0 }), 0, TEXT("TileSet: cleared, grid back to the fallback table"));
			Comp->SetWallAt({ 1, 0 }, true);
			Check(Comp->GetCellState({ 1, 0 }) == EPathCellState::Wall, TEXT("TileSet: fallback wall still works"));
		}

		// Jump Point Search must agree with A* on cost for every grid it is valid on.
		// Random grids catch pruning bugs that hand-picked cases walk straight past.
		{
			FRandomStream Rng(1337);
			int32 Solvable = 0;

			for (int32 Trial = 0; Trial < 40; Trial++)
			{
				FPathGrid G;
				G.Resize(12, 12);
				for (int32 i = 0; i < G.Num(); i++)
				{
					if (Rng.FRand() < 0.25f)
					{
						G.SetTileAtIndex(i, 1);
					}
				}
				G.SetTile({ 0, 0 }, 0);
				G.SetTile({ 11, 11 }, 0);

				FGridPathQuery Q;
				Q.Start = { 0, 0 };
				Q.Goal = { 11, 11 };
				Q.bAllowDiagonal = true;

				Q.Algorithm = EPathAlgorithm::AStar;
				FGridSearch Star;
				Star.Begin(G, Q);
				Star.Solve(G, 100000);

				Q.Algorithm = EPathAlgorithm::JumpPointSearch;
				FGridSearch Jps;
				Jps.Begin(G, Q);
				Jps.Solve(G, 100000);

				Check(!Jps.DidAlgorithmFallBack(), TEXT("JPS: ran rather than falling back"));
				Check(Star.GetStatus() == Jps.GetStatus(), TEXT("JPS: agrees with A* on reachability"));

				if (Star.GetStatus() == EPathStepResult::PathFound
					&& Jps.GetStatus() == EPathStepResult::PathFound)
				{
					Solvable++;
					CheckEq(Jps.GetPathCost(G), Star.GetPathCost(G), TEXT("JPS: same optimal cost as A*"));

					// Interpolation between jump points must leave a walkable, contiguous path
					const TArray<FIntPoint> Path = Jps.BuildPath(G);
					bool bContiguous = Path.Num() > 0 && Path[0] == FIntPoint(0, 0)
						&& Path.Last() == FIntPoint(11, 11);
					for (int32 i = 1; i < Path.Num() && bContiguous; i++)
					{
						const FIntPoint Delta = Path[i] - Path[i - 1];
						bContiguous = FMath::Abs(Delta.X) <= 1 && FMath::Abs(Delta.Y) <= 1
							&& (Delta.X != 0 || Delta.Y != 0)
							&& !G.IsBlocked(Path[i]);
					}
					Check(bContiguous, TEXT("JPS: path is contiguous and walkable"));
				}
			}

			// Guard against the comparison passing because nothing was ever solvable
			Check(Solvable > 5, TEXT("JPS: enough solvable trials to be meaningful"));
		}

		// JPS is not valid on a weighted grid, nor without diagonals, and must say so
		{
			FPathGrid Weighted;
			Weighted.SetTileTable({ FPathTileInfo(1, true, FColor::White),
									FPathTileInfo(1, false, FColor::Black),
									FPathTileInfo(3, true, FColor::Orange) });
			Weighted.Resize(6, 6);

			FGridPathQuery Q;
			Q.Start = { 0, 0 };
			Q.Goal = { 5, 5 };
			Q.Algorithm = EPathAlgorithm::JumpPointSearch;

			FGridSearch OnWeighted;
			OnWeighted.Begin(Weighted, Q);
			Check(OnWeighted.DidAlgorithmFallBack(), TEXT("JPS: falls back on a weighted grid"));
			Check(OnWeighted.GetAlgorithm() == EPathAlgorithm::AStar, TEXT("JPS: fell back to A*"));

			FPathGrid Uniform;
			Uniform.Resize(6, 6);
			Q.bAllowDiagonal = false;
			FGridSearch NoDiagonal;
			NoDiagonal.Begin(Uniform, Q);
			Check(NoDiagonal.DidAlgorithmFallBack(), TEXT("JPS: falls back without diagonals"));

			Q.bAllowDiagonal = true;
			FGridSearch Valid;
			Valid.Begin(Uniform, Q);
			Check(!Valid.DidAlgorithmFallBack(), TEXT("JPS: runs on a uniform 8-connected grid"));
		}

		// Round trip through world space
		{
			UPathFinding* Grid = MakeGrid(5, 5);
			Check(Grid->WorldToGrid(Grid->GridToWorld({ 3, 2 })) == FIntPoint(3, 2), TEXT("Coords: world round trip"));
			Check(!Grid->IsValidCoord({ 5, 0 }), TEXT("Coords: off-grid rejected"));
		}

		return Failures;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
