#include "PathFindingChecks.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathFindingTest, "TP1.PathFinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPathFindingTest::RunTest(const FString& Parameters)
{
	for (const FString& Failure : PathFindingChecks::Run())
	{
		AddError(Failure);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
