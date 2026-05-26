// S-G-D

#include "ChronogySubsystem.h"
#include "ChronogyComponent.h"
#include "ChronogyLogs.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"

void UChronogySubsystem::Deinitialize()
{
    if (SpawnDelegateHandle.IsValid())
    {
        if (UWorld* World = GetGameInstance()->GetWorld())
        {
            World->RemoveOnActorSpawnedHandler(SpawnDelegateHandle);
        }
    }
    Super::Deinitialize();
}

void UChronogySubsystem::RegisterComponent(UChronogyComponent* Component)
{
    if (Component)
    {
        RegisteredComponents.AddUnique(Component);
        UE_LOG(LogChronogy, Verbose, TEXT("ChronogySubsystem: registered '%s' (%d total)"), *Component->GetOwner()->GetName(), RegisteredComponents.Num());

        if (!SpawnDelegateHandle.IsValid())
        {
            if (UWorld* World = GetGameInstance()->GetWorld())
            {
                SpawnDelegateHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UChronogySubsystem::OnActorSpawned));
            }
        }
    }
}

void UChronogySubsystem::OnActorSpawned(AActor* Actor)
{
    if (!Actor || !GetGameInstance()->GetWorld()->HasBegunPlay()) return;

    UChronogyComponent* CC = Actor->FindComponentByClass<UChronogyComponent>();
    if (CC && CC->bShouldTrackSpawn)
    {
        TrackSpawnedActor(Actor);
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
    CurrentRewindTimestamp = GetGameInstance()->GetWorld()->GetRealTimeSeconds();
    UE_LOG(LogChronogy, Log, TEXT("ChronogySubsystem: global rewind started (speed=%.2f, components=%d, tracked actors=%d)"), GlobalRewindSpeed, RegisteredComponents.Num(), SpawnedActorRecords.Num());
    OnRewindStarted.Broadcast();
}

void UChronogySubsystem::StopGlobalRewind()
{
    if (!bIsRewinding) return;

    bIsRewinding = false;
    UE_LOG(LogChronogy, Log, TEXT("ChronogySubsystem: global rewind stopped at T=%.3f"), CurrentRewindTimestamp);

    for (int32 i = SpawnedActorRecords.Num() - 1; i >= 0; --i)
    {
        FChronogySpawnRecord& Record = SpawnedActorRecords[i];
        if (!Record.SpawnedActor.IsValid())
        {
            SpawnedActorRecords.RemoveAt(i, 1, EAllowShrinking::No);
            continue;
        }
        if (Record.SpawnTimestamp > CurrentRewindTimestamp)
        {
            UE_LOG(LogChronogy, Verbose, TEXT("ChronogySubsystem: destroying rewound actor '%s' (spawned T=%.3f, stop T=%.3f)"), *Record.SpawnedActor->GetName(), Record.SpawnTimestamp, CurrentRewindTimestamp);
            Record.SpawnedActor->Destroy();
            SpawnedActorRecords.RemoveAt(i, 1, EAllowShrinking::No);
        }
        else
        {
            Record.SpawnedActor->SetActorHiddenInGame(false);
        }
    }

    OnRewindCompleted.Broadcast();
}

void UChronogySubsystem::TrackSpawnedActor(AActor* Actor)
{
    if (!Actor) return;
    FChronogySpawnRecord& Record = SpawnedActorRecords.AddDefaulted_GetRef();
    Record.SpawnedActor    = Actor;
    Record.SpawnTimestamp  = GetGameInstance()->GetWorld()->GetRealTimeSeconds();
    UE_LOG(LogChronogy, Verbose, TEXT("ChronogySubsystem: tracking '%s' at T=%.3f"), *Actor->GetName(), Record.SpawnTimestamp);
}

void UChronogySubsystem::OnRewindTick(float Timestamp)
{
    if (GFrameCounter == LastRewindTickFrame) return;
    LastRewindTickFrame    = GFrameCounter;
    CurrentRewindTimestamp = Timestamp;

    for (const FChronogySpawnRecord& Record : SpawnedActorRecords)
    {
        if (Record.SpawnedActor.IsValid())
        {
            Record.SpawnedActor->SetActorHiddenInGame(Record.SpawnTimestamp > Timestamp);
        }
    }
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
