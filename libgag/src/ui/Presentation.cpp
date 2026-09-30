// SPDX-License-Identifier: GPL-3.0-or-later
#include <ui/Presentation.h>
#include <GraphicContext.h>
#include <HostViewport.h>
#include <InterfacePresentation.h>

namespace GAGGUI::ui
{
Presentation resolvePresentation(GAGCore::GraphicContext &context, double textScale)
{
	Presentation p;
	const int w = context.getW(), h = context.getH();
	p.viewport = {0, 0, w, h};
	p.unit = context.logicalUnitsPerPoint();
	p.textScale = textScale > 0 ? textScale : 1;
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
	return p;
}

Presentation resolvePresentation(GAGCore::DrawableSurface &surface, double textScale)
{
	if (auto *context = dynamic_cast<GAGCore::GraphicContext *>(&surface))
		return resolvePresentation(*context, textScale);
	auto p = Presentation::forSurface(surface.getW(), surface.getH());
	p.textScale = textScale > 0 ? textScale : 1;
	return p;
}
} // namespace GAGGUI::ui
