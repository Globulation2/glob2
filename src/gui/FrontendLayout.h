// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MobileSafeArea.h"
#include <Toolkit.h>
#include <string_view>

// Frontend-only presentation policy. Keep classification independent of the
// keyboard-reduced safe rectangle and of the in-game compact HUD policy.
struct FrontendLayout
{
	GAGCore::ViewRect safe;
	bool touch = false, phone = false, twoPane = false;
	double unit = 1;

	// A narrow tablet uses the same stacked flow without being classified as a phone.
	bool singleColumn() const { return touch && (phone || !twoPane); }

	static GAGCore::Font *font(const char *name)
	{
		const auto forced = GAGCore::presentationOverride();
		const bool touch = forced ? *forced == GAGCore::PresentationPreference::Compact
								  : GAGCore::presentationInput.touch;
		if (touch && std::string_view(name) == "standard")
			name = "frontend-body";
		else if (touch && std::string_view(name) == "little")
			name = "frontend-support";
		return GAGCore::Toolkit::getFont(name);
	}
	static FrontendLayout resolve(GAGCore::GraphicContext *gfx)
	{
		FrontendLayout out;
		out.safe = GAGCore::mobileDialogSafe(gfx);
		out.unit = gfx->logicalUnitsPerPoint();
		const auto forced = GAGCore::presentationOverride();
		out.touch = forced ? *forced == GAGCore::PresentationPreference::Compact
						   : GAGCore::presentationInput.touch;
		out.phone = out.touch && std::min(gfx->getW(), gfx->getH()) / out.unit < 600;
		out.twoPane = !out.phone && out.safe.w / out.unit >= 720 && out.safe.h / out.unit >= 480;
		return out;
	}
};
