// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIStyle.h>
#include <GraphicContext.h>
#include <memory>
class MenuColony;
class DynamicClouds;

// The front end's whole palette. Every menu screen reads from here; alpha is
// applied at the draw site, never baked into a token.
namespace FrontendPalette
{
	inline const GAGCore::Color ink(20, 44, 28);          // text, contours, hover ring
	inline const GAGCore::Color muted(104, 98, 70);       // secondary text
	inline const GAGCore::Color gold(233, 176, 53);       // primary action, selection
	inline const GAGCore::Color goldPressed(206, 152, 40);
	inline const GAGCore::Color violet(92, 74, 198);      // keyboard focus ring (the water's hue)
	inline const GAGCore::Color membrane(255, 232, 192);  // panel fill; tints toward sand over the world
	inline const GAGCore::Color gel(252, 246, 228);       // opaque control fill
	inline const GAGCore::Color gelDisabled(232, 224, 200);
	inline const GAGCore::Color scrim(18, 34, 22);        // veil / modal dim
}

class FrontendTheme : public GAGGUI::Style
{
public:
	FrontendTheme();
	~FrontendTheme();
	static FrontendTheme* current;
	static bool allowed;
	static void rounded(GAGCore::DrawableSurface*, int, int, int, int, int, GAGCore::Color);
	// Hand-drawn contour: per-corner radii and a low-frequency edge wobble seeded
	// from the rect, so it is stable frame to frame. Ink ring, fill, gel highlight.
	// inflate grows the drawn shape without changing the seed or the caller's rect.
	static void blob(GAGCore::DrawableSurface*, int x, int y, int w, int h, int r,
		GAGCore::Color fill, GAGCore::Color ink, int wobble, int inflate = 0);
	// The ring of blob() alone; never touches the interior (safe over list contents).
	static void ring(GAGCore::DrawableSurface*, int x, int y, int w, int h, int r,
		GAGCore::Color ink, int wobble, int inflate = 0);
	// Membrane opacity: 'normal' for the usual case; 255 under low-speed graphics.
	static int panelAlpha(int normal = 214);
	void background(GAGCore::DrawableSurface*, bool panel = true, const SDL_Rect* content = nullptr);
	void onFrame() override;
	void afterPaint(GAGCore::DrawableSurface*) override;
	void drawButtonSelection(GAGCore::DrawableSurface*, int, int, int, int) override;
	bool usesThemeTextColor() const override { return true; }
	void drawFieldBackground(GAGCore::DrawableSurface*, int, int, int, int) override;
	void drawSelectionBackground(GAGCore::DrawableSurface*, int, int, int, int) override;
	bool drawSelector(GAGCore::DrawableSurface*, int, int, int, int, unsigned, unsigned) override;
	void drawTextButtonBackground(GAGCore::DrawableSurface*, int, int, int, int, unsigned) override;
	void drawFrame(GAGCore::DrawableSurface*, int, int, int, int, unsigned) override;
	void drawOnOffButton(GAGCore::DrawableSurface*, int, int, int, int, unsigned, bool) override;
	void drawTriButton(GAGCore::DrawableSurface*, int, int, int, int, unsigned, Uint8) override;
	void drawScrollBar(GAGCore::DrawableSurface*, int, int, int, int, int, int) override;
	void drawProgressBar(GAGCore::DrawableSurface*, int, int, int, int, int) override;
	int getStyleMetric(StyleMetrics) override;
	std::unique_ptr<MenuColony> colony;
	GAGGUI::Style* original;
	GAGCore::Font::Style originalFonts[3];
private:
	bool attempted = false, painted = false;
	std::unique_ptr<GAGCore::DrawableSurface> fallback, fittedFallback;
	std::unique_ptr<DynamicClouds> clouds;
};

// Also used to suspend front-end presentation around gameplay/editor loops.
class FrontendScope
{
public:
	explicit FrontendScope(bool enabled = FrontendTheme::allowed);
	~FrontendScope();
	FrontendScope(const FrontendScope&) = delete;
	FrontendScope& operator=(const FrontendScope&) = delete;
private:
	GAGGUI::Style* previous;
	bool previousAllowed;
	GAGCore::Font::Style fonts[3];
};
