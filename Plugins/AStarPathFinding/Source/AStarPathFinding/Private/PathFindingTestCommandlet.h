#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PathFindingTestCommandlet.generated.h"

/**
 * Headless entry point for the UPathFinding behaviour checks, because the automation
 * harness does not complete headlessly in every environment:
 *
 *   UnrealEditor-Cmd <project>.uproject -run=PathFindingTest
 *
 * Returns the number of failures, so a non-zero exit code means something broke.
 */
UCLASS()
class UPathFindingTestCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
