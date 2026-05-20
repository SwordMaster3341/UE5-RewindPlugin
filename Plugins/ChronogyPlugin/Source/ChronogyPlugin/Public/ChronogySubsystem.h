// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChronogySubsystem.generated.h"

class UChronogyComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGlobalRewindStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGlobalRewindCompleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGlobalFastForwardStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGlobalFastForwardCompleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGlobalTimeScrubStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGlobalTimeScrubCompleted);






UCLASS()
class CHRONOGYPLUGIN_API UChronogySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:


	//Component Registration
	void RegisterComponent(UChronogyComponent* Component);
	void UnregisterComponent(UChronogyComponent* Component);


	//Broadcasting events to registered components

	UFUNCTION(BlueprintCallable, Category = "Chronogy")
	void StartGlobalRewind();

	UFUNCTION(BlueprintCallable, Category = "Chronogy")
	void StopGlobalRewind();

	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	float MaxRewindSecconds = 15.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chronogy")
	float GlobalRewindSpeed = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Chronogy")
	float CurrentTimeDilation = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "Chronogy")
	void SetTimeDilation(float Dilation);

	UFUNCTION(BlueprintCallable, Category = "Chronogy")
	void ResetTimeDilation();

	UPROPERTY(BlueprintReadOnly, Category = "Chronogy")
	bool bIsRewinding = false;

	FOnGlobalRewindStarted OnRewindStarted;
	FOnGlobalRewindCompleted OnRewindCompleted;

private:
	TArray<TWeakObjectPtr<UChronogyComponent>> RegisteredComponents;
	


	
	
};
