// Minimal stand-in for the Orbiter API, used only by the pipe test harness.
#pragma once
#include <windows.h>
class VESSEL {};
VESSEL *oapiGetFocusInterface();
double oapiGetTimeAcceleration();
