// Blake de Armas

#include "AbilitySystem/ChronogyRewindAbility.h"
#include "ChronogySubsystem.h"

//Trigger the global rewind in the ChronogySubsystem when the ability is activated
void UChronogyRewindAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UChronogySubsystem* Subsystem = World->GetSubsystem<UChronogySubsystem>())
		{
			Subsystem->StartGlobalRewind();
		}
	}
}

//Stop the global rewind in the ChronogySubsystem when the ability ends
void UChronogyRewindAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		if (UChronogySubsystem* Subsystem = World->GetSubsystem<UChronogySubsystem>())
		{
			Subsystem->StopGlobalRewind();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
