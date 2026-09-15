#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PathTileSet.generated.h"

/**
 * One tile type. The plugin ships no vocabulary of its own - a project defines whatever
 * tiles it needs (grass, mud, door, water) in a UPathTileSet. See docs/ADR.md ADR-0013.
 */
USTRUCT(BlueprintType)
struct FPathTileDef
{
	GENERATED_BODY()

	/** Name this tile is looked up by. Only used for lookup and display. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	FName Id = NAME_None;

	/**
	 * Multiplies the cost of moving into this tile. 1 is normal, 3 is three times as
	 * expensive. Must stay >= 1: the heuristic assumes no tile is cheaper than the base
	 * move cost, and a multiplier below 1 would make it overestimate and lose optimality.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile", meta = (ClampMin = 1, UIMin = 1))
	int32 CostMultiplier = 1;

	/** Whether this tile can be entered at all. A query may still override this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	bool bPassable = true;

	/** Colour used by the debug visualiser for an untouched cell of this type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	FColor Color = FColor::White;
};

/**
 * The tile vocabulary for one grid. Cells store an index into Tiles, so the order here is
 * meaningful and stable: inserting in the middle renumbers every cell that follows.
 */
UCLASS(BlueprintType)
class ASTARPATHFINDING_API UPathTileSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tiles")
	TArray<FPathTileDef> Tiles;

	/** Index of the tile with this id, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "Tiles")
	int32 IndexOfTile(FName Id) const;
};
