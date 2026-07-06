// Blake de Armas

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/ChronogyAbilityBase.h"
#include "ChronogyTimeStopAbility.generated.h"

//Simple time stop ability, stops everything including the player.

UCLASS()
class CHRONOGYPLUGIN_API UChronogyTimeStopAbility : public UChronogyAbilityBase
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
