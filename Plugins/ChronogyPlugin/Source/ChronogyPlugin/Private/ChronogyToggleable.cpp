// Blake de Armas

#include "ChronogyToggleable.h"
#include "ChronogyComponent.h"
#include "ChronogySubsystem.h"
#include "ChronogyLogs.h"
#include "Components/StaticMeshComponent.h"

AChronogyToggleable::AChronogyToggleable()
{
	PrimaryActorTick.bCanEverTick = false;

	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	SetRootComponent(StaticMesh);

	ChronogyComponent = CreateDefaultSubobject<UChronogyComponent>(TEXT("ChronogyComponent"));
}

void AChronogyToggleable::BeginPlay()
{
	Super::BeginPlay();
	FrameBuffer.Reserve(MaxFrames);
}

void AChronogyToggleable::SetToggleState(bool bNewState)
{
	if (bIsOn == bNewState) { return; }
	bIsOn = bNewState;
	UE_LOG(LogChronogy, Log, TEXT("[%s] Toggleable -> %s"), *GetName(), bIsOn ? TEXT("ON") : TEXT("OFF"));
	OnToggleableStateChanged.Broadcast(bIsOn);
}

// ---- IChronogySnapshotInterface ----

void AChronogyToggleable::CaptureSnapshot()
{
	if (static_cast<int32>(FrameBuffer.Num()) >= MaxFrames)
	{
		FrameBuffer.PopFront();
	}

	const UChronogySubsystem* Subsystem = GetWorld()->GetSubsystem<UChronogySubsystem>();

	FChronogyToggleableFrame& Frame = FrameBuffer.Emplace_GetRef();
	Frame.Timestamp = Subsystem ? Subsystem->GetTimelineSeconds() : GetWorld()->GetRealTimeSeconds();
	Frame.bIsOn     = bIsOn;
}

void AChronogyToggleable::ApplySnapshot(float Timestamp)
{
	if (FrameBuffer.Num() == 0) { return; }

	// Clamp to oldest recorded state
	if (Timestamp <= FrameBuffer[0].Timestamp)
	{
		const bool bRestored = FrameBuffer[0].bIsOn;
		if (bIsOn != bRestored)
		{
			bIsOn = bRestored;
			UE_LOG(LogChronogy, Log, TEXT("[%s] Toggleable rewind -> %s (clamped to oldest, T=%.3f)"), *GetName(), bIsOn ? TEXT("ON") : TEXT("OFF"), Timestamp);
			OnToggleableStateChanged.Broadcast(bIsOn);
		}
		return;
	}

	// Binary search for the latest frame at or before Timestamp
	int32 Lo = 0;
	int32 Hi = static_cast<int32>(FrameBuffer.Num()) - 1;
	while (Lo + 1 < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (FrameBuffer[Mid].Timestamp <= Timestamp)
			Lo = Mid;
		else
			Hi = Mid;
	}

	const bool bRestored = FrameBuffer[Lo].bIsOn;
	if (bIsOn != bRestored)
	{
		bIsOn = bRestored;
		UE_LOG(LogChronogy, Log, TEXT("[%s] Toggleable rewind -> %s (T=%.3f, frame %d/%d)"), *GetName(), bIsOn ? TEXT("ON") : TEXT("OFF"), Timestamp, Lo, FrameBuffer.Num());
		OnToggleableStateChanged.Broadcast(bIsOn);
	}
}

void AChronogyToggleable::EraseFutureSnapshots(float FromTimestamp)
{
	while (FrameBuffer.Num() > 0 && FrameBuffer.Last().Timestamp > FromTimestamp)
		FrameBuffer.Pop();
}
