// S-G-D

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Animation/PoseSnapshot.h"
#include "ChronogyComponent.generated.h"

/**
 * Records transform (and optionally bone-pose / movement) snapshots of its owner at a fixed
 * interval and replays them in reverse when ChronogySubsystem triggers a global rewind.
 *
 * Based on ue5-rewind by NU Makes Games
 * https://github.com/NuMakesGames/ue5-rewind
 * Licensed under MIT License (see LICENSE.txt)
 */

// Forward Declarations
class UChronogySubsystem;
class UPrimitiveComponent;
class UCharacterMovementComponent;
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

USTRUCT()
struct FChronogyPoseSnapshot
{
	GENERATED_BODY()

	float         Timestamp    = 0.0f;
	FPoseSnapshot PoseSnapshot;
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class CHRONOGYPLUGIN_API UChronogyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UChronogyComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//--------===Rewind Variables===-------\\

	// Maximum number of seconds that can be rewound.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	float MaxRewindSeconds = 15.0f;

	// Frequency at which snapshots are taken. 0.03333 ≈ 30 snapshots per second.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	float SnapShotFrequencySeconds = 0.03333f;

	// 2MB is the default memory for snapshot storage and acts as a hard cap.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	int32 MaxMemoryBytes = 2 * 1024 * 1024;

	// If the owner is a Character, also snapshot and restore CharacterMovementComponent velocity and mode.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	bool bSnapshotMovementVelocityAndMode = false;

	// Enable bone pose recording for animation rewind. Requires the anim instance to implement IChronogyAnimInterface.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	bool bSnapshotBonePoses = false;

	// Record a bone pose every N transform snapshots. Higher = less memory, less smooth anim rewind.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy", meta = (EditCondition = "bSnapshotBonePoses", ClampMin = "1"))
	int32 BoneSnapshotFrameInterval = 3;

	// Read-only query used by anim graphs and interface implementors to know when rewind is active.
	UFUNCTION(BlueprintPure, Category = "Chronogy")
	bool IsRewinding() const { return bIsRewinding; }

private:
	void RecordSnapshot();
	void ApplySnapshotAtTime(float Timestamp);
	void EraseFutureSnapshots(float FromTimestamp);
	void PlayBonePoseSnapshots();
	UAnimInstance* GetAnimInstance() const;

	UFUNCTION()
	void OnRewindStarted();

	UFUNCTION()
	void OnRewindCompleted();

	TArray<FChronogySnapshot>     SnapshotBuffer;
	TArray<FChronogyPoseSnapshot> BonePoseBuffer;
	IChronogySnapshotInterface*   SnapshotInterface = nullptr;

	bool  bIsRewinding                = false;
	bool  bPausedPhysics              = false;
	float TimeSinceLastSnapshot       = 0.0f;
	float RewindPlaybackTime          = 0.0f;
	float LastRealTimeSeconds         = 0.0f;
	int32 MaxSnapshotCount            = 0;
	int32 MaxBonePoseCount            = 0;
	int32 FramesSinceLastBoneSnapshot = 0;

	// Cached owner component references
	UPrimitiveComponent*         OwnerRootComponent     = nullptr;
	UCharacterMovementComponent* OwnerMovementComponent = nullptr;
	USkeletalMeshComponent*      OwnerSkeletalMesh      = nullptr;
	UChronogySubsystem*          Subsystem              = nullptr;
};
