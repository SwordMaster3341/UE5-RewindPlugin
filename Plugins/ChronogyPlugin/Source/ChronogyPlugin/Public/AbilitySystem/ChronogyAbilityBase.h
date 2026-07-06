// Blake de Armas

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AttributeSet.h"
#include "ChronogyAbilityBase.generated.h"

/**
 * This is the ability base class for all Chronogy abilities. 
 It adds a simple cost system based on GameplayAttributes, which can be set in the Blueprint defaults for each ability. W
 hen an ability is activated, it checks whether the actor has enough of the specified attribute (e.g. mana) and deducts the cost amount. 
 If not, the ability fails to activate.
 Essential for triggering rewind, time stop, and other time related mechanics.
 */
UCLASS()
class CHRONOGYPLUGIN_API UChronogyAbilityBase : public UGameplayAbility
{
	GENERATED_BODY()

public:
	//The attribute to check and deduct when this ability activates
	//Remember to set this in the ability Blueprint defaults (pointing it to mana, ect...)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chronogy|Cost")
	FGameplayAttribute CostAttribute;

	//How much of the attribute to deduct when this ability activates, also set in blueprint
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chronogy|Cost")
	float CostAmount;

	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
};
