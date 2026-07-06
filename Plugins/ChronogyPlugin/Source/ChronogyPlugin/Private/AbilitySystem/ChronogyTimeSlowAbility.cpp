// Blake de Armas

#include "AbilitySystem/ChronogyTimeSlowAbility.h"
#include "ChronogySubsystem.h"

//Activates ChronogyTimeSlowAbility, setting the global time dilation in the ChronogySubsystem and adjusting the avatar's CustomTimeDilation
void UChronogyTimeSlowAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (UChronogySubsystem* Subsystem = GetWorld()->GetSubsystem<UChronogySubsystem>())
	{
		Subsystem->SetTimeDilation(TimeDilation);
	}

	if (AActor* Avatar = ActorInfo->AvatarActor.Get())
	{
		Avatar->CustomTimeDilation = 1.0f / TimeDilation;
	}
}

//Resets the global time dilation in the ChronogySubsystem and resets the avatar's CustomTimeDilation when the ability ends
void UChronogyTimeSlowAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UChronogySubsystem* Subsystem = GetWorld()->GetSubsystem<UChronogySubsystem>())
	{
		Subsystem->ResetTimeDilation();
	}

	if (AActor* Avatar = ActorInfo->AvatarActor.Get())
	{
		Avatar->CustomTimeDilation = 1.0f;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
