// Blake de Armas

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/ChronogyAbilityBase.h"
#include "ChronogyTimeSlowAbility.generated.h"

// Simple Time slow ability, which sets global time dilation to a specified value and adjusts the player's custom time dilation to compensate
// creating a slow motion effect for everything except the player. Time dilation is reset when the ability ends.

UCLASS()
class CHRONOGYPLUGIN_API UChronogyTimeSlowAbility : public UChronogyAbilityBase
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, Category = "Chronogy|TimeSlow", meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float TimeDilation = 0.3f;

    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
