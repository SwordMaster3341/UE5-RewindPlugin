// Blake de Armas
#pragma once

#include "Logging/LogMacros.h"
#include "HAL/IConsoleManager.h"

class CHRONOGYPLUGIN_API FLogCategoryLogChronogy : public FLogCategory<ELogVerbosity::Log, ELogVerbosity::All>
{
public:
	FORCEINLINE FLogCategoryLogChronogy()
		: FLogCategory(TEXT("LogChronogy"))
	{
	}
};
extern CHRONOGYPLUGIN_API FLogCategoryLogChronogy LogChronogy;

// Chronogy.DebugDraw  1 = draw snapshot paths and rewind playhead in-world each tick
extern TAutoConsoleVariable<int32> CVarChronogyDebugDraw;
// Chronogy.Verbose    1 = log snapshot recording, playback, and buffer events each interval
extern TAutoConsoleVariable<int32> CVarChronogyVerbose;
// Chronogy.DebugAnim  1 = log animation interface resolution and pose snapshot push events each rewind tick
extern TAutoConsoleVariable<int32> CVarChronogyDebugAnim;
