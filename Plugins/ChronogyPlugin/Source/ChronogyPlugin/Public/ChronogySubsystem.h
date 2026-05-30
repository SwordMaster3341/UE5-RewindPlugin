// S-G-D

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChronogyParticle.h"
#include "ChronogySubsystem.generated.h"

class UChronogyComponent;
class UNiagaraComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGlobalRewindStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGlobalRewindCompleted);

USTRUCT()
struct FChronogySpawnRecord
{
	GENERATED_BODY()

	TWeakObjectPtr<AActor> SpawnedActor;
	float                  SpawnTimestamp = 0.0f;
};



UCLASS()
class CHRONOGYPLUGIN_API UChronogySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	//Component Registration
	void RegisterComponent(UChronogyComponent* Component);
	void UnregisterComponent(UChronogyComponent* Component);

	void TrackSpawnedActor(AActor* Actor);

	// Per-frame pumps for the detached FX registry, driven by UChronogyComponent so we reuse the
	// existing component->subsystem tick path (both are frame-deduped). Forward advances FX age,
	// rewind reverses it.
	void OnRewindTick(float Timestamp);
	void OnForwardTick(float DeltaSeconds);

	// Register a detached one-shot Niagara system (e.g. from SpawnSystemAtLocation) so it
	// reverses with the global rewind. Must still be alive when rewind starts.
	UFUNCTION(BlueprintCallable, Category = "Chronogy")
	void RegisterRewindableFX(UNiagaraComponent* FXComponent, EChronogyParticleRewindMode Mode = EChronogyParticleRewindMode::Scrub);

	// Shared per-Niagara-system rewind operations, used by both UChronogyComponent
	// (owner-attached effects) and this subsystem (detached one-shot FX). All Niagara API lives
	// here so the two paths share one implementation. The continuous-age model: a Scrub system is
	// put into solo + DesiredAge ONCE (ConfigureTrackForRewind) and stays there for its whole
	// life; its age is then driven up every forward frame (PollTrackActivation) and down every
	// rewind frame (ApplyTrackAgeAtTime). The mode never switches, so the system never vanishes
	// or freezes on rewind release.
	static void ConfigureTrackForRewind(FChronogyParticleTrack& Track, float SeekDelta);
	static void PollTrackActivation(FChronogyParticleTrack& Track, float Now);
	static void ApplyTrackAgeAtTime(FChronogyParticleTrack& Track, float RewindClock);
	// StopClock = the rewind clock where we stopped; Now = current real time. Re-anchors BirthTime
	// so forward play resumes from the age the rewind landed on (not now - original birth).
	static void RestoreTrack(FChronogyParticleTrack& Track, float StopClock, float Now);


	//Broadcasting events to registered components

	UFUNCTION(BlueprintCallable, Category = "Chronogy")
	void StartGlobalRewind();

	UFUNCTION(BlueprintCallable, Category = "Chronogy")
	void StopGlobalRewind();

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chronogy")
	float GlobalRewindSpeed = 1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Chronogy")
	float CurrentTimeDilation = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "Chronogy")
	void SetTimeDilation(float Dilation);

	UFUNCTION(BlueprintCallable, Category = "Chronogy")
	void ResetTimeDilation();

	UPROPERTY(BlueprintReadOnly, Category = "Chronogy")
	bool bIsRewinding = false;

	float GetCurrentRewindTimestamp() const { return CurrentRewindTimestamp; }

	FOnGlobalRewindStarted OnRewindStarted;
	FOnGlobalRewindCompleted OnRewindCompleted;

private:
	UFUNCTION()
	void OnActorSpawned(AActor* Actor);

	TArray<TWeakObjectPtr<UChronogyComponent>> RegisteredComponents;
	TArray<FChronogySpawnRecord>               SpawnedActorRecords;
	FDelegateHandle                            SpawnDelegateHandle;

	UPROPERTY()
	TArray<FChronogyParticleTrack>             FXTracks;

	float   CurrentRewindTimestamp = 0.0f;
	uint64  LastRewindTickFrame    = 0;
	uint64  LastForwardTickFrame   = 0;
	


	
	
};
