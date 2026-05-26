// S-G-D

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChronogySubsystem.generated.h"

class UChronogyComponent;

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
	void OnRewindTick(float Timestamp);


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

	float   CurrentRewindTimestamp = 0.0f;
	uint64  LastRewindTickFrame    = 0;
	


	
	
};
