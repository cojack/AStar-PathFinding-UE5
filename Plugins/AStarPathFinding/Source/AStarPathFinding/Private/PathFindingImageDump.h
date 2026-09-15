#pragma once

#include "CoreMinimal.h"
#include "GridPathCore.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ImageUtils.h"
#include "Misc/Paths.h"

/**
 * Renders a finished search to a PNG so a headless run leaves something you can look at.
 * Pure CPU encoding, so it works under -nullrhi with no GPU involved.
 */
namespace PathFindingImageDump
{
	/** Where the images land. Under Saved/, which is gitignored. */
	inline FString OutputDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("PathFindingTests");
	}

	inline FColor CellColour(const FPathGrid& Grid, const FGridSearch& Search,
		int32 Index, FIntPoint Start, FIntPoint Goal)
	{
		const FIntPoint Coord = Grid.IndexToCoord(Index);

		if (Coord == Start)
		{
			return FColor(0, 200, 255);   // start, cyan
		}
		if (Coord == Goal)
		{
			return FColor(255, 0, 255);   // goal, magenta
		}
		if (Grid.IsBlockedIndex(Index))
		{
			return FColor(25, 25, 25);    // wall
		}

		switch (Search.GetMembership(Index))
		{
		case EPathCellState::Open:   return FColor(60, 180, 75);    // queued
		case EPathCellState::Closed: return FColor(200, 50, 50);    // expanded
		case EPathCellState::Path:   return FColor(40, 90, 230);    // final path
		default:                     return Grid.TileInfoAt(Index).Color;
		}
	}

	/** Writes <OutputDir>/<Name>.png. Returns the path, or empty on failure. */
	inline FString Save(const FPathGrid& Grid, const FGridSearch& Search,
		FIntPoint Start, FIntPoint Goal, const FString& Name, int32 CellPixels = 24)
	{
		if (Grid.Width <= 0 || Grid.Height <= 0)
		{
			return FString();
		}

		const int32 Width = Grid.Width * CellPixels;
		const int32 Height = Grid.Height * CellPixels;

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);

		for (int32 Py = 0; Py < Height; Py++)
		{
			for (int32 Px = 0; Px < Width; Px++)
			{
				const int32 Cx = Px / CellPixels;
				const int32 Cy = Py / CellPixels;

				// One-pixel gutter so cell boundaries stay readable
				const bool bEdge = (Px % CellPixels == 0) || (Py % CellPixels == 0);

				const int32 Index = Grid.CoordToIndex({ Cx, Cy });
				const FColor Colour = bEdge || Index == INDEX_NONE
					? FColor(90, 90, 90)
					: CellColour(Grid, Search, Index, Start, Goal);

				Pixels[Py * Width + Px] = FColor(Colour.R, Colour.G, Colour.B, 255);
			}
		}

		const FString Path = OutputDir() / (Name + TEXT(".png"));
		const FImageView Image(Pixels.GetData(), Width, Height);

		return FImageUtils::SaveImageByExtension(*Path, Image) ? Path : FString();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
