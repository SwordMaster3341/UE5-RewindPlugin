# Documentation

The ChronogyPlugin is a standalone time rewind system for Unreal Engine 5, built on top of the Gameplay Ability System (GAS). It records very lightweight snapshots for N number of actors at a fixed interval and replays them in reverse when a global rewind is triggered.

This document serves to explain how the pieces fit together, what every important file does, the full component property reference, and the common setup workflows.

## Design Philosophy

This is not my first attempt at making a rewind system that is highly optimized, but it is the most successful. Throughout my experience working with this mechanic there were always three main issues:

Extensibility: accessing every object in the engine plus user implementations

Optimization: ensuring when rewinding it isn't a memory hog

Decoupling: ensuring each component doesn't interface directly with the actor creating a "God Class"

Hence, my main research and philosophy going into this was completely surrounding those three main principles. 

## Important Files:

This plugin is mainly built around 3 core pieces and a set of optional extensions.

### ChronogySubsystem

The ChronogySubsystem is a **GameInstanceSubsystem** that owns the global rewind state. It doesn't do any recording itself, it just broadcasts when rewind is started or completed while simultaneously exposing the controls for StartGlobalRewind and StopGlobalRewind. 

Essentially, if something with time is happening this subsystem is the main broadcast.

**Quick Note:**
The subsystem cannot tick itself, so the registered components drive it by calling **OnRewindTick**/**OnForwardTick** on the subsystem each frame. This is intentional and keeps the rewind clock aligned with the components that actually own its data.

### Chronogy Component

The per actor component that tracks each actor's individual states. You can add one to any actor you want to be rewindable. It records snapshots while time runs forward and restores them while the subsystem is rewinding. Each component registers itself with the subsystem and listens for its events.

**While time runs forward:**
The component records an **FChronogySnapshot** every **SnapShotFrequencySeconds** (30 Hz by default) into a **TRingBuffer**.

The ring buffer is the main key to the plugin's memory management, as when it is full recording a new frame just advances the front pointer (running in O(1)) and overwrites the oldest frame, so a component never grows past its budget.

**While the subsystem is rewinding:**
It walks the buffer backwards, applying the snapshot at the current rewind timestamp to the owner. Currently, it clears the TRingBuffer as the player passes each FChronogySnapshot. Should one wish to include a television style rewind (where you can choose the time you come back to) this would have to be modified.

Please note, physics are paused on the owner's primitive root during rewind. This was done because the position, rotation, and velocity from the snapshots replay the physics without fighting the engine. Upon completion of a rewind, the physics are re-enabled so the actor resumes motion clearly instead of ragdolling.

Each optional feature has its own parallel ring buffer (**LightBuffer**, **MaterialBuffer**,**BonePoseBuffer**) so you only need to pay for what you turn on. Snapshots are timestamped with **GetRealTimeSeconds()** rather than game time, so the recording stays consistent even when time dilation (such as slow motion/ time stop) is active.

**Lifecycle:**

**BeginPlay**: 
caches owner component references (root, primitive movement, skeletal mesh, light, mesh/material) and computes buffer sizes from a memory budget, resolves the optional IChronogySnapshotInterface on the owner, and registers the subsystem.

The memory cap is enforced here. There are 3 separate caps to prevent memory related crashes: a hard memory cap, the size of the TRingBuffer, and a limited timeframe where recording is allowed. 

**End Play:**
Unregisters from the subsystem and unbinds the delegates to avoid dangling references

**TickComponent**
Records forward/applies backward depending on the state. DebugDraw can be enabled in console with the command Chronogy.DebugDraw 1 


### Interfaces

**IChronogySnapshotInterface**
An easy way to record anything extra in a project such as the state of a door being open or closed. A written example of this extension is available under ChronogyToggleable.cpp

**IChronogyAnimInterface**
The same as above but for animations.

