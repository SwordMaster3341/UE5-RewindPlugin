// S-G-D

#include "ChronogyComponent.h"
#include "ChronogySubsystem.h"
#include "ChronogyLogs.h"
#include "Interfaces/ChronogyAnimInterface.h"
#include "Interfaces/ChronogySnapshotInterface.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
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
		FMath::CeilToInt(MaxRewindSeconds / SnapShotFrequencySeconds),
		MaxMemoryBytes / static_cast<int32>(sizeof(FChronogySnapshot))
	);

	SnapshotBuffer.Reserve(MaxSnapshotCount);
	SnapshotInterface = Cast<IChronogySnapshotInterface>(GetOwner());

	OwnerRootComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (!OwnerRootComponent)
	{
		UE_LOG(LogChronogy, Warning, TEXT("[%s] Root component is not a UPrimitiveComponent — physics will not be paused during rewind. Make the Static Mesh the root component in the Blueprint Components panel."), *GetOwner()->GetName());
	}

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
		MaxBonePoseCount  = FMath::CeilToInt(MaxRewindSeconds /
			(SnapShotFrequencySeconds * FMath::Max(1, BoneSnapshotFrameInterval)));
		BonePoseBuffer.Reserve(MaxBonePoseCount);
	}

	if (bSnapshotLightProperties)
	{
		OwnerLightComponent = GetOwner()->FindComponentByClass<ULightComponent>();
		if (OwnerLightComponent)
		{
			LightBuffer.Reserve(MaxSnapshotCount);
			UE_LOG(LogChronogy, Log, TEXT("[%s] Light property snapshotting enabled."), *GetOwner()->GetName());
		}
		else
		{
			UE_LOG(LogChronogy, Warning, TEXT("[%s] bSnapshotLightProperties=true but no ULightComponent found — light will not be rewound."), *GetOwner()->GetName());
		}
	}

	if (bSnapshotMaterialParameters)
	{
		if (UMeshComponent* Mesh = GetOwner()->FindComponentByClass<UMeshComponent>())
		{
			OwnerMeshComponent = Mesh;
			OwnerDynMat = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
			if (!OwnerDynMat)
			{
				OwnerDynMat = Mesh->CreateAndSetMaterialInstanceDynamic(0);
			}

			if (OwnerDynMat)
			{
				TArray<FMaterialParameterInfo> ParamInfos;
				TArray<FGuid> ParamGuids;
				OwnerDynMat->GetAllScalarParameterInfo(ParamInfos, ParamGuids);
				for (const FMaterialParameterInfo& Info : ParamInfos)
				{
					DetectedScalarParams.Add(Info.Name);
				}
				ParamInfos.Reset(); ParamGuids.Reset();
				OwnerDynMat->GetAllVectorParameterInfo(ParamInfos, ParamGuids);
				for (const FMaterialParameterInfo& Info : ParamInfos)
				{
					DetectedVectorParams.Add(Info.Name);
				}
				MaterialBuffer.Reserve(MaxSnapshotCount);
				UE_LOG(LogChronogy, Log, TEXT("[%s] Material parameter snapshotting enabled. Scalar params: %d, Vector params: %d"),
					*GetOwner()->GetName(), DetectedScalarParams.Num(), DetectedVectorParams.Num());
			}
			else
			{
				UE_LOG(LogChronogy, Warning, TEXT("[%s] bSnapshotMaterialParameters=true but could not create UMaterialInstanceDynamic for slot 0."), *GetOwner()->GetName());
			}
		}
		else
		{
			UE_LOG(LogChronogy, Warning, TEXT("[%s] bSnapshotMaterialParameters=true but no UMeshComponent found."), *GetOwner()->GetName());
		}
	}

	if (bSnapshotParticles)
	{
		DiscoverParticleComponents();
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

		// Clamp the particle clock to the oldest recorded moment. RewindPlaybackTime is unbounded
		// (the rewind ability has no floor), but the buffer only goes back to SnapshotBuffer[0];
		// ApplySnapshotAtTime already clamps transforms to that bottom. Without the same clamp here,
		// the falling clock sweeps a finished burst's age back into [0, DeathAge] and it "respawns"
		// when the buffer is exhausted. Clamping freezes particles with the world at the buffer floor.
		const float ParticleClock = SnapshotBuffer.Num() > 0
			? FMath::Max(RewindPlaybackTime, SnapshotBuffer[0].Timestamp)
			: RewindPlaybackTime;
		if (bSnapshotParticles) ApplyParticlesAtTime(ParticleClock);
		if (Subsystem) Subsystem->OnRewindTick(ParticleClock);
	}
	else
	{
		// Drive particle age every frame (finer than the snapshot interval) so brief bursts play
		// and later reverse smoothly. Owner-attached systems here; detached FX via the subsystem.
		if (bSnapshotParticles) PollParticleActivations(RealNow);
		if (Subsystem) Subsystem->OnForwardTick(RealNow);

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

	if (UAnimInstance* AnimInst = GetAnimInstance())
	{
		IChronogyAnimInterface::Execute_SetIsRewinding(AnimInst, true);
	}

	if (bSnapshotParticles)
	{
		BeginParticleRewind();
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

	// Match the in-rewind particle clock clamp (see TickComponent). Compute the floored stop clock
	// BEFORE EraseFutureSnapshots, which can empty the buffer and lose SnapshotBuffer[0].
	const float ParticleStopClock = (bSnapshotParticles && SnapshotBuffer.Num() > 0)
		? FMath::Max(RewindPlaybackTime, SnapshotBuffer[0].Timestamp)
		: RewindPlaybackTime;

	EraseFutureSnapshots(RewindPlaybackTime);

	if (bSnapshotParticles)
	{
		EndParticleRewind(ParticleStopClock);
	}

	if (UAnimInstance* AnimInst = GetAnimInstance())
	{
		IChronogyAnimInterface::Execute_SetIsRewinding(AnimInst, false);
	}
}

UAnimInstance* UChronogyComponent::GetAnimInstance() const
{
	USkeletalMeshComponent* Mesh = OwnerSkeletalMesh
		? OwnerSkeletalMesh
		: GetOwner()->FindComponentByClass<USkeletalMeshComponent>();

	if (!Mesh)
	{
		if (bSnapshotBonePoses)
		{
			UE_LOG(LogChronogy, Warning, TEXT("[%s] GetAnimInstance: no SkeletalMeshComponent found."), *GetOwner()->GetName());
		}
		return nullptr;
	}

	UAnimInstance* AnimInst = Mesh->GetAnimInstance();
	if (!AnimInst)
	{
		UE_LOG(LogChronogy, Warning, TEXT("[%s] GetAnimInstance: GetAnimInstance() returned null."), *GetOwner()->GetName());
		return nullptr;
	}

	const bool bImplements = AnimInst->GetClass()->ImplementsInterface(UChronogyAnimInterface::StaticClass());
	if (!bImplements)
	{
		UE_LOG(LogChronogy, Warning, TEXT("[%s] GetAnimInstance: %s does not implement IChronogyAnimInterface."),
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

	if (bSnapshotLightProperties && OwnerLightComponent)
	{
		if (LightBuffer.Num() >= MaxSnapshotCount)
			LightBuffer.RemoveAt(0, 1, EAllowShrinking::No);
		FChronogyLightFrame& LF = LightBuffer.AddDefaulted_GetRef();
		LF.Timestamp  = Snap.Timestamp;
		LF.Intensity  = OwnerLightComponent->Intensity;
		LF.LightColor = FLinearColor::FromSRGBColor(OwnerLightComponent->LightColor);
	}

	if (bSnapshotMaterialParameters && OwnerDynMat)
	{
		if (MaterialBuffer.Num() >= MaxSnapshotCount)
			MaterialBuffer.RemoveAt(0, 1, EAllowShrinking::No);
		FChronogyMaterialFrame& MF = MaterialBuffer.AddDefaulted_GetRef();
		MF.Timestamp = Snap.Timestamp;
		MF.Material  = OwnerMeshComponent->GetMaterial(0);
		MF.ScalarValues.SetNumUninitialized(DetectedScalarParams.Num());
		for (int32 i = 0; i < DetectedScalarParams.Num(); i++)
			OwnerDynMat->GetScalarParameterValue(FMaterialParameterInfo(DetectedScalarParams[i]), MF.ScalarValues[i]);
		MF.VectorValues.SetNumUninitialized(DetectedVectorParams.Num());
		for (int32 i = 0; i < DetectedVectorParams.Num(); i++)
			OwnerDynMat->GetVectorParameterValue(FMaterialParameterInfo(DetectedVectorParams[i]), MF.VectorValues[i]);
	}

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

	ApplyLightAtTime(Timestamp);
	ApplyMaterialAtTime(Timestamp);

	if (SnapshotInterface)
	{
		SnapshotInterface->ApplySnapshot(Timestamp);
	}
}

void UChronogyComponent::ApplyLightAtTime(float Timestamp)
{
	if (!bSnapshotLightProperties || !OwnerLightComponent || LightBuffer.Num() == 0) { return; }

	if (Timestamp <= LightBuffer[0].Timestamp)
	{
		OwnerLightComponent->SetIntensity(LightBuffer[0].Intensity);
		OwnerLightComponent->SetLightColor(LightBuffer[0].LightColor);
		return;
	}
	if (Timestamp >= LightBuffer.Last().Timestamp) { return; }

	int32 Lo = 0, Hi = LightBuffer.Num() - 1;
	while (Lo + 1 < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (LightBuffer[Mid].Timestamp <= Timestamp) Lo = Mid;
		else Hi = Mid;
	}

	const FChronogyLightFrame& Older = LightBuffer[Lo];
	const FChronogyLightFrame& Newer = LightBuffer[Hi];
	const float Alpha = FMath::Clamp((Timestamp - Older.Timestamp) / (Newer.Timestamp - Older.Timestamp), 0.f, 1.f);

	OwnerLightComponent->SetIntensity(FMath::Lerp(Older.Intensity, Newer.Intensity, Alpha));
	OwnerLightComponent->SetLightColor(FMath::Lerp(Older.LightColor, Newer.LightColor, Alpha));
}

void UChronogyComponent::ApplyMaterialAtTime(float Timestamp)
{
	if (!bSnapshotMaterialParameters || !OwnerDynMat || MaterialBuffer.Num() == 0) { return; }

	auto RestoreMaterial = [&](const FChronogyMaterialFrame& Frame)
	{
		if (Frame.Material.IsValid() && OwnerMeshComponent->GetMaterial(0) != Frame.Material.Get())
		{
			OwnerMeshComponent->SetMaterial(0, Frame.Material.Get());
		}
	};

	auto ApplyParams = [&](const FChronogyMaterialFrame& Frame)
	{
		if (!OwnerDynMat) { return; }
		for (int32 i = 0; i < DetectedScalarParams.Num(); i++)
			if (Frame.ScalarValues.IsValidIndex(i))
				OwnerDynMat->SetScalarParameterValue(DetectedScalarParams[i], Frame.ScalarValues[i]);
		for (int32 i = 0; i < DetectedVectorParams.Num(); i++)
			if (Frame.VectorValues.IsValidIndex(i))
				OwnerDynMat->SetVectorParameterValue(DetectedVectorParams[i], Frame.VectorValues[i]);
	};

	if (Timestamp <= MaterialBuffer[0].Timestamp) { RestoreMaterial(MaterialBuffer[0]); ApplyParams(MaterialBuffer[0]); return; }
	if (Timestamp >= MaterialBuffer.Last().Timestamp) { return; }

	int32 Lo = 0, Hi = MaterialBuffer.Num() - 1;
	while (Lo + 1 < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (MaterialBuffer[Mid].Timestamp <= Timestamp) Lo = Mid;
		else Hi = Mid;
	}

	const FChronogyMaterialFrame& Older = MaterialBuffer[Lo];
	const FChronogyMaterialFrame& Newer = MaterialBuffer[Hi];
	const float Alpha = FMath::Clamp((Timestamp - Older.Timestamp) / (Newer.Timestamp - Older.Timestamp), 0.f, 1.f);

	RestoreMaterial(Older);

	if (OwnerDynMat)
	{
		for (int32 i = 0; i < DetectedScalarParams.Num(); i++)
			if (Older.ScalarValues.IsValidIndex(i) && Newer.ScalarValues.IsValidIndex(i))
				OwnerDynMat->SetScalarParameterValue(DetectedScalarParams[i], FMath::Lerp(Older.ScalarValues[i], Newer.ScalarValues[i], Alpha));
		for (int32 i = 0; i < DetectedVectorParams.Num(); i++)
			if (Older.VectorValues.IsValidIndex(i) && Newer.VectorValues.IsValidIndex(i))
				OwnerDynMat->SetVectorParameterValue(DetectedVectorParams[i], FMath::Lerp(Older.VectorValues[i], Newer.VectorValues[i], Alpha));
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

	for (int32 i = LightBuffer.Num() - 1; i >= 0; --i)
	{
		if (LightBuffer[i].Timestamp > FromTimestamp)
			LightBuffer.RemoveAt(i, 1, EAllowShrinking::No);
		else
			break;
	}

	for (int32 i = MaterialBuffer.Num() - 1; i >= 0; --i)
	{
		if (MaterialBuffer[i].Timestamp > FromTimestamp)
			MaterialBuffer.RemoveAt(i, 1, EAllowShrinking::No);
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

	UAnimInstance* AnimInst = GetAnimInstance();
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

void UChronogyComponent::DiscoverParticleComponents()
{
	TArray<UNiagaraComponent*> Comps;
	GetOwner()->GetComponents<UNiagaraComponent>(Comps);

	const float Now = GetWorld()->GetRealTimeSeconds();
	for (UNiagaraComponent* C : Comps)
	{
		if (!C) { continue; }

		FChronogyParticleTrack& Track = ParticleTracks.AddDefaulted_GetRef();
		Track.Component  = C;
		Track.Mode       = DefaultParticleRewindMode;
		Track.bWasActive = C->IsActive();
		Track.BirthTime  = Track.bWasActive ? Now : -1.f;
		Track.DeathAge   = -1.f;

		// Put Scrub systems into solo + DesiredAge now and keep them there for their whole life.
		// Their age is driven from the rewind clock every frame — up in forward play, down in
		// rewind — so the mode never switches and they never vanish/freeze on rewind release.
		UChronogySubsystem::ConfigureTrackForRewind(Track, ParticleSeekDelta);
	}

	if (ParticleTracks.Num() == 0)
	{
		UE_LOG(LogChronogy, Warning, TEXT("[%s] bSnapshotParticles=true but no UNiagaraComponent found — particles will not be rewound."), *GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogChronogy, Log, TEXT("[%s] Particle rewind enabled. Tracking %d Niagara system(s)."), *GetOwner()->GetName(), ParticleTracks.Num());
	}
}

void UChronogyComponent::PollParticleActivations(float Now)
{
	for (FChronogyParticleTrack& Track : ParticleTracks)
	{
		UChronogySubsystem::PollTrackActivation(Track, Now);
	}
}

void UChronogyComponent::ApplyParticlesAtTime(float RewindClock)
{
	for (FChronogyParticleTrack& Track : ParticleTracks)
	{
		UChronogySubsystem::ApplyTrackAgeAtTime(Track, RewindClock);
	}
}

void UChronogyComponent::BeginParticleRewind()
{
	// Nothing to switch: Scrub systems are already solo + DesiredAge and stay that way, and Freeze
	// is paused per-frame by ApplyTrackAgeAtTime. Kept as a hook on the rewind-start event.
}

void UChronogyComponent::EndParticleRewind(float FromTimestamp)
{
	// Re-anchor each system's birth to real time at the stopped rewind clock so forward play
	// continues from exactly the age the rewind landed on (RestoreTrack also unpauses Freeze).
	const float Now = GetWorld()->GetRealTimeSeconds();
	for (FChronogyParticleTrack& Track : ParticleTracks)
	{
		UChronogySubsystem::RestoreTrack(Track, FromTimestamp, Now);
	}
}
