// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIStyle.h>
#include <GraphicContext.h>
#include <memory>
class MenuColony;

class FrontendTheme : public GAGGUI::Style
{
public:
	FrontendTheme();
	~FrontendTheme();
	static FrontendTheme* current;
	static bool allowed;
	static void rounded(GAGCore::DrawableSurface*, int, int, int, int, int, GAGCore::Color);
	void background(GAGCore::DrawableSurface*, bool panel = true, const SDL_Rect* content = nullptr);
	void onFrame() override;
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
};

// Also used to suspend front-end presentation around gameplay/editor loops.
// Screens hold scopes for their whole lifetime, and a screen stack creates the
// next screen before destroying the finished one, so scopes need not end in
// reverse order: the most recently created live scope decides the presentation.
class FrontendScope
{
public:
	explicit FrontendScope(bool enabled = FrontendTheme::allowed);
	~FrontendScope();
	FrontendScope(const FrontendScope&) = delete;
	FrontendScope& operator=(const FrontendScope&) = delete;
private:
	static void apply();
	bool enabled;
};
