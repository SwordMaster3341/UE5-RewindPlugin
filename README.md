# UE5-RewindPlugin

A fully featured extendable time rewinding plugin for Unreal Engine 5 built ontop of the Gameplay Ability System. 
Is pretty memory efficient and supports rewinding any number of objects, animations, and custom properties with minimal setup.

>Originally inspired by [NU Makesgames' time rewind system](https://github.com/NuMakesGames/ue5-rewind) rebuilt from scratch as a decoupled production ready plugin.

---

### Note on this Repository:

This is a public facing version of a larger project I actively develop on a self hosted instance of Gitea. 
The commit history here will not reflect the full active development. 
In general, I self host most of my work so my github profile underepresents what I actually build.
This repo mainly exists to give a readable overview for anyone who wants to see it.

## Features

**GAS Integration:** 
All rewind actions are implemented as GAS compatable Gameplay Abilities, giving you in built support for cooldowns, activation conditions, and "easy" implementation for networking should your project require it.

**Decoupled Architecture**
No tight coupling to any actors or components, you can drop it into any project without modification.

**Unlimited Object Support**
Uses memory management to ensure no singular object takes up more than 2mb in memory giving near unlimited object support. 
This cap can be adjusted in the CPP code.

**Plugin Interface**
A clean and extensable API for regestering new rewindable components with minimal boilerplate.
>**Note:** Some assets require different implementation such as animation rewinding requiring its own base class, please refer to the documentation for specifics.

---

## Requirements

- Unreal Engine 5.x
- Gameplay Ability System (GAS) enabled and configured in your project
> **GAS must be set up before installing this plugin.** If you haven't done this yet, see Epic's [GAS documentation](https://docs.unrealengine.com/5.0/en-US/gameplay-ability-system-for-unreal-engine/) before continuing.
 
---

## Installation

1. Clone or download the repository into your projects 'Plugins/' folder
   
```bash
cd YourProject/Plugins
git clone https://github.com/yourusername/UE5-RewindPlugin.git
```
2. Regenerate project files
3. Rebuild manually from source
4. Enable the plugin and restart the editor

---

# Thats it!
>Hope you enjoy!
