// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ChronogySnapshotInterface.generated.h"


UINTERFACE(MinimalAPI, NotBlueprintable)
class UChronogySnapshotInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class CHRONOGYPLUGIN_API IChronogySnapshotInterface
{
	GENERATED_BODY()

public:

	//Interface to capture the snapshots
	virtual void CaptureSnapshot() = 0;

	//Interface to Apply snapshots
	virtual void ApplySnapshot() = 0;

	//Interface to erase future snapshots when a new snapshot is captured while rewinding
	virtual void EraseFutureSnapshots() = 0;
};
