// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/ChronogyAbilityBase.h"
#include "AbilitySystemComponent.h"

bool UChronogyAbilityBase::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
    //Cost Validation


    if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags))
    {
        return false;
    }

    UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
    if (ASC && CostAttribute.IsValid())
    {
        if (ASC->GetNumericAttribute(CostAttribute) < CostAmount)
        {
            return false;
        }
    }

    return true;
}

void UChronogyAbilityBase::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (ASC && CostAttribute.IsValid())
	{
		ASC->ApplyModToAttributeUnsafe(CostAttribute, EGameplayModOp::Additive, -CostAmount);
	}
}

