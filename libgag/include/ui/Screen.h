// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Host.h"
#include <GUIBase.h>
#include <memory>

namespace GAGGUI::ui
{
// Publish a host's interactive controls to ApplicationHost::controlsChanged.
void publishControls(const void *owner, const Host &host);

// A screen whose whole content is one element tree rebuilt from its model.
class UIScreen : public Screen
{
  public:
	explicit UIScreen(const Theme &theme);
	~UIScreen() override;
	virtual Element build(const Presentation &presentation) = 0;
	void invalidate() { hostValue.invalidate(); }
	Host &host() { return hostValue; }
	const Presentation &presentation() const { return hostValue.presentation(); }
	const Theme &theme() const { return themeValue; }

	bool usesResponsiveViewport() const override { return true; }
	bool supportsCompactViewport() const override { return true; }
	void beginExecution(GAGCore::DrawableSurface *surface) override;
	void updateExecution(Uint32 tick) override;
	void handleExecutionEvent(SDL_Event event) override;
	void drawExecution() override;
	void viewportResized(int, int, int, int) override;
	void cancelExecutionInput() override;
	// Paints once into the current surface without presenting; harness use.
	void paintFrame(Uint32 tick);

  protected:
	// User text enlargement for this presentation.
	virtual double textScale(const Presentation &) const { return 1; }
	virtual void paintBackground(Canvas &canvas);
	virtual Rect available(const Presentation &presentation, const Metrics &metrics);
	virtual Rect place(Size measured, Rect available) { return available; }
	virtual void onEscape() {}
	// Hooks around each frame for themes with per-frame work.
	virtual void beforePaint() {}
	virtual void afterPaint(Canvas &) {}
	// Raw events a screen still wants to see (rare).
	virtual void onEvent(const SDL_Event &) {}
	// Return true to consume an event before the framework interprets it.
	virtual bool interceptEvent(const SDL_Event &) { return false; }
	void refreshPresentation();

  private:
	const Theme &themeValue;
	Host hostValue;
	std::unique_ptr<ToolkitTextMeasurer> measurer;
	bool measurerTouch = false;
	double measurerScale = 0;
	Uint32 lastTick = 0;
};

// A modal panel hosted by another screen or by gameplay; no nested loop.
class UIDialog
{
  public:
	explicit UIDialog(const Theme &theme);
	virtual ~UIDialog();
	virtual Element build(const Presentation &presentation) = 0;
	void attach(GAGCore::DrawableSurface &surface);
	bool event(const SDL_Event &event);
	void update(Uint32 tick);
	void draw(Uint32 tick);
	void cancelInput();
	void invalidate() { hostValue.invalidate(); }
	bool finished() const { return done; }
	int result() const { return resultValue; }
	void finish(int result);
	// Keep the dialog open after its result was consumed (a save that must retry).
	void resume() { done = false; }
	Host &host() { return hostValue; }
	const Presentation &presentation() const { return hostValue.presentation(); }
	const Theme &theme() const { return themeValue; }
	Rect panelBounds() const;

  protected:
	virtual double textScale(const Presentation &) const { return 1; }
	virtual bool scrim() const { return true; }
	virtual void onEscape() {}
	// Per-frame hook before the host updates (model polling).
	virtual void onUpdate(Uint32) {}
	// Return true to consume an event before the framework interprets it.
	virtual bool onEvent(const SDL_Event &) { return false; }
	// Panel width limit in points; negative uses the theme dialog width.
	virtual double maxWidth() const { return -1; }
	// Expand to the available height instead of sizing to content.
	virtual bool fillHeight() const { return false; }
	// Where the panel may lay out; defaults to the keyboard-safe rect, width limited.
	virtual Rect available(const Presentation &presentation, const Metrics &metrics);
	// Where the measured panel goes inside that area; defaults to centered.
	virtual Rect place(Size measured, Rect area);
	virtual void paintPanel(Canvas &canvas, Rect panel);
	void refreshPresentation();

  private:
	const Theme &themeValue;
	Host hostValue;
	GAGCore::DrawableSurface *surface = nullptr;
	std::unique_ptr<ToolkitTextMeasurer> measurer;
	bool measurerTouch = false;
	double measurerScale = 0;
	bool done = false;
	int resultValue = -1;
};
} // namespace GAGGUI::ui
