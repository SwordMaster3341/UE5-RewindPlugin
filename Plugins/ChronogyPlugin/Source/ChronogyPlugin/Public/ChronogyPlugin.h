// Blake de Armas
#pragma once

#include "Modules/ModuleManager.h"

//Plugin Boilerplate - mostly empty, but required for Unreal to recognize this as a module and load it at startup.

class FChronogyPluginModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
