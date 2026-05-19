// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "ChronogyComponent.generated.h"


/**
 *This Component focuses on exposing functions to drive global time manipulation operations
 and events for other actors/components to bind against and be notified when global time manipulation
 operations begin and end.

 Based heavily on ue5-rewind by NU Makes Games
 Original Code https://github.com/NuMakesGames/ue5-rewind/tree/main?tab=MIT-1-ov-file
 Licensed under MIT License (see LICENSE.txt)

 */

//Forward Declerations

class UChronogySubsystem;
class IChronogyAnimInterface;
class IChronogySnapshotInterface;


USTRUCT()
struct FChronogySnapshot
{
	GENERATED_BODY()
	
	float   Timestamp       = 0.0f;
	FVector Location        = FVector::ZeroVector;
	FQuat   Rotation        = FQuat::Identity;
	FVector LinearVelocity  = FVector::ZeroVector;
	FVector AngularVelocity = FVector::ZeroVector;
	uint8   MovementMode    = 0;

};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class CHRONOGYPLUGIN_API UChronogyComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UChronogyComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;


	//--------===Rewind Variables===-------\\

	//Maximum number of seconds that can be rewound.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	float MaxRewindSecconds = 15.0f;

	//Frequency at which snapshots are taken, 0.03333f is the default which is roughly 30 snapshots per second.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	float SnapShotFrequencySeconds = 0.03333f; 

	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	int32 MaxMemoryBytes = 2 * 1024 * 1024; //2MB is the defualt memory for snapshot storage and acts as a hard cap.

public:	
	void RecordSnapshot();
	void ApplySnapshotAtTime(float Timestamp);
	void EraseFutureSnapshots(float FromTimestamp);
	void PlayBonePoseSnapshots();

	IChronogyAnimInterface* GetAnimInterface() const;

	TArray<FChronogySnapshot>   SnapshotBuffer;
	IChronogySnapshotInterface* SnapshotInterface = nullptr;
	bool bIsRewinding = false;
	float TimeSinceLastSnapshot = 0.0f;
	float RewindPlaybackTime = 0.0f;
	int32 MaxSnapshotCount = 0;
	float LastRealTimeSeconds = 0.0f;

	UFUNCTION()
	void OnRewindStart();

	UFUNCTION()
	void OnRewindEnd();

		
};
