// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Animation/PoseSnapshot.h"
#include "ChronogyAnimInterface.generated.h"

//ChronogyAniminterface is simply the UObject shell required for any UInterface
// No Bluprints, created as CPP only
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
