// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

/// Width, in pixels, of each icon button in the view-selector strip at the top
/// of the right sidebar. The strip is a centred row of `numButtons` buttons of
/// this width inside the RIGHT_MENU_WIDTH-px panel, so its left margin is
/// `(RIGHT_MENU_WIDTH - numButtons*PANEL_BUTTON_WIDTH)/2`. Both the drawing code
/// (GameGUI::drawPanelButton) and the click hit-test derive their geometry from
/// this constant so the two cannot drift apart.
constexpr int PANEL_BUTTON_WIDTH = 32;

/// Map a panel-relative click x to a view-selector button index.
///
/// `mx` is measured from the panel's left edge; `leftMargin` is the strip's
/// left inset (`(RIGHT_MENU_WIDTH - numButtons*PANEL_BUTTON_WIDTH)/2`), and
/// `numButtons` is how many buttons the strip currently shows (NB_VIEWS in play,
/// RDM_NB_VIEWS in replay). Returns the button index in [0, numButtons), or
/// std::nullopt when the click lands in the empty margin to the right of the
/// last button.
///
/// Historic quirk, preserved deliberately: a click in the empty left margin
/// (mx < leftMargin, i.e. left of button 0's drawn rect) selects button 0. The
/// original code computed `(mx - leftMargin)/32` and relied on C++ integer
/// division truncating toward zero to fold that negative-offset margin onto
/// index 0. That truncation also made `1 << index` in the caller's hidden-button
/// mask test undefined the moment the margin ever grew wide enough to push the
/// quotient below zero (a narrower menu, a wider inset, or dropping the mx>0
/// outer guard). Clamping the negative-offset case to index 0 here reproduces
/// the old margin-to-button-0 behaviour exactly while making the index
/// non-negative by construction, so the shift can never be undefined.
///
/// Pure and SDL-free by design so the geometry can be unit-tested without a
/// window (see test/PanelButtonHitTest.cpp); the caller applies the
/// hiddenGUIElements mask, since whether a button is currently shown is policy,
/// not geometry.
inline std::optional<int> panelButtonIndex(int mx, int leftMargin, int numButtons)
{
	const int rel = mx - leftMargin;
	const int index = rel < 0 ? 0 : rel / PANEL_BUTTON_WIDTH;
	if (index >= numButtons)
		return std::nullopt;
	return index;
}
