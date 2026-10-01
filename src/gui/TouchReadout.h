// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ViewportTransform.h>
#include <string>

// A value readout lifted above a touch contact, so the finger that is changing
// the value never covers it. Shared by gameplay and the phone editor; points
// and rectangles are in drawable units, sizes are policy in InGameTouchTheme.
namespace TouchReadout
{
//! Where the readout is drawn: above the contact, or below it when the top of
//! `within` leaves no room; always horizontally inside `within`.
GAGCore::ViewRect bounds(GAGCore::ViewPoint contact, const std::string &text, GAGCore::ViewRect within);
void draw(GAGCore::ViewPoint contact, const std::string &text, GAGCore::ViewRect within);
} // namespace TouchReadout
