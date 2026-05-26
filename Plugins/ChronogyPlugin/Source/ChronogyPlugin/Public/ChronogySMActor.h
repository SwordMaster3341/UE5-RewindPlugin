// S-G-D

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChronogySMActor.generated.h"

// Forward Declarations
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
