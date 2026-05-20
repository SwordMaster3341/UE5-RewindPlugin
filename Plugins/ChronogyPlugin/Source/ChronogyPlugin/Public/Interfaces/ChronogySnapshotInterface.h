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

	// Called at each snapshot interval during normal play. Store current state into your buffer.
	virtual void CaptureSnapshot() = 0;

	// Called every tick during rewind with the current rewind timestamp. Restore the state recorded at or before that time.
	virtual void ApplySnapshot(float Timestamp) = 0;

	// Called when rewind ends. Discard any buffered states with a timestamp later than FromTimestamp.
	virtual void EraseFutureSnapshots(float FromTimestamp) = 0;
};
