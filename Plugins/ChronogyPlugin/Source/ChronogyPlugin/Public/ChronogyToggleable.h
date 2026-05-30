// S-G-D

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Containers/RingBuffer.h"
#include "Interfaces/ChronogySnapshotInterface.h"
#include "ChronogyToggleable.generated.h"

class UStaticMeshComponent;
class UChronogyComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnToggleableStateChanged, bool, bNewState);

USTRUCT()
struct FChronogyToggleableFrame
{
	GENERATED_BODY()

	float Timestamp = 0.0f;
	bool  bIsOn     = false;
};

UCLASS()
class CHRONOGYPLUGIN_API AChronogyToggleable : public AActor, public IChronogySnapshotInterface
{
	GENERATED_BODY()

public:
	AChronogyToggleable();

	// IChronogySnapshotInterface
	virtual void CaptureSnapshot() override;
	virtual void ApplySnapshot(float Timestamp) override;
	virtual void EraseFutureSnapshots(float FromTimestamp) override;

	UFUNCTION(BlueprintCallable, Category = "Chronogy|Toggleable")
	void SetToggleState(bool bNewState);

	UFUNCTION(BlueprintPure, Category = "Chronogy|Toggleable")
	bool IsOn() const { return bIsOn; }

	// Fires whenever the toggle state changes — during normal play and during rewind.
	UPROPERTY(BlueprintAssignable, Category = "Chronogy|Toggleable")
	FOnToggleableStateChanged OnToggleableStateChanged;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chronogy")
	TObjectPtr<UStaticMeshComponent> StaticMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chronogy")
	TObjectPtr<UChronogyComponent> ChronogyComponent;

	// Should match ChronogyComponent's (MaxRewindSeconds / SnapshotFrequencySeconds). Default matches component defaults (15s * 30Hz).
	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	int32 MaxFrames = 450;

private:
	bool bIsOn = false;
	TRingBuffer<FChronogyToggleableFrame> FrameBuffer;
};
