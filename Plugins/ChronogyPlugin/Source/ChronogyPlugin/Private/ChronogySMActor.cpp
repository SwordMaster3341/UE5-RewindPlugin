// Fill out your copyright notice in the Description page of Project Settings.


#include "ChronogySMActor.h"
#include "Components/StaticMeshComponent.h"
#include "ChronogyComponent.h"

// Sets default values
AChronogySMActor::AChronogySMActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	SetRootComponent(StaticMesh);

	ChronogyComponent = CreateDefaultSubobject<UChronogyComponent>(TEXT("ChronogyComponent"));
}
