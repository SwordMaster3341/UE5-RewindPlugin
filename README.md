# UE5-RewindPlugin

A fully featured extendable time rewinding plugin for Unreal Engine 5 built on top of the Gameplay Ability System. 
Is pretty memory efficient and supports rewinding any number of objects, animations, and custom properties with minimal setup.

>Originally inspired by [NU Makes Games' time rewind system](https://github.com/NuMakesGames/ue5-rewind), rebuilt from scratch as a decoupled, production-ready plugin.

---

## Features

**GAS Integration:** 
All rewind actions are implemented as GAS-compatible Gameplay Abilities, giving you built-in support for cooldowns and activation conditions. The plugin currently runs locally (single-player); building on GAS keeps it well positioned for replication, but networked rewind is not implemented yet.

**Decoupled Architecture**
No tight coupling to any actors or components, you can drop it into any project without modification.

**Unlimited Object Support**
Uses memory management to ensure no singular object takes up more than 2mb in memory giving near unlimited object support. 
This cap can be adjusted in the CPP code.

**Plugin Interface**
A clean and extensible API for registering new rewindable components with minimal boilerplate.
>**Note:** Some assets require different implementations such as animation rewinding requiring its own base class, please refer to the documentation for specifics.

---

## Requirements

- Unreal Engine 5.x
- Gameplay Ability System (GAS) enabled and configured in your project
> **GAS must be set up before installing this plugin.** If you haven't done this yet, see Epic's [GAS documentation](https://docs.unrealengine.com/5.0/en-US/gameplay-ability-system-for-unreal-engine/) before continuing.
 
---

## Installation

1. Download the Plugins Folder
2. Place inside C:..../YourUnrealEngineProject
3. Regenerate project files
4. Rebuild manually from source
5. Enable the plugin and restart the editor

### Note: A Demo Is Provided should one clone the whole folder as a project

---

### For A More Detailed Explanation See [Documentation](Documentation.md)



