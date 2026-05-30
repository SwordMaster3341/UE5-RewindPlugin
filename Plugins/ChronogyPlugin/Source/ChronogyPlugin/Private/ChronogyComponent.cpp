// S-G-D

//Chronogy Includes
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


/*
  This is the main component that handles rewinding for all actors.

  A component was chosen as it is easily implementable and inheritable in blueprints, and can be added to any actor without 
  modifying the actor's class. 

  It also allows for per-actor customization of what is recorded and rewound (e.g. whether to record bone poses or light properties)
  saving on memory.

*/


UChronogyComponent::UChronogyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UChronogyComponent::BeginPlay()
{
	Super::BeginPlay();

	SnapshotInterface = Cast<IChronogySnapshotInterface>(GetOwner());

	OwnerRootComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());

	if (!OwnerRootComponent)
	{
		UE_LOG(LogChronogy, Warning, TEXT("[%s] Root component is not a UPrimitiveComponent — physics will not be paused during rewind. Make the Static Mesh the root component in the Blueprint Components panel."), *GetOwner()->GetName());
	}

	// Blueprint toggleable variable
	if (bSnapshotMovementVelocityAndMode)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			OwnerMovementComponent = Cast<UCharacterMovementComponent>(Character->GetMovementComponent());
		}
	}

	// Blueprint toggleable variable
	if (bSnapshotBonePoses)
	{
		OwnerSkeletalMesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
		if (!OwnerSkeletalMesh)
		{
			UE_LOG(LogChronogy, Warning, TEXT("[%s] bSnapshotBonePoses=true but no USkeletalMeshComponent found — bone poses will not be recorded."), *GetOwner()->GetName());
		}
	}

	// Blueprint toggleable variable
	if (bSnapshotLightProperties)
	{
		OwnerLightComponent = GetOwner()->FindComponentByClass<ULightComponent>();
		if (OwnerLightComponent)
		{
			UE_LOG(LogChronogy, Log, TEXT("[%s] Light property snapshotting enabled."), *GetOwner()->GetName());
		}
		else
		{
			UE_LOG(LogChronogy, Warning, TEXT("[%s] bSnapshotLightProperties=true but no ULightComponent found — light will not be rewound."), *GetOwner()->GetName());
		}
	}

	// Blueprint toggleable variable
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

	/*
		This section calculates the minimum size of each snapshot based on what is recorded.

		Please note that every single rewindable has a hard cap of 2mb per snapshot. 

		You should almost never hit this cap, but if you do, the component will log a warning 
		and skip recording that snapshot to avoid crashing.

		The best thing you can do is to reduce the memory footprint of the snapshot or potentially increase the MaxMemoryBytes variable
		, but be aware that increasing memory usage can lead to performance issues and OOM crashes if taken too far.
	
	*/
	 
	int32 BytesPerSnapshot = sizeof(FChronogySnapshot);
	if (bSnapshotLightProperties && OwnerLightComponent)
	{
		BytesPerSnapshot += sizeof(FChronogyLightFrame);
	}
	if (bSnapshotMaterialParameters && OwnerDynMat)
	{
		BytesPerSnapshot += sizeof(FChronogyMaterialFrame)
			+ DetectedScalarParams.Num() * static_cast<int32>(sizeof(float))
			+ DetectedVectorParams.Num() * static_cast<int32>(sizeof(FLinearColor));
	}

	int32 BonePoseBytes = 0;
	if (bSnapshotBonePoses && OwnerSkeletalMesh)
	{
		BonePoseBytes = sizeof(FChronogyPoseSnapshot)
			+ OwnerSkeletalMesh->GetNumBones() * static_cast<int32>(sizeof(FTransform));
		BytesPerSnapshot += BonePoseBytes / FMath::Max(1, BoneSnapshotFrameInterval);
	}

	MaxSnapshotCount = FMath::Min(
		FMath::CeilToInt(MaxRewindSeconds / SnapShotFrequencySeconds),
		FMath::Max(1, MaxMemoryBytes / BytesPerSnapshot));

	SnapshotBuffer.Reserve(MaxSnapshotCount);
	if (bSnapshotLightProperties && OwnerLightComponent) { LightBuffer.Reserve(MaxSnapshotCount); }
	if (bSnapshotMaterialParameters && OwnerDynMat)      { MaterialBuffer.Reserve(MaxSnapshotCount); }

	if (bSnapshotBonePoses && OwnerSkeletalMesh)
	{
		MaxBonePoseCount = FMath::CeilToInt(
			MaxRewindSeconds / (SnapShotFrequencySeconds * FMath::Max(1, BoneSnapshotFrameInterval)));
		// Keep the pose buffer within its own share of the budget as well.
		MaxBonePoseCount = FMath::Min(MaxBonePoseCount, FMath::Max(1, MaxMemoryBytes / FMath::Max(1, BonePoseBytes)));
		BonePoseBuffer.Reserve(MaxBonePoseCount);
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


// Unregister from subsystem to avoid dangling references and ensure clean end-play behavior.
// Standard UE5 garbage collection
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


/*
	This component ticks every frame and either records snapshots or applies them depending on whether or not we are rewinding.
	
	The main logic behind the tracking.

*/

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


		/*
		The particle clock is a highly important part of the rewind, as it is the only way particles can "rewind".

		By default Niagara has no way of rewinding particle simulations, but by driving the particle age with the rewind clock
		we can achieve a similar effect.

		Spawning/despawning is already handled by the subsystem for all actors.

		Edit:
		Added a clamp here on the rewind secconds so that way when a particle system hits 0 on its lifetime, it will not be culled

		*/
		const float ParticleClock = SnapshotBuffer.Num() > 0
			? FMath::Max(RewindPlaybackTime, SnapshotBuffer[0].Timestamp)
			: RewindPlaybackTime;
		if (bSnapshotParticles) ApplyParticlesAtTime(ParticleClock);
		if (Subsystem) Subsystem->OnRewindTick(ParticleClock);
	}
	else
	{
		// This fixes a bug with rewinding that upon a player entering the game and then immidiately rewinding,
		// the particles do not despawn
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

	// Debug tool showing lines on screen for your position history. 
	// Yellow lines are the rewind path, green lines are the forward path. 
	// Spheres show current snapshot target.
#if ENABLE_DRAW_DEBUG
	if (CVarChronogyDebugDraw.GetValueOnGameThread() && SnapshotBuffer.Num() > 0)
	{
		UWorld* World = GetWorld();
		if (bIsRewinding)
		{
			for (int32 i = 1; i < static_cast<int32>(SnapshotBuffer.Num()); i++)
			{
				DrawDebugLine(World, SnapshotBuffer[i - 1].Location, SnapshotBuffer[i].Location, FColor::Yellow, false, -1.f, 0, 0.5f);
			}
			DrawDebugSphere(World, GetOwner()->GetActorLocation(), 16.f, 8, FColor::Red, false, -1.f, 0, 2.f);
		}
		else
		{
			for (int32 i = 1; i < static_cast<int32>(SnapshotBuffer.Num()); i++)
			{
				DrawDebugLine(World, SnapshotBuffer[i - 1].Location, SnapshotBuffer[i].Location, FColor::Green, false, -1.f, 0, 0.5f);
			}
			DrawDebugSphere(World, SnapshotBuffer.Last().Location, 8.f, 6, FColor::Green, false, -1.f, 0, 1.f);
		}
	}
#endif
}

/*
Simple broadcast to scream that rewind has started or ended, which lets the component pause physics and trigger animation events.
*/
void UChronogyComponent::OnRewindStarted()
{
	bIsRewinding = true;
	RewindPlaybackTime = GetWorld()->GetRealTimeSeconds();

	UE_LOG(LogChronogy, Log, TEXT("[%s] Rewind started. Snapshots: %d/%d  BonePoses: %d/%d"),
		*GetOwner()->GetName(), static_cast<int32>(SnapshotBuffer.Num()), MaxSnapshotCount, static_cast<int32>(BonePoseBuffer.Num()), MaxBonePoseCount);

	if (OwnerRootComponent && OwnerRootComponent->BodyInstance.bSimulatePhysics)
	{
		bPausedPhysics = true;
		OwnerRootComponent->SetSimulatePhysics(false);
	}

	if (UAnimInstance* AnimInst = GetAnimInstance())
	{
		IChronogyAnimInterface::Execute_SetIsRewinding(AnimInst, true);
	}
}

/*
Simple broadcast to scream that rewind has started or ended, which lets the component pause physics and trigger animation events.
*/
void UChronogyComponent::OnRewindCompleted()
{
	bIsRewinding = false;

	if (bPausedPhysics && OwnerRootComponent)
	{
		bPausedPhysics = false;
		OwnerRootComponent->SetSimulatePhysics(true);
		OwnerRootComponent->RecreatePhysicsState();

		// Restore velocity from the snapshot closest to where we stopped
		for (int32 i = static_cast<int32>(SnapshotBuffer.Num()) - 1; i >= 0; --i)
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
		*GetOwner()->GetName(), RewindPlaybackTime, static_cast<int32>(SnapshotBuffer.Num()));

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
		? OwnerSkeletalMesh.Get()
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
	// Drop oldest snapshot to make room if the buffer is full (O(1) on a ring — just advances the front)
	if (static_cast<int32>(SnapshotBuffer.Num()) >= MaxSnapshotCount)
	{
		SnapshotBuffer.PopFront();
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
		if (static_cast<int32>(LightBuffer.Num()) >= MaxSnapshotCount)
			LightBuffer.PopFront();
		FChronogyLightFrame& LF = LightBuffer.Emplace_GetRef();
		LF.Timestamp  = Snap.Timestamp;
		LF.Intensity  = OwnerLightComponent->Intensity;
		LF.LightColor = FLinearColor::FromSRGBColor(OwnerLightComponent->LightColor);
	}

	if (bSnapshotMaterialParameters && OwnerDynMat)
	{
		if (static_cast<int32>(MaterialBuffer.Num()) >= MaxSnapshotCount)
			MaterialBuffer.PopFront();
		FChronogyMaterialFrame& MF = MaterialBuffer.Emplace_GetRef();
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

			if (static_cast<int32>(BonePoseBuffer.Num()) >= MaxBonePoseCount)
				BonePoseBuffer.PopFront();

			FChronogyPoseSnapshot& PoseSnap = BonePoseBuffer.Emplace_GetRef();
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
	int32 Hi = static_cast<int32>(SnapshotBuffer.Num()) - 1;
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

	// Guard a degenerate bracket (two frames captured the same instant => Range 0): without this the
	// divide yields NaN, FMath::Clamp does not sanitize NaN, and the Lerp/Slerp produce garbage.
	const float Range = Newer.Timestamp - Older.Timestamp;
	const float Alpha = Range > KINDA_SMALL_NUMBER
		? FMath::Clamp((Timestamp - Older.Timestamp) / Range, 0.f, 1.f)
		: 1.f;

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

	int32 Lo = 0, Hi = static_cast<int32>(LightBuffer.Num()) - 1;
	while (Lo + 1 < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (LightBuffer[Mid].Timestamp <= Timestamp) Lo = Mid;
		else Hi = Mid;
	}

	const FChronogyLightFrame& Older = LightBuffer[Lo];
	const FChronogyLightFrame& Newer = LightBuffer[Hi];
	const float Range = Newer.Timestamp - Older.Timestamp;
	const float Alpha = Range > KINDA_SMALL_NUMBER
		? FMath::Clamp((Timestamp - Older.Timestamp) / Range, 0.f, 1.f)
		: 1.f;

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

	int32 Lo = 0, Hi = static_cast<int32>(MaterialBuffer.Num()) - 1;
	while (Lo + 1 < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (MaterialBuffer[Mid].Timestamp <= Timestamp) Lo = Mid;
		else Hi = Mid;
	}

	const FChronogyMaterialFrame& Older = MaterialBuffer[Lo];
	const FChronogyMaterialFrame& Newer = MaterialBuffer[Hi];
	const float Range = Newer.Timestamp - Older.Timestamp;
	const float Alpha = Range > KINDA_SMALL_NUMBER
		? FMath::Clamp((Timestamp - Older.Timestamp) / Range, 0.f, 1.f)
		: 1.f;

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
	// Buffers are sorted oldest→newest, so trim from the back (O(1) per pop on a ring)
	while (SnapshotBuffer.Num() > 0 && SnapshotBuffer.Last().Timestamp > FromTimestamp)
		SnapshotBuffer.Pop();

	while (BonePoseBuffer.Num() > 0 && BonePoseBuffer.Last().Timestamp > FromTimestamp)
		BonePoseBuffer.Pop();

	while (LightBuffer.Num() > 0 && LightBuffer.Last().Timestamp > FromTimestamp)
		LightBuffer.Pop();

	while (MaterialBuffer.Num() > 0 && MaterialBuffer.Last().Timestamp > FromTimestamp)
		MaterialBuffer.Pop();

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
	int32 Lo = 0, Hi = static_cast<int32>(BonePoseBuffer.Num()) - 1;
	while (Lo + 1 < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (BonePoseBuffer[Mid].Timestamp <= RewindPlaybackTime) Lo = Mid;
		else Hi = Mid;
	}

	const FChronogyPoseSnapshot& Older = BonePoseBuffer[Lo];
	const FChronogyPoseSnapshot& Newer = BonePoseBuffer[Hi];

	const float Range = Newer.Timestamp - Older.Timestamp;
	const float Alpha = Range > KINDA_SMALL_NUMBER
		? FMath::Clamp((RewindPlaybackTime - Older.Timestamp) / Range, 0.f, 1.f)
		: 1.f;

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
