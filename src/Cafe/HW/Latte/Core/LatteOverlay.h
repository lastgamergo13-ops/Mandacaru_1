#pragma once

void LatteOverlay_init();
void LatteOverlay_render(bool pad_view);
void LatteOverlay_updateStats(double fps, sint32 drawcalls, sint32 fastDrawcalls);
// Pushes the device's current Android thermal status level (as reported by
// PowerManager.getCurrentThermalStatus()/OnThermalStatusChangedListener, 0=NOMINAL..6=SHUTDOWN)
// so it can be shown in the overlay when config.overlay.device_thermal_status is on. Written from
// the Android/JNI thread, read from the render thread - see g_thermalStatus in LatteOverlay.cpp
// for why this one field is a real atomic instead of the "benign race" the rest of this file's
// stats rely on.
void LatteOverlay_updateThermalStatus(sint32 thermalStatus);

void LatteOverlay_pushNotification(const std::string& text, sint32 duration);