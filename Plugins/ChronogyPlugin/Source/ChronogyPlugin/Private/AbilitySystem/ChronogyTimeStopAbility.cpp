// S-G-D

#include "AbilitySystem/ChronogyTimeStopAbility.h"
#include "ChronogySubsystem.h"
#include "GameFramework/PlayerController.h"

void UChronogyTimeStopAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    if (UChronogySubsystem* Subsystem = GetWorld()->GetGameInstance()->GetSubsystem<UChronogySubsystem>())
    {
        Subsystem->SetTimeDilation(0.0f);
    }
}

void UChronogyTimeStopAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    if (UChronogySubsystem* Subsystem = GetWorld()->GetGameInstance()->GetSubsystem<UChronogySubsystem>())
    {
        Subsystem->ResetTimeDilation();
    }

    if (APlayerController* PC = Cast<APlayerController>(ActorInfo->PlayerController.Get()))
    {
        PC->CustomTimeDilation = 1.0f;
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
