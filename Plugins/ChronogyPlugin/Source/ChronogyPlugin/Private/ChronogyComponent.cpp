// S-G-D

#include "ChronogyComponent.h"
#include "ChronogySubsystem.h"
#include "ChronogyLogs.h"
#include "Interfaces/ChronogyAnimInterface.h"
#include "Interfaces/ChronogySnapshotInterface.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/PoseSnapshot.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"

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

	OwnerRootComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());

	if (bSnapshotMovementVelocityAndMode)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			OwnerMovementComponent = Cast<UCharacterMovementComponent>(Character->GetMovementComponent());
		}
	}

	if (bSnapshotBonePoses)
	{
		OwnerSkeletalMesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
		if (!OwnerSkeletalMesh)
		{
			UE_LOG(LogChronogy, Warning, TEXT("[%s] bSnapshotBonePoses=true but no USkeletalMeshComponent found — bone poses will not be recorded."), *GetOwner()->GetName());
		}
		MaxBonePoseCount  = FMath::CeilToInt(MaxRewindSecconds /
			(SnapShotFrequencySeconds * FMath::Max(1, BoneSnapshotFrameInterval)));
		BonePoseBuffer.Reserve(MaxBonePoseCount);
	}

	Subsystem = GetWorld()->GetGameInstance()->GetSubsystem<UChronogySubsystem>();
	if (Subsystem)
	{
		Subsystem->RegisterComponent(this);
		Subsystem->OnRewindStarted.AddUniqueDynamic(this, &UChronogyComponent::OnRewindStarted);
		Subsystem->OnRewindCompleted.AddUniqueDynamic(this, &UChronogyComponent::OnRewindCompleted);
		UE_LOG(LogChronogy, Log, TEXT("[%s] Registered with ChronogySubsystem. MaxSnapshots=%d, MaxBonePoses=%d"), *GetOwner()->GetName(), MaxSnapshotCount, MaxBonePoseCount);
	}
	else
	{
		UE_LOG(LogChronogy, Error, TEXT("[%s] ChronogySubsystem not found — component will not rewind."), *GetOwner()->GetName());
	}

	LastRealTimeSeconds = GetWorld()->GetRealTimeSeconds();
}

void UChronogyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Subsystem)
	{
		Subsystem->UnregisterComponent(this);
		Subsystem->OnRewindStarted.RemoveDynamic(this, &UChronogyComponent::OnRewindStarted);
		Subsystem->OnRewindCompleted.RemoveDynamic(this, &UChronogyComponent::OnRewindCompleted);
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
		float RewindSpeed = Subsystem ? Subsystem->GlobalRewindSpeed : 1.0f;
		RewindPlaybackTime -= RealDelta * RewindSpeed;
		ApplySnapshotAtTime(RewindPlaybackTime);
		PlayBonePoseSnapshots();
	}
	else
	{
		// Use real delta so snapshot intervals are consistent during time dilation
		TimeSinceLastSnapshot += RealDelta;
		if (TimeSinceLastSnapshot >= SnapShotFrequencySeconds)
		{
			RecordSnapshot();
			TimeSinceLastSnapshot = 0.f;
		}
	}

#if ENABLE_DRAW_DEBUG
	if (CVarChronogyDebugDraw.GetValueOnGameThread() && SnapshotBuffer.Num() > 0)
	{
		UWorld* World = GetWorld();
		if (bIsRewinding)
		{
			for (int32 i = 1; i < SnapshotBuffer.Num(); i++)
			{
				DrawDebugLine(World, SnapshotBuffer[i - 1].Location, SnapshotBuffer[i].Location, FColor::Yellow, false, -1.f, 0, 0.5f);
			}
			DrawDebugSphere(World, GetOwner()->GetActorLocation(), 16.f, 8, FColor::Red, false, -1.f, 0, 2.f);
		}
		else
		{
			for (int32 i = 1; i < SnapshotBuffer.Num(); i++)
			{
				DrawDebugLine(World, SnapshotBuffer[i - 1].Location, SnapshotBuffer[i].Location, FColor::Green, false, -1.f, 0, 0.5f);
			}
			DrawDebugSphere(World, SnapshotBuffer.Last().Location, 8.f, 6, FColor::Green, false, -1.f, 0, 1.f);
		}
	}
#endif
}

void UChronogyComponent::OnRewindStarted()
{
	bIsRewinding = true;
	RewindPlaybackTime = GetWorld()->GetRealTimeSeconds();

	UE_LOG(LogChronogy, Log, TEXT("[%s] Rewind started. Snapshots: %d/%d  BonePoses: %d/%d"),
		*GetOwner()->GetName(), SnapshotBuffer.Num(), MaxSnapshotCount, BonePoseBuffer.Num(), MaxBonePoseCount);

	if (OwnerRootComponent && OwnerRootComponent->BodyInstance.bSimulatePhysics)
	{
		bPausedPhysics = true;
		OwnerRootComponent->SetSimulatePhysics(false);
	}

	if (UAnimInstance* AnimInst = GetAnimInterface())
	{
		IChronogyAnimInterface::Execute_SetIsRewinding(AnimInst, true);
	}
}

void UChronogyComponent::OnRewindCompleted()
{
	bIsRewinding = false;

	if (bPausedPhysics && OwnerRootComponent)
	{
		bPausedPhysics = false;
		OwnerRootComponent->SetSimulatePhysics(true);
		OwnerRootComponent->RecreatePhysicsState();

		// Restore velocity from the snapshot closest to where we stopped
		for (int32 i = SnapshotBuffer.Num() - 1; i >= 0; --i)
		{
			if (SnapshotBuffer[i].Timestamp <= RewindPlaybackTime)
			{
				OwnerRootComponent->SetPhysicsLinearVelocity(SnapshotBuffer[i].LinearVelocity);
				OwnerRootComponent->SetPhysicsAngularVelocityInRadians(SnapshotBuffer[i].AngularVelocity);
				if (OwnerMovementComponent)
				{
					OwnerMovementComponent->Velocity = SnapshotBuffer[i].LinearVelocity;
				}
				break;
			}
		}
	}

	UE_LOG(LogChronogy, Log, TEXT("[%s] Rewind completed at T=%.3f. Snapshots remaining: %d"),
		*GetOwner()->GetName(), RewindPlaybackTime, SnapshotBuffer.Num());

	EraseFutureSnapshots(RewindPlaybackTime);

	if (UAnimInstance* AnimInst = GetAnimInterface())
	{
		IChronogyAnimInterface::Execute_SetIsRewinding(AnimInst, false);
	}
}

UAnimInstance* UChronogyComponent::GetAnimInterface() const
{
	USkeletalMeshComponent* Mesh = OwnerSkeletalMesh
		? OwnerSkeletalMesh
		: GetOwner()->FindComponentByClass<USkeletalMeshComponent>();

	if (!Mesh)
	{
		UE_LOG(LogChronogy, Warning, TEXT("[%s] GetAnimInterface: no SkeletalMeshComponent found."), *GetOwner()->GetName());
		return nullptr;
	}

	UAnimInstance* AnimInst = Mesh->GetAnimInstance();
	if (!AnimInst)
	{
		UE_LOG(LogChronogy, Warning, TEXT("[%s] GetAnimInterface: GetAnimInstance() returned null."), *GetOwner()->GetName());
		return nullptr;
	}

	const bool bImplements = AnimInst->GetClass()->ImplementsInterface(UChronogyAnimInterface::StaticClass());
	if (!bImplements)
	{
		UE_LOG(LogChronogy, Warning, TEXT("[%s] GetAnimInterface: %s does not implement IChronogyAnimInterface."),
			*GetOwner()->GetName(), *AnimInst->GetClass()->GetName());
	}

	return bImplements ? AnimInst : nullptr;
}

void UChronogyComponent::RecordSnapshot()
{
	// Drop oldest snapshot to make room if the buffer is full
	if (SnapshotBuffer.Num() >= MaxSnapshotCount)
	{
		UE_LOG(LogChronogy, VeryVerbose, TEXT("[%s] Snapshot buffer full (%d) — dropping oldest."), *GetOwner()->GetName(), MaxSnapshotCount);
		SnapshotBuffer.RemoveAt(0, 1, EAllowShrinking::No);
	}

	FChronogySnapshot Snap;
	Snap.Timestamp = GetWorld()->GetRealTimeSeconds();
	Snap.Location  = GetOwner()->GetActorLocation();
	Snap.Rotation  = GetOwner()->GetActorQuat();

	if (OwnerRootComponent)
	{
		Snap.LinearVelocity  = OwnerRootComponent->GetPhysicsLinearVelocity();
		Snap.AngularVelocity = OwnerRootComponent->GetPhysicsAngularVelocityInRadians();
	}

	if (OwnerMovementComponent)
	{
		Snap.MovementMode = static_cast<uint8>(OwnerMovementComponent->MovementMode.GetValue());
	}

	SnapshotBuffer.Add(Snap);

	if (bSnapshotBonePoses && OwnerSkeletalMesh)
	{
		FramesSinceLastBoneSnapshot++;
		if (FramesSinceLastBoneSnapshot >= BoneSnapshotFrameInterval)
		{
			FramesSinceLastBoneSnapshot = 0;

			if (BonePoseBuffer.Num() >= MaxBonePoseCount)
				BonePoseBuffer.RemoveAt(0, 1, EAllowShrinking::No);

			FChronogyPoseSnapshot& PoseSnap = BonePoseBuffer.AddDefaulted_GetRef();
			PoseSnap.Timestamp = GetWorld()->GetRealTimeSeconds();
			OwnerSkeletalMesh->SnapshotPose(PoseSnap.PoseSnapshot);
		}
	}

	if (SnapshotInterface)
	{
		SnapshotInterface->CaptureSnapshot();
	}
}

void UChronogyComponent::ApplySnapshotAtTime(float Timestamp)
{
	if (SnapshotBuffer.Num() == 0) { return; }

	// Clamp to the oldest recorded state
	if (Timestamp <= SnapshotBuffer[0].Timestamp)
	{
		const FChronogySnapshot& Snap = SnapshotBuffer[0];
		GetOwner()->SetActorLocationAndRotation(Snap.Location, Snap.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		if (OwnerMovementComponent)
		{
			OwnerMovementComponent->SetMovementMode(static_cast<EMovementMode>(Snap.MovementMode));
		}
		if (SnapshotInterface) { SnapshotInterface->ApplySnapshot(Timestamp); }
		return;
	}

	// Clamp to the newest recorded state — we haven't gone back far enough yet
	if (Timestamp >= SnapshotBuffer.Last().Timestamp)
	{
		return;
	}

	// Binary search for the two snapshots that bracket Timestamp
	int32 Lo = 0;
	int32 Hi = SnapshotBuffer.Num() - 1;
	while (Lo + 1 < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (SnapshotBuffer[Mid].Timestamp <= Timestamp)
			Lo = Mid;
		else
			Hi = Mid;
	}

	const FChronogySnapshot& Older = SnapshotBuffer[Lo];
	const FChronogySnapshot& Newer = SnapshotBuffer[Hi];

	const float Range = Newer.Timestamp - Older.Timestamp;
	const float Alpha = FMath::Clamp((Timestamp - Older.Timestamp) / Range, 0.f, 1.f);

	const FVector BlendedLocation = FMath::Lerp(Older.Location, Newer.Location, Alpha);
	const FQuat   BlendedRotation = FQuat::Slerp(Older.Rotation, Newer.Rotation, Alpha);

	GetOwner()->SetActorLocationAndRotation(BlendedLocation, BlendedRotation, false, nullptr, ETeleportType::TeleportPhysics);

	if (OwnerMovementComponent)
	{
		// Discrete value — snap to whichever side of the midpoint we're on
		const EMovementMode Mode = Alpha < 0.5f
			? static_cast<EMovementMode>(Older.MovementMode)
			: static_cast<EMovementMode>(Newer.MovementMode);
		OwnerMovementComponent->SetMovementMode(Mode);
	}

	if (SnapshotInterface)
	{
		SnapshotInterface->ApplySnapshot(Timestamp);
	}
}

void UChronogyComponent::EraseFutureSnapshots(float FromTimestamp)
{
	// Buffer is sorted oldest→newest, so trim from the back
	for (int32 i = SnapshotBuffer.Num() - 1; i >= 0; --i)
	{
		if (SnapshotBuffer[i].Timestamp > FromTimestamp)
		{
			SnapshotBuffer.RemoveAt(i, 1, EAllowShrinking::No);
		}
		else
		{
			break;
		}
	}

	for (int32 i = BonePoseBuffer.Num() - 1; i >= 0; --i)
	{
		if (BonePoseBuffer[i].Timestamp > FromTimestamp)
			BonePoseBuffer.RemoveAt(i, 1, EAllowShrinking::No);
		else
			break;
	}

	if (SnapshotInterface)
	{
		SnapshotInterface->EraseFutureSnapshots(FromTimestamp);
	}
}

void UChronogyComponent::PlayBonePoseSnapshots()
{
	if (!bSnapshotBonePoses || BonePoseBuffer.Num() < 2) { return; }

	UAnimInstance* AnimInst = GetAnimInterface();
	if (!AnimInst) { return; }

	// Clamp to oldest recorded pose
	if (RewindPlaybackTime <= BonePoseBuffer[0].Timestamp)
	{
		IChronogyAnimInterface::Execute_PushRewindPoseSnapshot(AnimInst, BonePoseBuffer[0].PoseSnapshot);
		return;
	}

	// Clamp to newest recorded pose
	if (RewindPlaybackTime >= BonePoseBuffer.Last().Timestamp)
	{
		IChronogyAnimInterface::Execute_PushRewindPoseSnapshot(AnimInst, BonePoseBuffer.Last().PoseSnapshot);
		return;
	}

	// Binary search for the two poses bracketing RewindPlaybackTime
	int32 Lo = 0, Hi = BonePoseBuffer.Num() - 1;
	while (Lo + 1 < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (BonePoseBuffer[Mid].Timestamp <= RewindPlaybackTime) Lo = Mid;
		else Hi = Mid;
	}

	const FChronogyPoseSnapshot& Older = BonePoseBuffer[Lo];
	const FChronogyPoseSnapshot& Newer = BonePoseBuffer[Hi];

	const float Range = Newer.Timestamp - Older.Timestamp;
	const float Alpha = FMath::Clamp((RewindPlaybackTime - Older.Timestamp) / Range, 0.f, 1.f);

	FPoseSnapshot BlendedPose = Newer.PoseSnapshot;
	const int32 BoneCount = FMath::Min(
		Older.PoseSnapshot.LocalTransforms.Num(),
		Newer.PoseSnapshot.LocalTransforms.Num());

	for (int32 i = 0; i < BoneCount; i++)
	{
		BlendedPose.LocalTransforms[i].Blend(
			Older.PoseSnapshot.LocalTransforms[i],
			Newer.PoseSnapshot.LocalTransforms[i],
			Alpha);
	}

	if (CVarChronogyDebugAnim.GetValueOnGameThread())
	{
		UE_LOG(LogChronogy, Log, TEXT("[%s] PushRewindPoseSnapshot: T=%.3f  Alpha=%.3f  Bones=%d"),
			*GetOwner()->GetName(), RewindPlaybackTime, Alpha, BoneCount);
	}

	IChronogyAnimInterface::Execute_PushRewindPoseSnapshot(AnimInst, BlendedPose);
}
