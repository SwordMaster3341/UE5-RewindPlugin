// S-G-D
#pragma once

#include "Modules/ModuleManager.h"

class FChronogyPluginModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
