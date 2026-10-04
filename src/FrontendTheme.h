// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIStyle.h>
#include <GraphicContext.h>
#include <memory>
#include <string>
class MenuColony;

// The frontend presentation outside the framework: the live colony backdrop and
// the paper panel, painted with the frontend Theme palette so every colour has
// one home. Widget painting itself lives in the framework controls.
class FrontendTheme : public GAGGUI::Style
{
  public:
	FrontendTheme();
	~FrontendTheme();
	static FrontendTheme *current;
	static bool allowed;
	static void rounded(GAGCore::DrawableSurface *, int, int, int, int, int, GAGCore::Color);
	void background(GAGCore::DrawableSurface *, bool panel = true,
					const SDL_Rect *content = nullptr);
	void onFrame() override;
	void drawTextButtonBackground(GAGCore::DrawableSurface *, int, int, int, int,
								  unsigned) override;
	void drawFrame(GAGCore::DrawableSurface *, int, int, int, int, unsigned) override;
	std::unique_ptr<MenuColony> colony;
	GAGGUI::Style *original;
	GAGCore::Font::Style originalFonts[3];

	// Copies the menu theme's colours into the legacy Style fields.
	void syncPalette();
	unsigned generation = 0;

  private:
	GAGCore::DrawableSurface *backdropImage();
	void terrain(GAGCore::DrawableSurface *);
	bool attempted = false, painted = false;
	std::string fallbackPath;
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
	FrontendScope(const FrontendScope &) = delete;
	FrontendScope &operator=(const FrontendScope &) = delete;

	// Re-applies the presentation after the menu theme changed.
	static void refresh();

  private:
	static void apply();
	bool enabled;
};
