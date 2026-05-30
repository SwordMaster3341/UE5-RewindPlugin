// S-G-D

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Animation/PoseSnapshot.h"
#include "ChronogyAnimInterface.generated.h"

// UChronogyAnimInterface is the UObject shell required by UInterface. 
UINTERFACE(MinimalAPI)
class UChronogyAnimInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * The purpose of the AnimInterface is to allow any Actor's animation blueprint to be rewindable no matter the state.
 * 
 * The setup is as follows:
 * 
 * Implement the ChronogyInterface
 * Override the two BlueprintNativeEvent functions to store the incoming pose snapshot and bool in the anim instance each tick.
 * Blend Pose By Bool (bIsRewinding) node in the anim graph, with the normal pose on one side and a Pose Snapshot on the other.
 * 
 * It is done this way so that no matter what skeleton or animation blueprint setup you have,
 * you can still have a rewind pose by just implementing this interface and blending to the snapshot pose when bIsRewinding is true.
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
