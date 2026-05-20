// S-G-D
#pragma once

#include "Logging/LogMacros.h"
#include "HAL/IConsoleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogChronogy, Log, All);

// Chronogy.DebugDraw  1 = draw snapshot paths and rewind playhead in-world each tick
// Chronogy.Verbose    1 = log snapshot recording, playback, and buffer events each interval
extern TAutoConsoleVariable<int32> CVarChronogyDebugDraw;
extern TAutoConsoleVariable<int32> CVarChronogyVerbose;
