// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <ViewportTransform.h>
#include <optional>
#include <string_view>

namespace GAGCore
{
enum class PresentationPreference
{
	Automatic,
	Compact,
	Spacious
};
enum class PresentationLayout
{
	Compact,
	Spacious
};

inline PresentationPreference parsePresentationPreference(std::string_view value)
{
	if (value == "compact")
		return PresentationPreference::Compact;
	if (value == "spacious")
		return PresentationPreference::Spacious;
	return PresentationPreference::Automatic;
}
inline const char *presentationPreferenceName(PresentationPreference value)
{
	switch (value)
	{
	case PresentationPreference::Compact:
		return "compact";
	case PresentationPreference::Spacious:
		return "spacious";
	default:
		return "automatic";
	}
}
// Host points (CSS pixels in a browser). Pixel density belongs to rendering.
struct ViewportMetrics
{
	double width = 800, height = 600, userScale = 1;
	SafeInsets safe;
	double keyboardInset = 0;
};
struct InputCapabilities
{
	bool touch = false, pointer = true, hover = true, keyboard = true;
};
struct ResolvedPresentation
{
	PresentationLayout layout = PresentationLayout::Spacious;
	ViewRect usable, dialog;
	double panelWidth = 160, minimumTargetHeight = 32;
	bool touch = false, hover = true;
	bool adaptedControls() const { return touch || layout == PresentationLayout::Compact; }
};
inline ResolvedPresentation resolvePresentation(PresentationPreference preference,
												const ViewportMetrics &viewport,
												const InputCapabilities &input,
												double desktopPanelWidth = 160)
{
	ResolvedPresentation result;
	const double scale =
		std::isfinite(viewport.userScale) && viewport.userScale > 0 ? viewport.userScale : 1;
	const double width = std::isfinite(viewport.width) ? std::max(0.0, viewport.width) : 0;
	const double height = std::isfinite(viewport.height) ? std::max(0.0, viewport.height) : 0;
	result.usable = {std::clamp(viewport.safe.left, 0.0, width) / scale,
					 std::clamp(viewport.safe.top, 0.0, height) / scale,
					 std::max(0.0, width - std::max(0.0, viewport.safe.left) -
									   std::max(0.0, viewport.safe.right)) /
						 scale,
					 std::max(0.0, height - std::max(0.0, viewport.safe.top) -
									   std::max(0.0, viewport.safe.bottom)) /
						 scale};
	result.panelWidth = input.touch ? 288 : desktopPanelWidth;
	const bool fits = result.usable.w >= 480 + result.panelWidth && result.usable.h >= 480;
	result.layout = preference != PresentationPreference::Compact && fits
						? PresentationLayout::Spacious
						: PresentationLayout::Compact;
	result.touch = input.touch;
	result.hover = input.pointer && input.hover;
	result.minimumTargetHeight = input.touch ? 48 : 32;
	result.dialog = result.usable;
	// Keyboard occlusion never participates in the gameplay fit decision.
	const double bottom = (height - std::max(viewport.safe.bottom, viewport.keyboardInset)) / scale;
	result.dialog.h =
		std::max(0.0, std::min(result.dialog.y + result.dialog.h, bottom) - result.dialog.y);
	return result;
}
inline std::optional<PresentationPreference> presentationOverride()
{
	if (const char *value = SDL_getenv_unsafe("GLOB2_MOBILE_UI"); value && *value)
		return std::string_view(value) == "1"            ? PresentationPreference::Compact
			   : std::string_view(value) == "touch-auto" ? PresentationPreference::Automatic
														 : PresentationPreference::Spacious;
	return {};
}
inline PresentationPreference presentationPreference = PresentationPreference::Automatic;
inline ViewportMetrics presentationViewport;
inline InputCapabilities presentationInput;
inline ResolvedPresentation presentationState;
// The player's text-size preference (1 = the authored size). Touch hosts apply
// it to all interface text: framework screens and dialogs through Presentation,
// bespoke touch painters through GraphicContext::textUnitsPerPoint().
inline double userTextScale = 1;
inline void updatePresentation(const ViewportMetrics &viewport, const InputCapabilities &input)
{
	presentationViewport = viewport;
	presentationInput = input;
	presentationState = resolvePresentation(presentationOverride().value_or(presentationPreference),
											viewport, input);
}
// Shared forms adapt for compact layouts and touch input, including tablets.
inline bool phonePresentationRequested()
{
	if (const char *value = SDL_getenv_unsafe("GLOB2_MOBILE_UI");
		value && std::string_view(value).starts_with("touch-"))
		return true;
	if (const auto forced = presentationOverride())
		return *forced == PresentationPreference::Compact;
	return presentationState.adaptedControls();
}
} // namespace GAGCore
