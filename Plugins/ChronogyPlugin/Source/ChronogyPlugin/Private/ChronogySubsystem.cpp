// Blake de Armas

#include "ChronogySubsystem.h"
#include "ChronogyComponent.h"
#include "ChronogyLogs.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraCommon.h"

void UChronogySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    SpawnDelegateHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UChronogySubsystem::OnActorSpawned));
}

bool UChronogySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

float UChronogySubsystem::GetTimelineSeconds() const
{
    return GetWorld()->GetRealTimeSeconds() - TimelineOffset;
}

void UChronogySubsystem::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->RemoveOnActorSpawnedHandler(SpawnDelegateHandle);
    }
    Super::Deinitialize();
}

void UChronogySubsystem::RegisterComponent(UChronogyComponent* Component)
{
    if (Component)
    {
        RegisteredComponents.AddUnique(Component);
        UE_LOG(LogChronogy, Verbose, TEXT("ChronogySubsystem: registered '%s' (%d total)"), *Component->GetOwner()->GetName(), RegisteredComponents.Num());
    }
}

void UChronogySubsystem::OnActorSpawned(AActor* Actor)
{
    if (!Actor || !GetWorld()->HasBegunPlay()) return;

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
    CurrentRewindTimestamp = GetTimelineSeconds();
    LastRealTimeSeconds    = GetWorld()->GetRealTimeSeconds();
    UE_LOG(LogChronogy, Log, TEXT("ChronogySubsystem: global rewind started (speed=%.2f, components=%d, tracked actors=%d)"), GlobalRewindSpeed, RegisteredComponents.Num(), SpawnedActorRecords.Num());

    OnRewindStarted.Broadcast();
}

void UChronogySubsystem::StopGlobalRewind()
{
    if (!bIsRewinding) return;

    bIsRewinding = false;
    TimelineOffset = GetWorld()->GetRealTimeSeconds() - CurrentRewindTimestamp;
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

    const float Now = GetTimelineSeconds();
    for (int32 i = FXTracks.Num() - 1; i >= 0; --i)
    {
        FChronogyParticleTrack& Track = FXTracks[i];
        UNiagaraComponent* C = Track.Component.Get();
        if (!C)
        {
            FXTracks.RemoveAt(i, 1, EAllowShrinking::No);
            continue;
        }

        if (Track.BirthTime < 0.f || CurrentRewindTimestamp < Track.BirthTime)
        {
            // Rewound to before it was spawned it no longer exists.
            C->DestroyComponent();
            FXTracks.RemoveAt(i, 1, EAllowShrinking::No);
            continue;
        }

        RestoreTrack(Track, CurrentRewindTimestamp, Now);
    }

    OnRewindCompleted.Broadcast();
}

void UChronogySubsystem::TrackSpawnedActor(AActor* Actor)
{
    if (!Actor) return;
    FChronogySpawnRecord& Record = SpawnedActorRecords.AddDefaulted_GetRef();
    Record.SpawnedActor    = Actor;
    Record.SpawnTimestamp  = GetTimelineSeconds();
    UE_LOG(LogChronogy, Verbose, TEXT("ChronogySubsystem: tracking '%s' at T=%.3f"), *Actor->GetName(), Record.SpawnTimestamp);
}

void UChronogySubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    const float RealNow   = GetWorld()->GetRealTimeSeconds();
    const float RealDelta = RealNow - LastRealTimeSeconds;
    LastRealTimeSeconds   = RealNow;

    if (bIsRewinding)
    {
        CurrentRewindTimestamp -= RealDelta * GlobalRewindSpeed;

        float EarliestRecorded = TNumericLimits<float>::Max();
        for (const TWeakObjectPtr<UChronogyComponent>& Component : RegisteredComponents)
        {
            if (Component.IsValid())
            {
                const TOptional<float> Oldest = Component->GetOldestSnapshotTime();
                if (Oldest.IsSet())
                {
                    EarliestRecorded = FMath::Min(EarliestRecorded, Oldest.GetValue());
                }
            }
        }
        if (EarliestRecorded != TNumericLimits<float>::Max())
        {
            CurrentRewindTimestamp = FMath::Max(CurrentRewindTimestamp, EarliestRecorded);
        }

        for (const FChronogySpawnRecord& Record : SpawnedActorRecords)
        {
            if (Record.SpawnedActor.IsValid())
            {
                Record.SpawnedActor->SetActorHiddenInGame(Record.SpawnTimestamp > CurrentRewindTimestamp);
            }
        }

        // CurrentRewindTimestamp is the absolute rewind clock; each FX system's age is (clock - BirthTime).
        for (int32 i = FXTracks.Num() - 1; i >= 0; --i)
        {
            if (FXTracks[i].Component.IsValid())
                ApplyTrackAgeAtTime(FXTracks[i], CurrentRewindTimestamp);
            else
                FXTracks.RemoveAt(i, 1, EAllowShrinking::No);
        }
    }
    else
    {
        // Drive detached FX age forward during normal play, mirroring the owner-attached path. Keeping
        // them age-driven the whole time (instead of switching modes at rewind boundaries) is what lets
        // them resume cleanly after a rewind without vanishing.
        const float TimelineNow = RealNow - TimelineOffset;
        for (int32 i = FXTracks.Num() - 1; i >= 0; --i)
        {
            if (FXTracks[i].Component.IsValid())
                PollTrackActivation(FXTracks[i], TimelineNow);
            else
                FXTracks.RemoveAt(i, 1, EAllowShrinking::No);
        }
    }
}

TStatId UChronogySubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UChronogySubsystem, STATGROUP_Tickables);
}

void UChronogySubsystem::SetTimeDilation(float Dilation)
{
    CurrentTimeDilation = Dilation;
    UGameplayStatics::SetGlobalTimeDilation(GetWorld(), Dilation);
}

void UChronogySubsystem::ResetTimeDilation()
{
    CurrentTimeDilation = 1.0f;
    UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.0f);
}

void UChronogySubsystem::RegisterRewindableFX(UNiagaraComponent* FXComponent, EChronogyParticleRewindMode Mode)
{
    if (!FXComponent) return;

    FChronogyParticleTrack& Track = FXTracks.AddDefaulted_GetRef();
    Track.Component  = FXComponent;
    Track.Mode       = Mode;
    Track.bWasActive = FXComponent->IsActive();
    Track.BirthTime  = Track.bWasActive ? GetTimelineSeconds() : -1.f;
    Track.DeathAge   = -1.f;

    // A one-shot that auto-destroys on completion would be gone before we could reverse it.
    FXComponent->SetAutoDestroy(false);

    // Put it into solo + DesiredAge now and leave it there; OnForwardTick / OnRewindTick drive the age.
    ConfigureTrackForRewind(Track, 0.0166f);

    UE_LOG(LogChronogy, Verbose, TEXT("ChronogySubsystem: registered detached FX '%s' (%d tracked)"), *FXComponent->GetName(), FXTracks.Num());
}

void UChronogySubsystem::ConfigureTrackForRewind(FChronogyParticleTrack& Track, float SeekDelta)
{
    UNiagaraComponent* C = Track.Component.Get();
    if (!C) return;

    if (Track.Mode != EChronogyParticleRewindMode::Scrub || Track.bScrubReady) return;

    C->SetForceSolo(true);
    C->SetSeekDelta(SeekDelta);
    C->SetCanRenderWhileSeeking(true);
    C->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
    Track.bScrubReady = true;
}

void UChronogySubsystem::PollTrackActivation(FChronogyParticleTrack& Track, float Now)
{
    UNiagaraComponent* C = Track.Component.Get();
    if (!C) return;

    const bool bActive = C->IsActive();

    // Birth (or re-birth after a previous death): a system we are not already tracking as alive
    // activated. Anchor its age clock to this absolute time.
    if (bActive && !Track.bWasActive && (Track.BirthTime < 0.f || Track.DeathAge >= 0.f))
    {
        Track.BirthTime = Now;
        Track.DeathAge  = -1.f;
    }

    // While born and not yet finished, drive its age forward from the clock (appearance = f(clock)).
    if (Track.BirthTime >= 0.f && Track.DeathAge < 0.f)
    {
        const float Age = Now - Track.BirthTime;

        if (!bActive && Track.bWasActive)
        {
            Track.DeathAge = Age;
        }
        else if (Track.Mode == EChronogyParticleRewindMode::Scrub && Track.bScrubReady)
        {
            if (!bActive) C->Activate(false);
            C->SetDesiredAge(Age);
        }
    }

    Track.bWasActive = C->IsActive();
}

void UChronogySubsystem::ApplyTrackAgeAtTime(FChronogyParticleTrack& Track, float RewindClock)
{
    UNiagaraComponent* C = Track.Component.Get();
    if (!C || Track.BirthTime < 0.f) return;   // never born — nothing to reverse

    const float Age = RewindClock - Track.BirthTime;

    const bool bVisible = Age >= 0.f && (Track.DeathAge < 0.f || Age <= Track.DeathAge);
    if (!bVisible)
    {
        if (C->IsActive()) C->Deactivate();
        return;
    }

    switch (Track.Mode)
    {
    case EChronogyParticleRewindMode::Scrub:
        if (!C->IsActive()) C->Activate(false);
        C->SetDesiredAge(Age);
        break;
    case EChronogyParticleRewindMode::Freeze:
        if (!C->IsActive()) C->Activate(false);
        C->SetPaused(true);
        break;
    case EChronogyParticleRewindMode::FollowTransform:
        if (!C->IsActive()) C->Activate(false);
        // Left ticking — it trails the owner as the owner's transform is rewound.
        break;
    }
}

void UChronogySubsystem::RestoreTrack(FChronogyParticleTrack& Track, float StopClock, float Now)
{
    UNiagaraComponent* C = Track.Component.Get();
    if (!C) return;

    if (Track.Mode == EChronogyParticleRewindMode::Freeze)
    {
        C->SetPaused(false);
    }
    //Stopped at its birth or greater
    if (Track.BirthTime >= 0.f)
    {
        const float AgeAtStop = StopClock - Track.BirthTime;
        if (AgeAtStop < 0.f)
        {
            Track.BirthTime  = -1.f;
            Track.DeathAge   = -1.f;
            if (C->IsActive()) C->Deactivate();
            Track.bWasActive = false;
            return;
        }
        if (Track.DeathAge < 0.f || AgeAtStop <= Track.DeathAge)
        {
            Track.BirthTime  = Now - AgeAtStop;
            Track.DeathAge   = -1.f;
            Track.bWasActive = false;
            return;
        }
        // else: stopped after it had already finished — leave it finished/empty.
    }

    Track.bWasActive = C->IsActive();
}
