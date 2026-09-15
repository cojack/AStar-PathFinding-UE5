#include "PathFindingGrid.h"
#include "PathFinding.h"

APathFindingGrid::APathFindingGrid()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	PathFinding = CreateDefaultSubobject<UPathFinding>(TEXT("PathFinding"));
}
