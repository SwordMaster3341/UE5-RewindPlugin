// S-G-D

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Animation/PoseSnapshot.h"
#include "ChronogyAnimInterface.generated.h"

// UChronogyAnimInterface is the UObject shell required by UInterface — not Blueprintable, C++ only.
UINTERFACE(MinimalAPI)
class UChronogyAnimInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class CHRONOGYPLUGIN_API IChronogyAnimInterface
{
	GENERATED_BODY()

public:

	// Called each tick during rewind with the current blended pose. Store it and display it while bIsRewinding is true.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Chronogy")
	void PushRewindPoseSnapshot(const FPoseSnapshot& Snapshot);

	// Flags when to switch between normal playback and rewinding.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Chronogy")
	void SetIsRewinding(bool bIsRewinding);


};
