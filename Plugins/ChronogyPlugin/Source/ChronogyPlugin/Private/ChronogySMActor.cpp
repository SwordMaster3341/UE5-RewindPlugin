// S-G-D

#include "ChronogySMActor.h"
#include "Components/StaticMeshComponent.h"
#include "ChronogyComponent.h"

AChronogySMActor::AChronogySMActor()
{
	PrimaryActorTick.bCanEverTick = false;

	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	SetRootComponent(StaticMesh);

	ChronogyComponent = CreateDefaultSubobject<UChronogyComponent>(TEXT("ChronogyComponent"));
}
