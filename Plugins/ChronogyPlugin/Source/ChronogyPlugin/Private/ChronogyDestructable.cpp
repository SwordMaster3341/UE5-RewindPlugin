// S-G-D

#include "ChronogyDestructable.h"
#include "ChronogyComponent.h"
#include "ChronogySubsystem.h"
#include "ChronogyLogs.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

AChronogyDestructable::AChronogyDestructable()
{
	PrimaryActorTick.bCanEverTick = false;

	IntactMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IntactMesh"));
	SetRootComponent(IntactMesh);
	IntactMesh->SetMobility(EComponentMobility::Movable);
	IntactMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	ChronogyComponent = CreateDefaultSubobject<UChronogyComponent>(TEXT("ChronogyComponent"));
}

void AChronogyDestructable::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Show the intact mesh in-editor so designers can place and align the actor.
	if (IntactMesh && IntactStaticMesh)
	{
		IntactMesh->SetStaticMesh(IntactStaticMesh);
	}
}

void AChronogyDestructable::BeginPlay()
{
	Super::BeginPlay();

	CreateFragmentComponents();

	// Remember whether the intact cube is a physics body so we can re-enable it after reassembly.
	bIntactSimulatesPhysics = IntactMesh && IntactMesh->BodyInstance.bSimulatePhysics;

	Subsystem = GetWorld()->GetGameInstance()->GetSubsystem<UChronogySubsystem>();
	if (Subsystem)
	{
		// Fragments are non-root simulating bodies, so the sibling UChronogyComponent (which only
		// pauses the root) won't stop them — gate their physics off ourselves when rewind begins.
		Subsystem->OnRewindStarted.AddUniqueDynamic(this, &AChronogyDestructable::OnRewindStarted);
	}
	else
	{
		UE_LOG(LogChronogy, Error, TEXT("[%s] ChronogySubsystem not found — Destructable will not rewind."), *GetName());
	}
}

void AChronogyDestructable::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Subsystem)
	{
		Subsystem->OnRewindStarted.RemoveDynamic(this, &AChronogyDestructable::OnRewindStarted);
	}

	Super::EndPlay(EndPlayReason);
}

void AChronogyDestructable::CreateFragmentComponents()
{
	FragmentComponents.Reserve(FragmentMeshes.Num());
	RestRelativeTransforms.Reserve(FragmentMeshes.Num());

	for (int32 i = 0; i < FragmentMeshes.Num(); ++i)
	{
		if (!FragmentMeshes[i])
		{
			UE_LOG(LogChronogy, Warning, TEXT("[%s] FragmentMeshes[%d] is null — skipped."), *GetName(), i);
			continue;
		}

		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(FragmentMeshes[i]);
		Comp->RegisterComponent();
		// Exported fragments share the source pivot, so identity-relative reassembles the whole.
		Comp->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		Comp->SetRelativeTransform(FTransform::Identity);
		Comp->SetSimulatePhysics(false);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetHiddenInGame(true);

		if (FragmentMassKg > 0.0f)
		{
			Comp->SetMassOverrideInKg(NAME_None, FragmentMassKg, true);
		}

		FragmentComponents.Add(Comp);
		// Store actor-relative manually (not via the attachment API): physics simulation detaches the
		// component from the parent transform chain, so GetRelativeTransform/SetRelativeTransform stop
		// tracking the actor. = identity here since the fragment sits at the actor origin.
		RestRelativeTransforms.Add(Comp->GetComponentTransform().GetRelativeTransform(GetActorTransform()));
	}

	UE_LOG(LogChronogy, Log, TEXT("[%s] Destructable created %d fragment components."), *GetName(), FragmentComponents.Num());
}

void AChronogyDestructable::TriggerDestruction()
{
	TriggerDestructionWithImpulse(GetActorLocation(), DefaultImpulseStrength);
}

void AChronogyDestructable::TriggerDestructionWithImpulse(FVector Origin, float Strength)
{
	if (bFractured)
	{
		return;
	}
	if (Subsystem && Subsystem->bIsRewinding)
	{
		// A rewind in progress owns the fragment transforms; don't fracture mid-rewind.
		return;
	}

	// Carry the cube's motion into the fragments so a falling/moving cube doesn't shatter in mid-air
	// from a dead stop.
	const bool    bRootWasSimulating = IntactMesh && IntactMesh->IsSimulatingPhysics();
	const FVector InheritedLinVel    = bRootWasSimulating ? IntactMesh->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	const FVector InheritedAngVel    = bRootWasSimulating ? IntactMesh->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;

	bFractured        = true;
	FractureTimestamp = GetWorld()->GetRealTimeSeconds();

	// Reassemble the fragments at the cube's CURRENT pose before breaking. While intact they are
	// hidden, and once a prior shatter detached them via physics they no longer follow the moving
	// cube — so without this they would reveal at a stale location (e.g. the post-rewind/spawn point).
	const FTransform CurrentActorTM = GetActorTransform();
	for (int32 i = 0; i < FragmentComponents.Num(); ++i)
	{
		if (FragmentComponents[i] && RestRelativeTransforms.IsValidIndex(i))
		{
			FragmentComponents[i]->SetWorldTransform(RestRelativeTransforms[i] * CurrentActorTM, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	ShowFragments(true);
	// Freeze the now-hidden root: it has no collision while hidden, so left simulating it would
	// free-fall forever, corrupting the recorded root path (and the fragments' relative frame).
	if (IntactMesh)
	{
		IntactMesh->SetSimulatePhysics(false);
	}
	SetFragmentsSimulating(true);

	for (UStaticMeshComponent* Comp : FragmentComponents)
	{
		if (Comp)
		{
			Comp->SetPhysicsLinearVelocity(InheritedLinVel);
			Comp->SetPhysicsAngularVelocityInRadians(InheritedAngVel);
			Comp->AddRadialImpulse(Origin, DefaultImpulseRadius, Strength, ERadialImpulseFalloff::RIF_Linear, true);
		}
	}

	// Record the transition (and a first transform frame) immediately so there is data before the
	// next snapshot interval.
	CaptureSnapshot();

	UE_LOG(LogChronogy, Log, TEXT("[%s] Destructable fractured at T=%.3f (%d fragments)."), *GetName(), FractureTimestamp, FragmentComponents.Num());
}

void AChronogyDestructable::CaptureSnapshot()
{
	const float Now = GetWorld()->GetRealTimeSeconds();

	// State buffer is a step function — only append when the state actually changes.
	if (StateBuffer.Num() == 0 || StateBuffer.Last().bFractured != bFractured)
	{
		FChronogyDestructableStateFrame& State = StateBuffer.AddDefaulted_GetRef();
		State.Timestamp  = Now;
		State.bFractured = bFractured;
	}

	// Transforms are only meaningful (and only change) while fractured.
	if (!bFractured)
	{
		return;
	}

	if (TransformBuffer.Num() >= MaxFrames)
	{
		TransformBuffer.RemoveAt(0, 1, EAllowShrinking::No);
	}

	FChronogyDestructableTransformFrame& Frame = TransformBuffer.AddDefaulted_GetRef();
	Frame.Timestamp = Now;
	Frame.FragmentTransforms.SetNumUninitialized(FragmentComponents.Num());
	const FTransform ActorTM = GetActorTransform();
	for (int32 i = 0; i < FragmentComponents.Num(); ++i)
	{
		// Actor-relative, computed from world (see FChronogyDestructableTransformFrame). Manual rather than
		// GetRelativeTransform() because physics simulation detaches the fragment from the parent chain.
		Frame.FragmentTransforms[i] = FragmentComponents[i]
			? FragmentComponents[i]->GetComponentTransform().GetRelativeTransform(ActorTM)
			: FTransform::Identity;
	}
}

bool AChronogyDestructable::IsFracturedAtTime(float Timestamp) const
{
	// Buffer is sorted oldest->newest; the last frame at or before Timestamp wins. Tiny buffer.
	for (int32 i = StateBuffer.Num() - 1; i >= 0; --i)
	{
		if (StateBuffer[i].Timestamp <= Timestamp)
		{
			return StateBuffer[i].bFractured;
		}
	}
	return false;
}

void AChronogyDestructable::ApplySnapshot(float Timestamp)
{
	if (!IsFracturedAtTime(Timestamp))
	{
		// Rewound to before the break: reassemble.
		RestoreRestState();
		return;
	}

	ApplyFracturedTransforms(Timestamp);
}

void AChronogyDestructable::ApplyFracturedTransforms(float Timestamp)
{
	if (TransformBuffer.Num() == 0)
	{
		return;
	}

	ShowFragments(true);

	// Compose stored actor-relative transforms with the current actor transform to get world poses.
	// SetWorldTransform (not SetRelativeTransform) because physics may have detached the fragments.
	const FTransform ActorTM = GetActorTransform();

	auto ApplyFrame = [this, &ActorTM](const FChronogyDestructableTransformFrame& Frame)
	{
		for (int32 i = 0; i < FragmentComponents.Num(); ++i)
		{
			if (FragmentComponents[i] && Frame.FragmentTransforms.IsValidIndex(i))
			{
				FragmentComponents[i]->SetWorldTransform(Frame.FragmentTransforms[i] * ActorTM, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	};

	// Clamp to the ends of the recorded window.
	if (Timestamp <= TransformBuffer[0].Timestamp)
	{
		ApplyFrame(TransformBuffer[0]);
		return;
	}
	if (Timestamp >= TransformBuffer.Last().Timestamp)
	{
		ApplyFrame(TransformBuffer.Last());
		return;
	}

	// Binary search for the two frames bracketing Timestamp (same pattern as UChronogyComponent).
	int32 Lo = 0;
	int32 Hi = TransformBuffer.Num() - 1;
	while (Lo + 1 < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (TransformBuffer[Mid].Timestamp <= Timestamp)
			Lo = Mid;
		else
			Hi = Mid;
	}

	const FChronogyDestructableTransformFrame& Older = TransformBuffer[Lo];
	const FChronogyDestructableTransformFrame& Newer = TransformBuffer[Hi];
	const float Range = Newer.Timestamp - Older.Timestamp;
	const float Alpha = FMath::Clamp((Timestamp - Older.Timestamp) / Range, 0.f, 1.f);

	for (int32 i = 0; i < FragmentComponents.Num(); ++i)
	{
		if (!FragmentComponents[i] || !Older.FragmentTransforms.IsValidIndex(i) || !Newer.FragmentTransforms.IsValidIndex(i))
		{
			continue;
		}

		const FVector BlendedLocation = FMath::Lerp(Older.FragmentTransforms[i].GetLocation(), Newer.FragmentTransforms[i].GetLocation(), Alpha);
		const FQuat   BlendedRotation = FQuat::Slerp(Older.FragmentTransforms[i].GetRotation(), Newer.FragmentTransforms[i].GetRotation(), Alpha);
		const FVector Scale           = Older.FragmentTransforms[i].GetScale3D();

		const FTransform BlendedRelative(BlendedRotation, BlendedLocation, Scale);
		FragmentComponents[i]->SetWorldTransform(BlendedRelative * ActorTM, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AChronogyDestructable::EraseFutureSnapshots(float FromTimestamp)
{
	for (int32 i = TransformBuffer.Num() - 1; i >= 0; --i)
	{
		if (TransformBuffer[i].Timestamp > FromTimestamp)
			TransformBuffer.RemoveAt(i, 1, EAllowShrinking::No);
		else
			break;
	}

	for (int32 i = StateBuffer.Num() - 1; i >= 0; --i)
	{
		if (StateBuffer[i].Timestamp > FromTimestamp)
			StateBuffer.RemoveAt(i, 1, EAllowShrinking::No);
		else
			break;
	}

	// Recompute live state from what remains. EraseFutureSnapshots runs on rewind completion after
	// the buffers are trimmed, so it is the authoritative point to settle physics/visibility.
	bFractured = (StateBuffer.Num() > 0) && StateBuffer.Last().bFractured;

	if (bFractured)
	{
		FractureTimestamp = 0.0f;
		for (int32 i = StateBuffer.Num() - 1; i >= 0; --i)
		{
			if (StateBuffer[i].bFractured)
			{
				FractureTimestamp = StateBuffer[i].Timestamp;
				break;
			}
		}

		// Resume the simulation from where the rewind stopped. Root stays frozen while shattered.
		if (IntactMesh)
		{
			IntactMesh->SetSimulatePhysics(false);
		}
		ShowFragments(true);
		SetFragmentsSimulating(true);
		RestoreFragmentVelocities();
	}
	else
	{
		FractureTimestamp = 0.0f;
		RestoreRestState();

		// Reassembled — let the intact cube be a physics body again if the designer made it one.
		// Guarded so we don't clobber the velocity the UChronogyComponent just restored on the root.
		if (bIntactSimulatesPhysics && IntactMesh && !IntactMesh->IsSimulatingPhysics())
		{
			IntactMesh->SetSimulatePhysics(true);
		}
	}
}

void AChronogyDestructable::OnRewindStarted()
{
	// Freeze the rigid bodies so per-tick ApplySnapshot transforms aren't fought by the solver.
	SetFragmentsSimulating(false);
}

void AChronogyDestructable::SetFragmentsSimulating(bool bSimulate)
{
	for (UStaticMeshComponent* Comp : FragmentComponents)
	{
		if (Comp)
		{
			Comp->SetSimulatePhysics(bSimulate);
		}
	}
}

void AChronogyDestructable::ShowFragments(bool bShow)
{
	if (bFragmentsVisible == bShow)
	{
		return;
	}
	bFragmentsVisible = bShow;

	if (IntactMesh)
	{
		IntactMesh->SetHiddenInGame(bShow);
		IntactMesh->SetCollisionEnabled(bShow ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	}

	for (UStaticMeshComponent* Comp : FragmentComponents)
	{
		if (Comp)
		{
			Comp->SetHiddenInGame(!bShow);
			Comp->SetCollisionEnabled(bShow ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		}
	}
}

void AChronogyDestructable::RestoreRestState()
{
	// Compose actor-relative rest poses with the current actor transform; SetWorldTransform because
	// physics simulation may have detached the fragments from the parent transform chain.
	const FTransform ActorTM = GetActorTransform();
	for (int32 i = 0; i < FragmentComponents.Num(); ++i)
	{
		if (FragmentComponents[i] && RestRelativeTransforms.IsValidIndex(i))
		{
			FragmentComponents[i]->SetSimulatePhysics(false);
			FragmentComponents[i]->SetWorldTransform(RestRelativeTransforms[i] * ActorTM, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	ShowFragments(false);
}

void AChronogyDestructable::RestoreFragmentVelocities()
{
	if (TransformBuffer.Num() < 2)
	{
		return;
	}

	const FChronogyDestructableTransformFrame& Newer = TransformBuffer.Last();
	const FChronogyDestructableTransformFrame& Older = TransformBuffer[TransformBuffer.Num() - 2];
	const float Dt = Newer.Timestamp - Older.Timestamp;
	if (Dt <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Stored transforms are relative to the actor root; compose with the current actor transform to
	// recover world-space velocity (correct even if the cube was moved/rotated before fracture).
	const FTransform ActorTM = GetActorTransform();

	for (int32 i = 0; i < FragmentComponents.Num(); ++i)
	{
		if (!FragmentComponents[i] || !Newer.FragmentTransforms.IsValidIndex(i) || !Older.FragmentTransforms.IsValidIndex(i))
		{
			continue;
		}

		const FVector WorldNewer = ActorTM.TransformPosition(Newer.FragmentTransforms[i].GetLocation());
		const FVector WorldOlder = ActorTM.TransformPosition(Older.FragmentTransforms[i].GetLocation());
		FragmentComponents[i]->SetPhysicsLinearVelocity((WorldNewer - WorldOlder) / Dt);

		FQuat DeltaQuat = Newer.FragmentTransforms[i].GetRotation() * Older.FragmentTransforms[i].GetRotation().Inverse();
		DeltaQuat.Normalize();
		FVector Axis;
		float   Angle;
		DeltaQuat.ToAxisAndAngle(Axis, Angle);
		if (Angle > PI)
		{
			Angle -= 2.0f * PI;
		}
		const FVector WorldAxis = ActorTM.GetRotation().RotateVector(Axis);
		FragmentComponents[i]->SetPhysicsAngularVelocityInRadians(WorldAxis * (Angle / Dt));
	}
}
