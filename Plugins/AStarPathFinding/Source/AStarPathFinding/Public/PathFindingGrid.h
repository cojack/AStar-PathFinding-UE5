#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PathFindingGrid.generated.h"

class UPathFinding;

/**
 * Drop-in actor that owns a UPathFinding component, so a grid can be placed in a level
 * and subclassed in the Blueprint Editor without hand-building an actor first.
 */
UCLASS(Blueprintable, BlueprintType)
class ASTARPATHFINDING_API APathFindingGrid : public AActor
{
	GENERATED_BODY()

public:
	APathFindingGrid();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PathFinding")
	TObjectPtr<UPathFinding> PathFinding;
};
