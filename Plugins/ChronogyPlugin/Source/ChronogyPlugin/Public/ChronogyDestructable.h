// Blake de Armas

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Containers/RingBuffer.h"
#include "Interfaces/ChronogySnapshotInterface.h"
#include "ChronogyDestructable.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UChronogyComponent;
class UChronogySubsystem;

/**
 * A baked-fragment destructible. Holds one intact mesh plus N pre-fractured fragment meshes
 * (exported from the Chaos Fracture editor via "export to mesh"). On TriggerDestruction the intact
 * mesh is hidden, the fragments are revealed and simulated as plain rigid bodies, and their motion
 * is recorded for rewind — no GeometryCollection / Chaos runtime dependency.
 *
 * Implements IChronogySnapshotInterface so its sibling UChronogyComponent drives Capture/Apply/Erase.
 * Two buffers are kept separate by design:
 *   - StateBuffer     : step-function intact<->fractured history (sparse, recorded on change only)
 *   - TransformBuffer : per-fragment world transforms, the bulk, recorded only while fractured
 * Frame 0 is free: intact == fragments at their rest transforms, so nothing is stored until a break.
 */

// Step-function state: did the set break, and when.
USTRUCT()
struct FChronogyDestructableStateFrame
{
	GENERATED_BODY()

	float Timestamp = 0.0f;
	bool  bFractured = false;
};

// Bulk per-fragment transforms (relative to the actor root), recorded only while fractured.
// Relative — not world — so they compose correctly with the root transform that UChronogyComponent
// rewinds independently, keeping fragments glued to the cube's frame even if it was moved/rotated.
USTRUCT()
struct FChronogyDestructableTransformFrame
{
	GENERATED_BODY()

	float              Timestamp = 0.0f;
	TArray<FTransform> FragmentTransforms;
};

UCLASS()
class CHRONOGYPLUGIN_API AChronogyDestructable : public AActor, public IChronogySnapshotInterface
{
	GENERATED_BODY()

public:
	AChronogyDestructable();

	// IChronogySnapshotInterface — called by the sibling UChronogyComponent.
	virtual void CaptureSnapshot() override;
	virtual void ApplySnapshot(float Timestamp) override;
	virtual void EraseFutureSnapshots(float FromTimestamp) override;

	// Break the set at the actor origin using the configured default impulse.
	UFUNCTION(BlueprintCallable, Category = "Chronogy|Destructable")
	void TriggerDestruction();

	// Break the set with an explicit radial impulse (e.g. from a hit location).
	UFUNCTION(BlueprintCallable, Category = "Chronogy|Destructable")
	void TriggerDestructionWithImpulse(FVector Origin, float Strength);

	UFUNCTION(BlueprintPure, Category = "Chronogy|Destructable")
	bool IsFractured() const { return bFractured; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// Root. Shown while intact, hidden once fractured.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chronogy")
	TObjectPtr<UStaticMeshComponent> IntactMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chronogy")
	TObjectPtr<UChronogyComponent> ChronogyComponent;

	// The single mesh shown before the break (e.g. SM_DestroyedCubeBase).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chronogy|Destructable")
	TObjectPtr<UStaticMesh> IntactStaticMesh;

	// One entry per exported fragment (GC_..._SM_1_ .. _32_). Array order defines fragment index.
	// Each fragment mesh must have simple collision for physics to simulate.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chronogy|Destructable")
	TArray<TObjectPtr<UStaticMesh>> FragmentMeshes;

	// Per-fragment mass override in kg. 0 = use the mesh's computed mass.
	UPROPERTY(EditAnywhere, Category = "Chronogy|Destructable")
	float FragmentMassKg = 0.0f;

	// Strength of the radial impulse applied by the parameterless TriggerDestruction.
	UPROPERTY(EditAnywhere, Category = "Chronogy|Destructable")
	float DefaultImpulseStrength = 250.0f;

	// Radius of the radial impulse applied on fracture.
	UPROPERTY(EditAnywhere, Category = "Chronogy|Destructable")
	float DefaultImpulseRadius = 200.0f;

	// Cap on recorded transform frames. Default ~15s @ 30Hz, matching the UChronogyComponent budget.
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	int32 MaxFrames = 450;

private:
	void CreateFragmentComponents();
	void SetFragmentsSimulating(bool bSimulate);
	void ShowFragments(bool bShow);          // toggles intact mesh vs fragment visibility + collision
	void RestoreRestState();                 // snap fragments to rest, show intact mesh, physics off
	void ApplyFracturedTransforms(float Timestamp);
	void RestoreFragmentVelocities();        // estimate velocity from the last two frames on resume
	bool IsFracturedAtTime(float Timestamp) const;

	UFUNCTION()
	void OnRewindStarted();

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> FragmentComponents;

	TArray<FTransform>                   RestRelativeTransforms; // frame 0, fragment-local relative to root
	TArray<FChronogyDestructableStateFrame>     StateBuffer;      // step-function, appended on change only — not count-capped
	TRingBuffer<FChronogyDestructableTransformFrame> TransformBuffer; // bulk per-fragment transforms, fixed window

	UPROPERTY()
	TObjectPtr<UChronogySubsystem> Subsystem;

	bool  bFractured              = false;
	bool  bFragmentsVisible       = false;
	bool  bIntactSimulatesPhysics = false; // designer intent captured at BeginPlay
	float FractureTimestamp       = 0.0f;
};
