// S-G-D

#include "ChronogyComponent.h"
#include "ChronogySubsystem.h"
#include "Interfaces/ChronogyAnimInterface.h"
#include "Interfaces/ChronogySnapshotInterface.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"

UChronogyComponent::UChronogyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UChronogyComponent::BeginPlay()
{
	Super::BeginPlay();

	MaxSnapshotCount = FMath::Min
	(
		FMath::CeilToInt(MaxRewindSecconds / SnapShotFrequencySeconds),
		MaxMemoryBytes / static_cast<int32>(sizeof(FChronogySnapshot))
	);

	SnapshotBuffer.Reserve(MaxSnapshotCount);
	SnapshotInterface = Cast<IChronogySnapshotInterface>(GetOwner());

	UChronogySubsystem* Subsystem = GetWorld()->GetGameInstance()->GetSubsystem<UChronogySubsystem>();
	if (Subsystem)
	{
		Subsystem->RegisterComponent(this);
		Subsystem->OnRewindStarted.AddDynamic(this, &UChronogyComponent::OnRewindStart);
		Subsystem->OnRewindCompleted.AddDynamic(this, &UChronogyComponent::OnRewindEnd);
	}

	LastRealTimeSeconds = GetWorld()->GetRealTimeSeconds();
}

void UChronogyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UChronogySubsystem* Subsystem = GetWorld()->GetGameInstance()->GetSubsystem<UChronogySubsystem>();
	if (Subsystem)
	{
		Subsystem->UnregisterComponent(this);
		Subsystem->OnRewindStarted.RemoveDynamic(this, &UChronogyComponent::OnRewindStart);
		Subsystem->OnRewindCompleted.RemoveDynamic(this, &UChronogyComponent::OnRewindEnd);
	}

	Super::EndPlay(EndPlayReason);
}

void UChronogyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	float RealNow = GetWorld()->GetRealTimeSeconds();
	float RealDelta = RealNow - LastRealTimeSeconds;
	LastRealTimeSeconds = RealNow;

	if (bIsRewinding)
	{
		UChronogySubsystem* Subsystem = GetWorld()->GetGameInstance()->GetSubsystem<UChronogySubsystem>();
		float RewindSpeed = Subsystem ? Subsystem->GlobalRewindSpeed : 1.0f;

		RewindPlaybackTime -= RealDelta * RewindSpeed;
		ApplySnapshotAtTime(RewindPlaybackTime);
		PlayBonePoseSnapshots();
		return;
	}

	TimeSinceLastSnapshot += DeltaTime;
	if (TimeSinceLastSnapshot >= SnapShotFrequencySeconds)
	{
		RecordSnapshot();
		TimeSinceLastSnapshot = 0.f;
	}
}

void UChronogyComponent::OnRewindStart()
{
	bIsRewinding = true;
	RewindPlaybackTime = GetWorld()->GetRealTimeSeconds();
}

void UChronogyComponent::OnRewindEnd()
{
	bIsRewinding = false;
	EraseFutureSnapshots(RewindPlaybackTime);
}

IChronogyAnimInterface* UChronogyComponent::GetAnimInterface() const
{
	if (USkeletalMeshComponent* Mesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>())
	{
		return Cast<IChronogyAnimInterface>(Mesh->GetAnimInstance());
	}
	return nullptr;
}

void UChronogyComponent::RecordSnapshot() {}
void UChronogyComponent::ApplySnapshotAtTime(float Timestamp) {}
void UChronogyComponent::EraseFutureSnapshots(float FromTimestamp) {}
void UChronogyComponent::PlayBonePoseSnapshots() {}
