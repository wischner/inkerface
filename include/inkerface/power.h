//
// Declares switching the device off. A reader is "off" when its chip is in
// deep sleep, drawing almost nothing, with the e-paper panel still showing
// the last frame; the next press of the power button wakes it, and waking
// is a fresh boot that runs program() from the start.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#pragma once

#include <inkerface/display.h>

namespace inkerface {

//
// Switch the device off until its power button is pressed again.
//
// Parameters:
//     screen - The display. Its last presented frame stays on the panel,
//              so present an "off" screen first if the device should say
//              so.
//
// Notes:
//     Device: puts the panel and the board's switched power rails to
//     sleep, waits for the power button to be released, arms it as the
//     wake source and enters deep sleep. It does not return. Nothing is
//     shut down in an orderly way, so close files and say goodbye to
//     servers before calling it.
//     Host: there is nothing to switch off. It returns at once, and the
//     program should end, as the device's would.
//     The usual trigger is input_snapshot::power, a hold of the power
//     button.
//
void power_off(display &screen);

} // namespace inkerface
