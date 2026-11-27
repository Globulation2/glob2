// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FrontendUI.h"

namespace GAGGUI
{
class ScreenStack;
}

class HandPreferenceScreen : public Glob2UI::Screen
{
  public:
	// Ends with a thumb side, or this when the player chose the desktop layout.
	static constexpr int USE_DESKTOP_LAYOUT = 100;

	// With a screen stack the screen offers "Use desktop layout" behind a
	// confirmation, for desktops that were wrongly detected as touch devices.
	explicit HandPreferenceScreen(GAGGUI::ScreenStack *screens = nullptr) : screens(screens) {}
	// Whether the first-launch prompt should run: the thumb side is unchosen and
	// the player uses touch. A mouse host in a small frame (a web portal's 800x450
	// desktop embed) gets the compact layout but has no thumb to place, and its
	// "Use desktop layout" cannot make the frame fit, so asking would loop. A
	// GLOB2_MOBILE_UI override keeps asking whenever it requests phone controls.
	static bool needed();
	const char *recordingId() const override { return "hand_preference"; }
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void choose(int side);
	// Opens the confirmation; useDesktopLayout() runs if the player confirms.
	void confirmDesktopLayout();
	// Saves the Desktop interface preference, applies it and ends the screen.
	void useDesktopLayout();

  protected:
	void onEscape() override {} // Both choices complete the first-launch prompt.

  private:
	GAGGUI::ScreenStack *screens;
	bool saveFailed = false;
	bool confirming = false;
};
