// S-G-D

#include "ChronogySubsystem.h"
#include "ChronogyComponent.h"
#include "ChronogyLogs.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraCommon.h"

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

    // Detached FX need no mode change here: each was put into solo + DesiredAge when it was
    // registered and stays there. OnRewindTick simply starts driving its age downward.

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

    // Detached FX: destroy any that were rewound back to before they were spawned; let the rest
    // resume forward play. RestoreTrack re-anchors their birth time to Now so they continue from
    // the age the rewind stopped on; OnForwardTick then drives them forward again.
    const float Now = GetGameInstance()->GetWorld()->GetRealTimeSeconds();
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
            // Rewound to before it was spawned — it no longer exists.
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

    // Timestamp is the absolute rewind clock; each FX system's age is (clock - BirthTime).
    for (int32 i = FXTracks.Num() - 1; i >= 0; --i)
    {
        if (FXTracks[i].Component.IsValid())
            ApplyTrackAgeAtTime(FXTracks[i], Timestamp);
        else
            FXTracks.RemoveAt(i, 1, EAllowShrinking::No);
    }
}

void UChronogySubsystem::OnForwardTick(float RealNow)
{
    if (GFrameCounter == LastForwardTickFrame) return;
    LastForwardTickFrame = GFrameCounter;

    // Drive detached FX age forward during normal play, mirroring the owner-attached path. Keeping
    // them age-driven the whole time (instead of switching modes at rewind boundaries) is what lets
    // them resume cleanly after a rewind without vanishing.
    for (int32 i = FXTracks.Num() - 1; i >= 0; --i)
    {
        if (FXTracks[i].Component.IsValid())
            PollTrackActivation(FXTracks[i], RealNow);
        else
            FXTracks.RemoveAt(i, 1, EAllowShrinking::No);
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

void UChronogySubsystem::RegisterRewindableFX(UNiagaraComponent* FXComponent, EChronogyParticleRewindMode Mode)
{
    if (!FXComponent) return;

    FChronogyParticleTrack& Track = FXTracks.AddDefaulted_GetRef();
    Track.Component  = FXComponent;
    Track.Mode       = Mode;
    Track.bWasActive = FXComponent->IsActive();
    Track.BirthTime  = Track.bWasActive ? GetGameInstance()->GetWorld()->GetRealTimeSeconds() : -1.f;
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

    // Only Scrub is age-driven. Freeze plays normally and is just paused during rewind;
    // FollowTransform trails the rewinding owner. Configure Scrub ONCE and never switch back —
    // the system lives its whole life in solo + DesiredAge so entering/leaving rewind is a no-op.
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
            // It was playing and went inactive => a one-shot finished. Record its lifetime so rewind
            // hides it past this age and bounds the backward scrub to [0, DeathAge]. (A system merely
            // scrubbed to its spawn point also reads inactive, but bWasActive is cleared there, so it
            // is revived below instead of being killed.)
            Track.DeathAge = Age;
        }
        else if (Track.Mode == EChronogyParticleRewindMode::Scrub && Track.bScrubReady)
        {
            // Keep it playing forward, reviving a system that idled inactive at its spawn point
            // (e.g. right after a rewind landed on the birth frame) rather than leaving it frozen.
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

    // Visible only within its real lifetime: at/after birth, and (if it finished) not past death.
    // Outside that window it does not exist at this point on the timeline, so hide it. This is the
    // only despawn gating the particle path does, and it bounds the backward scrub to [0, DeathAge].
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

    // Scrub stays in solo + DesiredAge for its whole life. Switching the age-update mode back here
    // is exactly what de-registered the system from the batched sim and made it vanish, so we do
    // NOT touch it — forward play resumes by driving DesiredAge upward again (PollTrackActivation).
    if (Track.Mode == EChronogyParticleRewindMode::Freeze)
    {
        C->SetPaused(false);
    }

    // Re-anchor BirthTime to real time so forward play continues from exactly the age the rewind
    // stopped on. Real time keeps advancing during the wait + rewind, so without this a one-shot
    // stopped mid-life would resume showing itself already finished ((now - original birth) is huge).
    if (Track.BirthTime >= 0.f)
    {
        const float AgeAtStop = StopClock - Track.BirthTime;
        if (AgeAtStop < 0.f)
        {
            // Stopped before its birth — it does not exist in the resumed timeline. Reset to unborn
            // so gameplay can spawn it fresh, and hide it now.
            Track.BirthTime  = -1.f;
            Track.DeathAge   = -1.f;
            if (C->IsActive()) C->Deactivate();
            Track.bWasActive = false;
            return;
        }
        if (Track.DeathAge < 0.f || AgeAtStop <= Track.DeathAge)
        {
            // Stopped within its life — re-anchor so forward play continues from this exact age, and
            // clear bWasActive so the next forward tick revives + drives it (rather than reading the
            // just-scrubbed, possibly-inactive system as a death and freezing it at the spawn point).
            Track.BirthTime  = Now - AgeAtStop;
            Track.DeathAge   = -1.f;
            Track.bWasActive = false;
            return;
        }
        // else: stopped after it had already finished — leave it finished/empty.
    }

    Track.bWasActive = C->IsActive();
}
