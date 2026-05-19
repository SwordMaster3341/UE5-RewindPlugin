// S-G-D

#include "ChronogySubsystem.h"
#include "ChronogyComponent.h"
#include "Kismet/GameplayStatics.h"

void UChronogySubsystem::RegisterComponent(UChronogyComponent* Component)
{
    if (Component)
    {
        RegisteredComponents.AddUnique(Component);
    }
}

void UChronogySubsystem::UnregisterComponent(UChronogyComponent* Component)
{
    RegisteredComponents.RemoveSwap(Component);
}

void UChronogySubsystem::StartGlobalRewind()
{
    if (bIsRewinding) return;

    bIsRewinding = true;
    OnRewindStarted.Broadcast();
}

void UChronogySubsystem::StopGlobalRewind()
{
    if (!bIsRewinding) return;

    bIsRewinding = false;
    OnRewindCompleted.Broadcast();
}

void UChronogySubsystem::SetTimeDilation(float Dilation)
{
    CurrentTimeDilation = Dilation;
    UGameplayStatics::SetGlobalTimeDilation(GetGameInstance()->GetWorld(), Dilation);
}

void UChronogySubsystem::ResetTimeDilation()
{
    CurrentTimeDilation = 1.0f;
    UGameplayStatics::SetGlobalTimeDilation(GetGameInstance()->GetWorld(), 1.0f);
}
