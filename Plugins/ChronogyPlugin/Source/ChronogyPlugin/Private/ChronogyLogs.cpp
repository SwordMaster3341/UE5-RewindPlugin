// S-G-D
#include "ChronogyLogs.h"

DEFINE_LOG_CATEGORY(LogChronogy);

TAutoConsoleVariable<int32> CVarChronogyDebugDraw(
	TEXT("Chronogy.DebugDraw"),
	0,
	TEXT("1 = draw recorded snapshot path (green) and rewind playhead (red) for every ChronogyComponent"),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarChronogyVerbose(
	TEXT("Chronogy.Verbose"),
	0,
	TEXT("1 = log snapshot recording intervals, buffer overflow events, and playback progress"),
	ECVF_Default);
