// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderStateScope.h>
#include <ui/Canvas.h>
#include <GraphicContext.h>
#include <Toolkit.h>
#include <cmath>

namespace GAGGUI::ui
{
ToolkitTextMeasurer::ToolkitTextMeasurer(const Theme &theme, bool touch, double textScale)
	: theme(theme), touch(touch), textScale(textScale > 0 ? textScale : 1)
{
}

GAGCore::Font *ToolkitTextMeasurer::font(FontRole role) const
{
	auto *result = GAGCore::Toolkit::getFont(theme.fontName(role, touch));
	if (!result)
		result = GAGCore::Toolkit::getFont(theme.fontName(role, false));
	return result;
}

int ToolkitTextMeasurer::width(FontRole role, const std::string &text) const
{
	auto *f = font(role);
	return f ? int(std::lround(f->getStringWidth(text) * textScale)) : 0;
}

int ToolkitTextMeasurer::lineHeight(FontRole role) const
{
	auto *f = font(role);
	return f ? int(std::lround(f->getStringHeight("Ag") * textScale)) : 0;
}

SurfaceCanvas::SurfaceCanvas(GAGCore::DrawableSurface &target, const Theme &theme,
							 const Presentation &presentation)
	: target(target), theme(theme), presentation(presentation),
	  measure(std::make_unique<ToolkitTextMeasurer>(theme, presentation.touch,
													presentation.textScale))
{
	int x, y, w, h;
	target.getClipRect(&x, &y, &w, &h);
	clips.push_back({x, y, w, h});
}

SurfaceCanvas::~SurfaceCanvas()
{
	const auto &base = clips.front();
	target.setClipRect(base.x, base.y, base.w, base.h);
}

Size SurfaceCanvas::size() const { return {target.getW(), target.getH()}; }

const TextMeasurer &SurfaceCanvas::measurer() const { return *measure; }

void SurfaceCanvas::fillRect(Rect r, GAGCore::Color color)
{
	if (r.empty())
		return;
	target.drawFilledRect(r.x, r.y, r.w, r.h, color);
}

void SurfaceCanvas::strokeRect(Rect r, GAGCore::Color color)
{
	if (r.empty())
		return;
	target.drawRect(r.x, r.y, r.w, r.h, color);
}

void SurfaceCanvas::fillRounded(Rect r, int radius, GAGCore::Color color)
{
	if (r.empty())
		return;
	radius = std::min({radius, r.w / 2, r.h / 2});
	if (radius <= 0)
	{
		fillRect(r, color);
		return;
	}
	target.drawFilledRect(r.x, r.y + radius, r.w, r.h - 2 * radius, color);
	for (int row = 0; row < radius; ++row)
	{
		const float dy = radius - row - 0.5f;
		const int inset = radius - int(std::sqrt(float(radius * radius) - dy * dy));
		target.drawFilledRect(r.x + inset, r.y + row, r.w - 2 * inset, 1, color);
		target.drawFilledRect(r.x + inset, r.bottom() - row - 1, r.w - 2 * inset, 1, color);
	}
}

void SurfaceCanvas::line(Point a, Point b, GAGCore::Color color)
{
	target.drawLine(a.x, a.y, b.x, b.y, color);
}

void SurfaceCanvas::text(Point at, FontRole role, const std::string &value, GAGCore::Color color)
{
	if (value.empty())
		return;
	auto *font = static_cast<ToolkitTextMeasurer &>(*measure).font(role);
	if (!font)
		return;
	font->pushStyle(GAGCore::Font::Style(GAGCore::Font::STYLE_NORMAL, color));
	const double scale = presentation.textScale;
	auto *context = dynamic_cast<GAGCore::GraphicContext *>(&target);
	if (context && std::abs(scale - 1) > 1e-6)
	{
		const auto current = clips.back();
		SDL_Rect bounds{current.x, current.y, current.w, current.h};
		{
            GAGCore::UITransformScope pass(*context, float(scale), float(at.x), float(at.y), &bounds);
            target.drawString(0, 0, font, value);
        }
		applyClip();
	}
	else
		target.drawString(at.x, at.y, font, value);
	font->popStyle();
}

void SurfaceCanvas::applyClip()
{
	const auto &c = clips.back();
	target.setClipRect(c.x, c.y, c.w, c.h);
}

void SurfaceCanvas::pushClip(Rect rect)
{
	clips.push_back(clips.back().intersect(rect));
	applyClip();
}

void SurfaceCanvas::popClip()
{
	if (clips.size() > 1)
		clips.pop_back();
	applyClip();
}

Rect SurfaceCanvas::clip() const { return clips.back(); }

void SurfaceCanvas::drawSurface(Rect d, GAGCore::DrawableSurface *surface, unsigned char alpha)
{
	if (!surface || d.empty())
		return;
	if (d.w == surface->getW() && d.h == surface->getH())
		target.drawSurface(d.x, d.y, surface, alpha);
	else
		target.drawSurface(d.x, d.y, d.w, d.h, surface, alpha);
}

void SurfaceCanvas::drawIcon(Rect destination, const IconAsset &asset, GAGCore::Color color)
{
	if (destination.empty() || !asset.available())
		return;
	auto *context = dynamic_cast<GAGCore::GraphicContext *>(&target);
	const double rasterScale = context ? context->getRasterScale() : 1;
	const int required =
		int(std::ceil(std::max(destination.w, destination.h) * std::max(1.0, rasterScale)));
	const IconAsset::Raster *chosen = &asset.rasters.back();
	for (const auto &raster : asset.rasters)
		if (raster.pixels >= required)
		{
			chosen = &raster;
			break;
		}
	if (!chosen->surface)
		return;
	const std::uint32_t rgba = (std::uint32_t(color.r) << 24) | (std::uint32_t(color.g) << 16) |
							   (std::uint32_t(color.b) << 8) | color.a;
	const auto key = std::make_pair(chosen->pixels, rgba);
	auto found = asset.colours.find(key);
	if (found == asset.colours.end())
	{
		SDL_Surface *mask =
			SDL_ConvertSurfaceFormat(chosen->surface->getSDLSurface(), SDL_PIXELFORMAT_RGBA32, 0);
		if (!mask)
			return;
		if (SDL_LockSurface(mask) != 0)
		{
			SDL_FreeSurface(mask);
			return;
		}
		for (int y = 0; y < mask->h; ++y)
		{
			auto *row =
				reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(mask->pixels) + y * mask->pitch);
			for (int x = 0; x < mask->w; ++x)
			{
				Uint8 r, g, b, a;
				SDL_GetRGBA(row[x], mask->format, &r, &g, &b, &a);
				row[x] = SDL_MapRGBA(mask->format, color.r, color.g, color.b,
									 Uint8(unsigned(a) * color.a / 255));
			}
		}
		SDL_UnlockSurface(mask);
		SDL_SetSurfaceBlendMode(mask, SDL_BLENDMODE_BLEND);
		auto tinted = std::make_shared<GAGCore::DrawableSurface>(mask);
		SDL_FreeSurface(mask);
		if (asset.colours.size() >= 64)
			asset.colours.clear();
		found = asset.colours.emplace(key, std::move(tinted)).first;
	}
	drawSurface(destination, found->second.get(), 255);
}

void SurfaceCanvas::drawSprite(Point at, GAGCore::Sprite *sprite, int frame)
{
	if (!sprite || frame < 0 || frame >= sprite->getFrameCount())
		return;
	target.drawSprite(at.x, at.y, sprite, unsigned(frame));
}

void SurfaceCanvas::transformed(double scale, Point origin, Rect bounds,
								const std::function<void()> &paint)
{
	auto *context = dynamic_cast<GAGCore::GraphicContext *>(&target);
	const auto area = clips.back().intersect(bounds);
	if (context)
	{
		SDL_Rect limit{area.x, area.y, area.w, area.h};
		{
            GAGCore::UITransformScope pass(*context, float(scale), float(origin.x), float(origin.y), &limit);
            paint();
        }
		applyClip();
	}
	else
	{
		pushClip(bounds);
		paint();
		popClip();
	}
}
} // namespace GAGGUI::ui
