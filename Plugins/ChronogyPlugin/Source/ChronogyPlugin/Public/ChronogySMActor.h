// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChronogySMActor.generated.h"

//Forward Declerations
class UStaticMeshComponent;
class UChronogyComponent;


UCLASS()
class CHRONOGYPLUGIN_API AChronogySMActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AChronogySMActor();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chronogy")
	TObjectPtr<UStaticMeshComponent> StaticMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chronogy")
	TObjectPtr<UChronogyComponent> ChronogyComponent;
};
