// Blake de Armas

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Animation/PoseSnapshot.h"
#include "Containers/RingBuffer.h"
#include "ChronogyParticle.h"
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
class USkeletalMeshComponent;
class ULightComponent;
class UMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
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

USTRUCT()
struct FChronogyLightFrame
{
	GENERATED_BODY()

	float        Timestamp  = 0.0f;
	float        Intensity  = 0.0f;
	FLinearColor LightColor = FLinearColor::White;
};

USTRUCT()
struct FChronogyMaterialFrame
{
	GENERATED_BODY()

	float                          Timestamp = 0.0f;
	TWeakObjectPtr<UMaterialInterface> Material;
	TArray<float>                  ScalarValues;
	TArray<FLinearColor>           VectorValues;
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

	// If this actor is spawned at runtime, automatically register its spawn time with ChronogySubsystem so it is destroyed on rewind past its birth. Uncheck for actors that should persist through rewind.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	bool bShouldTrackSpawn = true;

	// Enable bone pose recording for animation rewind. Requires the anim instance to implement IChronogyAnimInterface.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	bool bSnapshotBonePoses = false;

	// Record a bone pose every N transform snapshots. Higher = less memory, less smooth anim rewind.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy", meta = (EditCondition = "bSnapshotBonePoses", ClampMin = "1"))
	int32 BoneSnapshotFrameInterval = 3;

	// If the owner has a ULightComponent, snapshot and rewind its Intensity and Color.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	bool bSnapshotLightProperties = false;

	// If the owner has a mesh, create a UMaterialInstanceDynamic for slot 0 and rewind all detected scalar and vector parameters.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	bool bSnapshotMaterialParameters = false;

	// If the owner has UNiagaraComponents, record their activation times and reverse them during rewind.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	bool bSnapshotParticles = false;

	// Default reversal mechanism for detected Niagara systems. Scrub = true reverse (short CPU bursts); FollowTransform = motion-trails that follow the rewinding owner; Freeze = pause+gate fallback (GPU/looping/long/non-deterministic).
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy", meta = (EditCondition = "bSnapshotParticles"))
	EChronogyParticleRewindMode DefaultParticleRewindMode = EChronogyParticleRewindMode::Scrub;

	// Seek granularity (seconds) while scrubbing Niagara age backward. Larger = cheaper, less smooth.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy", meta = (EditCondition = "bSnapshotParticles", ClampMin = "0.005"))
	float ParticleSeekDelta = 0.0166f;

	// Read-only query used by anim graphs and interface implementors to know when rewind is active.
	UFUNCTION(BlueprintPure, Category = "Chronogy")
	bool IsRewinding() const { return bIsRewinding; }

	TOptional<float> GetOldestSnapshotTime() const
	{
		return SnapshotBuffer.Num() > 0 ? TOptional<float>(SnapshotBuffer[0].Timestamp) : TOptional<float>();
	}

private:
	void RecordSnapshot();
	void ApplySnapshotAtTime(float Timestamp);
	void EraseFutureSnapshots(float FromTimestamp);
	void PlayBonePoseSnapshots();
	void ApplyLightAtTime(float Timestamp);
	void ApplyMaterialAtTime(float Timestamp);
	void DiscoverParticleComponents();
	void PollParticleActivations(float DeltaSeconds);
	void ApplyParticlesAtTime(float DeltaSeconds);
	void EndParticleRewind(float FromTimestamp);
	UAnimInstance* GetAnimInstance() const;

	UFUNCTION()
	void OnRewindStarted();

	UFUNCTION()
	void OnRewindCompleted();

	// TRingBuffers my love, memory management made easy (I did this with arrays first ima cry)
	TRingBuffer<FChronogySnapshot>     SnapshotBuffer;
	TRingBuffer<FChronogyPoseSnapshot> BonePoseBuffer;
	TRingBuffer<FChronogyLightFrame>   LightBuffer;
	TRingBuffer<FChronogyMaterialFrame> MaterialBuffer;

	UPROPERTY()
	TArray<FChronogyParticleTrack> ParticleTracks;

	IChronogySnapshotInterface*   SnapshotInterface = nullptr;

	TArray<FName> DetectedScalarParams;
	TArray<FName> DetectedVectorParams;

	bool  bIsRewinding                = false;
	bool  bPausedPhysics              = false;
	float TimeSinceLastSnapshot       = 0.0f;
	float RewindPlaybackTime          = 0.0f;
	float LastRealTimeSeconds         = 0.0f;
	int32 MaxSnapshotCount            = 0;
	int32 MaxBonePoseCount            = 0;
	int32 FramesSinceLastBoneSnapshot = 0;

	// Cached owner component references that prevent dangling nullpointers during rewind when components may be destroyed and re-created.
	// Still, why the heck are you destroying and re-creating components during rewind, don't do that, it's not gonna end well.
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent>         OwnerRootComponent     = nullptr;
	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> OwnerMovementComponent = nullptr;
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent>      OwnerSkeletalMesh      = nullptr;
	UPROPERTY()
	TObjectPtr<ULightComponent>             OwnerLightComponent    = nullptr;
	UPROPERTY()
	TObjectPtr<UMeshComponent>              OwnerMeshComponent     = nullptr;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic>    OwnerDynMat            = nullptr;
	UPROPERTY()
	TObjectPtr<UChronogySubsystem>          Subsystem              = nullptr;
};
