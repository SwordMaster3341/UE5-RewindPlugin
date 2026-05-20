// S-G-D

#include "ChronogySubsystem.h"
#include "ChronogyComponent.h"
#include "ChronogyLogs.h"
#include "Kismet/GameplayStatics.h"

void UChronogySubsystem::RegisterComponent(UChronogyComponent* Component)
{
    if (Component)
    {
        RegisteredComponents.AddUnique(Component);
        UE_LOG(LogChronogy, Verbose, TEXT("ChronogySubsystem: registered '%s' (%d total)"), *Component->GetOwner()->GetName(), RegisteredComponents.Num());
    }
}

void UChronogySubsystem::UnregisterComponent(UChronogyComponent* Component)
{
    RegisteredComponents.RemoveSwap(Component);
    UE_LOG(LogChronogy, Verbose, TEXT("ChronogySubsystem: unregistered component (%d remaining)"), RegisteredComponents.Num());
}

void UChronogySubsystem::StartGlobalRewind()
{
    if (bIsRewinding) return;

    bIsRewinding = true;
    UE_LOG(LogChronogy, Log, TEXT("ChronogySubsystem: global rewind started (speed=%.2f, components=%d)"), GlobalRewindSpeed, RegisteredComponents.Num());
    OnRewindStarted.Broadcast();
}

void UChronogySubsystem::StopGlobalRewind()
{
    if (!bIsRewinding) return;

    bIsRewinding = false;
    UE_LOG(LogChronogy, Log, TEXT("ChronogySubsystem: global rewind stopped"));
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
