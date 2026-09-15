#include "PathFindingTestCommandlet.h"
#include "PathFindingChecks.h"

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

	return Failures.Num();
#else
	UE_LOG(LogPathFindingTest, Error, TEXT("Built without WITH_DEV_AUTOMATION_TESTS, nothing to run."));
	return 1;
#endif
}
