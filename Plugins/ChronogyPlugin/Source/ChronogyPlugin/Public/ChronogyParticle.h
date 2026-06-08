// Blake de Armas

#pragma once

#include "CoreMinimal.h"
#include "ChronogyParticle.generated.h"

class UNiagaraComponent;

/**
 * How a Niagara system is reversed when ChronogySubsystem triggers a global rewind.
 * A system's entire appearance is a pure function of its age, so rewind drives that age
 * from the same RewindPlaybackTime clock the rest of the plugin uses.
 */
UENUM(BlueprintType)
enum class EChronogyParticleRewindMode : uint8
{
	// DesiredAge scrubbing — true reverse. Best for short, CPU, deterministic bursts
	// (muzzle flashes, impacts, dust). Niagara cannot step backward, so a backward seek
	// re-simulates from age 0 — cost scales with effect duration. GPU sims cannot seek
	// backward and a non-deterministic emitter (no fixed RandomSeed) will retrace a
	// plausible but not pixel-identical history. Use Freeze for those.
	Scrub,

	// Leave the system ticking normally so it follows the owner as the owner is rewound.
	// Best for motion-driven ribbon / trail emitters (e.g. a sword trail), which reverse
	// naturally by trailing the reversing transform. Scrubbing a motion-trail collapses it.
	FollowTransform,

	// Pause the simulation during rewind and gate it on its spawn time (hidden before it
	// was born). Robust fallback for GPU, looping, long-lived or non-deterministic systems
	// that cannot be scrubbed faithfully or cheaply.
	Freeze
};

/**
 * Per-Niagara-system bookkeeping shared by UChronogyComponent (owner-attached effects) and
 * UChronogySubsystem (detached one-shot world FX). Operated on by the static helpers on
 * UChronogySubsystem so both paths share one implementation of the Niagara calls.
 */
USTRUCT()
struct FChronogyParticleTrack
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UNiagaraComponent> Component;

	// Resolved rewind mechanism for this system.
	EChronogyParticleRewindMode Mode = EChronogyParticleRewindMode::Scrub;

	// Absolute real-time (GetRealTimeSeconds) of this system's most recent activation; < 0 until it
	// has ever been born. Its appearance is a pure function of (clock - BirthTime) — driven forward
	// in normal play and backward during rewind — anchored to this birth time so a finished one-shot
	// stays lined up with the rewind buffer (reappears/despawns at the right point in the timeline,
	// not the instant rewind starts). The system stays solo+DesiredAge for life (no mode switching).
	float BirthTime = -1.0f;

	// Age (seconds since birth) at which the system last went inactive (finished); < 0 while alive.
	// During rewind the system is hidden when the rewind age is past DeathAge (it is over) or below 0
	// (not yet born), and only scrubbed inside [0, DeathAge]. Gates despawn correctly and bounds the
	// backward re-sim cost to the effect's real lifetime.
	float DeathAge = -1.0f;

	// Edge-detection of activation: inactive -> active = birth, active -> inactive = death.
	bool bWasActive = false;

	// True once solo + DesiredAge has been configured on the component (Scrub only, done once).
	bool bScrubReady = false;
};
