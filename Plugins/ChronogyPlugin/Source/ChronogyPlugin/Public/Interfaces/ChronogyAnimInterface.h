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

	//Simple interface that allows animations to push each pose snapshot to the ChronogyComponent 
	virtual void PushRewindPoseSnapshot(const FPoseSnapshot& Snapshot) = 0;

	//Interface that flags when to switch between normal playback and rewinding
	virtual void SetIsRewinding(bool bIsRewinding) = 0;


};
