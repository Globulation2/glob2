// SPDX-License-Identifier: GPL-3.0-or-later
#include <ui/Presentation.h>
#include <GraphicContext.h>
#include <HostViewport.h>
#include <InterfacePresentation.h>

namespace GAGGUI::ui
{
Presentation resolvePresentation(GAGCore::GraphicContext &context, double touchTextScale)
{
	Presentation p;
	const int w = context.getW(), h = context.getH();
	p.viewport = {0, 0, w, h};
	p.unit = context.logicalUnitsPerPoint();
	auto insets = GAGCore::mobileSafeInsets(&context);
	const double uiScale = context.getUiScale();
	insets.left /= uiScale;
	insets.top /= uiScale;
	insets.right /= uiScale;
	insets.bottom /= uiScale;
	const int left = int(insets.left * p.unit), top = int(insets.top * p.unit);
	const int right = int(insets.right * p.unit), bottom = int(insets.bottom * p.unit);
	p.safe = {left, top, std::max(0, w - left - right), std::max(0, h - top - bottom)};
	const double keyboard = GAGCore::mobileKeyboardInset(&context) * p.unit / uiScale;
	p.dialog = p.safe;
	p.dialog.h = std::max(0, std::min(p.safe.bottom(), int(h - keyboard)) - p.safe.y);
	const auto forced = GAGCore::presentationOverride();
	if (forced)
		p.touch = *forced == GAGCore::PresentationPreference::Compact ||
				  GAGCore::phonePresentationRequested();
	else
		p.touch = GAGCore::presentationInput.touch;
	p.hover = GAGCore::presentationState.hover && !p.touch;
	if (forced && *forced == GAGCore::PresentationPreference::Spacious)
		p.hover = true;
	applyTextSize(p, touchTextScale);
	return p;
}

void applyTextSize(Presentation &p, double touchTextScale)
{
	const double user = GAGCore::userTextScale > 0 ? GAGCore::userTextScale : 1;
	p.textGrowth = user;
	p.textScale = (p.touch && touchTextScale > 0 ? touchTextScale : 1) * user;
	// Touch text is sized in points, so it keeps one physical size whatever
	// logical surface a screen asks for (gameplay's 800x600 floor raises `unit`
	// on phones). Pointer hosts keep the authored pixel sizes, enlarged only by
	// the preference, so their established layouts stay as they were at 100%.
	p.textUnit = p.touch ? p.unit * p.textScale : p.textScale;
}

Presentation resolvePresentation(GAGCore::DrawableSurface &surface, double touchTextScale)
{
	if (auto *context = dynamic_cast<GAGCore::GraphicContext *>(&surface))
		return resolvePresentation(*context, touchTextScale);
	return Presentation::forSurface(surface.getW(), surface.getH());
}
} // namespace GAGGUI::ui
