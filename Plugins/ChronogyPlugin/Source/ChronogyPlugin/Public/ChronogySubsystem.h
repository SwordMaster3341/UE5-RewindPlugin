// Blake de Armas

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
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
class CHRONOGYPLUGIN_API UChronogySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	//Component Registration
	void RegisterComponent(UChronogyComponent* Component);
	void UnregisterComponent(UChronogyComponent* Component);

	void TrackSpawnedActor(AActor* Actor);

	// Register a detached one-shot Niagara system (e.g. from SpawnSystemAtLocation) so it
	// reverses with the global rewind. Must still be alive when rewind starts.
	UFUNCTION(BlueprintCallable, Category = "Chronogy")
	void RegisterRewindableFX(UNiagaraComponent* FXComponent, EChronogyParticleRewindMode Mode = EChronogyParticleRewindMode::Scrub);

	// Shared Niagara rewind ops for both owner-attached (UChronogyComponent) and detached FX paths.
	// Continuous-age model: a Scrub system is set to solo + DesiredAge ONCE (ConfigureTrackForRewind)
	// for life, then aged up each forward frame (PollTrackActivation) and down each rewind frame
	// (ApplyTrackAgeAtTime). The mode never switches, so it never vanishes/freezes on rewind release.
	static void ConfigureTrackForRewind(FChronogyParticleTrack& Track, float SeekDelta);
	static void PollTrackActivation(FChronogyParticleTrack& Track, float Now);
	static void ApplyTrackAgeAtTime(FChronogyParticleTrack& Track, float RewindClock);
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

	float GetTimelineSeconds() const;

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
	float   LastRealTimeSeconds    = 0.0f;
	float   TimelineOffset         = 0.0f;
	


	
	
};
