// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextLayout.h"
#include <functional>
#include <memory>

namespace GAGCore
{
class DrawableSurface;
class Sprite;
} // namespace GAGCore

namespace GAGGUI::ui
{
// Drawing surface abstraction so controls paint without a window and tests can
// record what was drawn. Text drawing honours the presentation text scale.
class Canvas
{
  public:
	virtual ~Canvas() = default;
	virtual Size size() const = 0;
	virtual const TextMeasurer &measurer() const = 0;
	virtual void fillRect(Rect rect, GAGCore::Color color) = 0;
	virtual void strokeRect(Rect rect, GAGCore::Color color) = 0;
	virtual void fillRounded(Rect rect, int radius, GAGCore::Color color) = 0;
	virtual void line(Point from, Point to, GAGCore::Color color) = 0;
	// Draws one line of text with its top-left at `at`.
	virtual void text(Point at, FontRole role, const std::string &text, GAGCore::Color color) = 0;
	virtual void pushClip(Rect rect) = 0;
	virtual void popClip() = 0;
	virtual Rect clip() const = 0;
	// Bitmap content; recording canvases ignore these.
	virtual void drawSurface(Rect destination, GAGCore::DrawableSurface *surface,
							 unsigned char alpha = 255) = 0;
	virtual void drawSprite(Point at, GAGCore::Sprite *sprite, int frame) = 0;
	// Run a legacy painter with a scale and translation applied to the surface.
	virtual void transformed(double scale, Point origin, Rect bounds,
							 const std::function<void()> &paint) = 0;
	// Escape hatch for custom painters; null when recording.
	virtual GAGCore::DrawableSurface *surface() { return nullptr; }
};

// Real canvas over a DrawableSurface (or GraphicContext for scaled text).
class SurfaceCanvas : public Canvas
{
  public:
	SurfaceCanvas(GAGCore::DrawableSurface &target, const Theme &theme, const Presentation &presentation);
	~SurfaceCanvas() override;
	Size size() const override;
	const TextMeasurer &measurer() const override;
	void fillRect(Rect rect, GAGCore::Color color) override;
	void strokeRect(Rect rect, GAGCore::Color color) override;
	void fillRounded(Rect rect, int radius, GAGCore::Color color) override;
	void line(Point from, Point to, GAGCore::Color color) override;
	void text(Point at, FontRole role, const std::string &text, GAGCore::Color color) override;
	void pushClip(Rect rect) override;
	void popClip() override;
	Rect clip() const override;
	void drawSurface(Rect destination, GAGCore::DrawableSurface *surface,
					 unsigned char alpha) override;
	void drawSprite(Point at, GAGCore::Sprite *sprite, int frame) override;
	void transformed(double scale, Point origin, Rect bounds,
					 const std::function<void()> &paint) override;
	GAGCore::DrawableSurface *surface() override { return &target; }

  private:
	GAGCore::DrawableSurface &target;
	const Theme &theme;
	Presentation presentation;
	std::unique_ptr<TextMeasurer> measure;
	std::vector<Rect> clips;
	void applyClip();
};

// Measures through Toolkit fonts, scaled by the presentation text scale.
class ToolkitTextMeasurer : public TextMeasurer
{
  public:
	ToolkitTextMeasurer(const Theme &theme, bool touch, double textScale);
	int width(FontRole role, const std::string &text) const override;
	int lineHeight(FontRole role) const override;
	GAGCore::Font *font(FontRole role) const;
	double scale() const { return textScale; }

  private:
	const Theme &theme;
	bool touch;
	double textScale;
};
} // namespace GAGGUI::ui
