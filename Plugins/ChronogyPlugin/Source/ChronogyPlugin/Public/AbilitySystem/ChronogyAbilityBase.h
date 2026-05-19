// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AttributeSet.h"
#include "ChronogyAbilityBase.generated.h"

/**
 * 
 */
UCLASS()
class CHRONOGYPLUGIN_API UChronogyAbilityBase : public UGameplayAbility
{
	GENERATED_BODY()


public:
	//The attribute to check and deduct when this ability activates
	//Set this in the ability Blueprint defaults (pointing it to mana, ect...)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chronogy|Cost")
	FGameplayAttribute CostAttribute;

	//How much of the attribute to deduct when this ability activates
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chronogy|Cost")
	float CostAmount;

	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;

};
