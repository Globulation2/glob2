// SPDX-License-Identifier: GPL-3.0-or-later
#include "TouchDial.h"
#include "GlobalContainer.h"
#include <algorithm>
#include <cmath>
#include <vector>
using namespace GAGCore;

namespace TouchDial
{
namespace
{
constexpr double pi = 3.14159265358979323846;
double radians(double degrees)
{
	return degrees * pi / 180;
}
} // namespace

std::optional<Polar> polar(const Geometry &geometry, ViewPoint p)
{
	const double across = (geometry.mirrored ? p.x - geometry.center.x : geometry.center.x - p.x) / geometry.unit;
	const double up = (geometry.center.y - p.y) / geometry.unit;
	if (across < 0 || up < 0)
		return std::nullopt;
	return Polar{std::hypot(across, up), std::atan2(up, across) * 180 / pi};
}
ViewPoint point(const Geometry &geometry, double radius, double angle)
{
	const double across = radius * std::cos(radians(angle)) * geometry.unit;
	const double up = radius * std::sin(radians(angle)) * geometry.unit;
	return {geometry.mirrored ? geometry.center.x + across : geometry.center.x - across, geometry.center.y - up};
}
double padAngle(const Ring &ring, double points)
{
	return points / std::max(1.0, ring.middle()) * 180 / pi;
}
int value(double angle, double from, double to, int maximum)
{
	if (maximum <= 0 || to <= from)
		return 0;
	return std::clamp(int(std::lround((angle - from) / (to - from) * maximum)), 0, maximum);
}
double angleOf(int value, double from, double to, int maximum)
{
	return maximum <= 0 ? from : from + (to - from) * std::clamp(value, 0, maximum) / double(maximum);
}
std::array<int, 3> shares(const std::array<int, 3> &weights, int budget)
{
	const int total = weights[0] + weights[1] + weights[2];
	if (total <= 0)
		return {budget, 0, 0};
	std::array<int, 3> result{}, remainder{};
	int assigned = 0;
	for (int i = 0; i < 3; ++i)
	{
		result[i] = weights[i] * budget / total;
		remainder[i] = weights[i] * budget % total;
		assigned += result[i];
	}
	while (assigned++ < budget)
	{
		const auto best = std::max_element(remainder.begin(), remainder.end());
		++result[best - remainder.begin()];
		*best = -1;
	}
	return result;
}
struct Painter::Cache
{
	struct Entry
	{
		std::array<double, 8> key;
		Color color;
		double x, y, width, height;
		std::unique_ptr<DrawableSurface> image;
	};
	struct Label
	{
		std::array<double, 11> key;
		std::string text;
		Color color;
		Sprite *icon;
		int iconFrame;
		Color iconTint;
		double x, y, width, height;
		std::unique_ptr<DrawableSurface> image;
	};
	std::vector<Entry> entries;
	std::vector<Label> labels;
};
Painter::Painter() : cache(std::make_unique<Cache>()) {}
Painter::~Painter() = default;

void Painter::fill(const Geometry &g, double inner, double outer, double from, double to, const Color &color)
{
	if (to <= from || outer <= inner) return;
	auto *gfx = globalContainer->gfx;
	const double density = std::max(.25, double(gfx->getRasterScale()));
	const std::array<double, 8> key{inner, outer, from, to, g.unit, density, double(g.mirrored), 0};
	auto found = std::find_if(cache->entries.begin(), cache->entries.end(), [&](const auto &entry) {
		return entry.key == key && entry.color.r == color.r && entry.color.g == color.g &&
			entry.color.b == color.b && entry.color.a == color.a;
	});
	if (found == cache->entries.end())
	{
		// Coverage is evaluated in backing pixels, independently of the logical
		// canvas size. The rounded rectangle lives in the ring's polar coordinates.
		const double r0 = inner * g.unit, r1 = outer * g.unit;
		const double a0 = radians(from), a1 = radians(to), mid = (a0 + a1) / 2;
		const double radius = (r0 + r1) / 2, half = (r1 - r0) / 2;
		const double arcHalf = (a1 - a0) * radius / 2;
		const double corner = std::min({3.5 * g.unit, half, arcHalf});
		const double padding = 1.5 / density;
		const double across0 = r0 * std::cos(a1) - padding, across1 = r1 * std::cos(a0) + padding;
		const double up0 = r0 * std::sin(a0) - padding, up1 = r1 * std::sin(a1) + padding;
		const double x = g.mirrored ? across0 : -across1, y = -up1;
		const int width = std::max(1, int(std::ceil((across1 - across0) * density)));
		const int height = std::max(1, int(std::ceil((up1 - up0) * density)));
		auto *pixels = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
		if (!pixels) return;
		SDL_SetSurfaceBlendMode(pixels, SDL_BLENDMODE_BLEND);
		for (int row = 0; row < height; ++row)
			for (int column = 0; column < width; ++column)
			{
				const double across = g.mirrored ? x + (column + .5) / density : -x - (column + .5) / density;
				const double up = -y - (row + .5) / density;
				const double r = std::hypot(across, up);
				const double qx = std::abs((std::atan2(up, across) - mid) * radius) - arcHalf + corner;
				const double qy = std::abs(r - radius) - half + corner;
				const double distance = std::hypot(std::max(0., qx), std::max(0., qy)) +
					std::min(0., std::max(qx, qy)) - corner;
				const double coverage = std::clamp(.5 - distance * density, 0., 1.);
				const double bevel = std::clamp((r - r0) / (r1 - r0), 0., 1.);
				const double light = .91 + .16 * bevel + (distance > -1.2 * g.unit ? .06 : 0);
				const Uint32 red = Uint8(std::clamp(color.r * light, 0., 255.));
				const Uint32 green = Uint8(std::clamp(color.g * light, 0., 255.));
				const Uint32 blue = Uint8(std::clamp(color.b * light, 0., 255.));
				const Uint32 alpha = Uint8(std::round(color.a * coverage));
				auto *line = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels->pixels) + row * pixels->pitch);
				line[column] = (alpha << 24) | (red << 16) | (green << 8) | blue;
			}
		// Bounded per-inspector cache: ordinary taps reuse a small set of shapes;
		// continuous worker drags cannot accumulate textures indefinitely.
		if (cache->entries.size() >= 48) cache->entries.erase(cache->entries.begin());
		cache->entries.push_back({key, color, x, y, width / density, height / density,
			std::make_unique<DrawableSurface>(pixels, DrawableSurface::AdoptPixels{})});
		found = std::prev(cache->entries.end());
	}
	gfx->drawSurface(float(g.center.x + found->x), float(g.center.y + found->y),
		float(found->width), float(found->height), found->image.get());
}
void Painter::circle(ViewPoint center, double radius, const Color &color, const Color *trashInk)
{
	auto *gfx = globalContainer->gfx;
	const double density = std::max(.25, double(gfx->getRasterScale()));
	const double iconKey = trashInk ? double((Uint32(trashInk->a) << 24) | (Uint32(trashInk->r) << 16) | (Uint32(trashInk->g) << 8) | trashInk->b) : 0;
	const std::array<double, 8> key{radius, density, iconKey, 0, 0, 0, 0, 1};
	auto found = std::find_if(cache->entries.begin(), cache->entries.end(), [&](const auto &entry) {
		return entry.key == key && entry.color == color;
	});
	if (found == cache->entries.end())
	{
		const double extent = radius + 1.5 / density;
		const int size = std::max(1, int(std::ceil(2 * extent * density)));
		auto *pixels = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_ARGB8888);
		if (!pixels) return;
		SDL_SetSurfaceBlendMode(pixels, SDL_BLENDMODE_BLEND);
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x)
			{
				const double dx = (x + .5) / density - extent, dy = (y + .5) / density - extent;
				const double distance = std::hypot(dx, dy) - radius;
				const double coverage = std::clamp(.5 - distance * density, 0., 1.);
				const double light = 1 - .10 * dy / radius + (distance > -1.5 / density ? .25 : 0);
				auto channel = [&](Uint8 value) { return Uint32(std::clamp(value * light, 0., 255.)); };
				auto *line = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels->pixels) + y * pixels->pitch);
				line[x] = (Uint32(std::round(color.a * coverage)) << 24) |
					(channel(color.r) << 16) | (channel(color.g) << 8) | channel(color.b);
				if (trashInk)
				{
					const double scale = radius / 24;
					auto box = [&](double cx, double cy, double halfW, double halfH) {
						const double ax = std::abs(dx / scale - cx) - halfW, ay = std::abs(dy / scale - cy) - halfH;
						return (std::hypot(std::max(ax, 0.), std::max(ay, 0.)) + std::min(std::max(ax, ay), 0.)) * scale;
					};
					const double iconDistance = std::min({std::abs(box(0, 3, 7, 8)) - scale,
						box(0, -8, 10, 1), std::abs(box(0, -11, 3, 2)) - scale,
						box(-2.5, 3, .7, 5), box(2.5, 3, .7, 5)});
					const double ink = std::clamp(.5 - iconDistance * density, 0., 1.) * trashInk->a / 255.;
					auto blend = [&](Uint8 value, Uint8 foreground) {
						return Uint32(std::round(channel(value) * (1 - ink) + foreground * ink));
					};
					line[x] = (Uint32(std::round(color.a * coverage)) << 24) |
						(blend(color.r, trashInk->r) << 16) | (blend(color.g, trashInk->g) << 8) | blend(color.b, trashInk->b);
				}
			}
		if (cache->entries.size() >= 48) cache->entries.erase(cache->entries.begin());
		cache->entries.push_back({key, color, -extent, -extent, size / density, size / density,
			std::make_unique<DrawableSurface>(pixels, DrawableSurface::AdoptPixels{})});
		found = std::prev(cache->entries.end());
	}
	gfx->drawSurface(float(center.x + found->x), float(center.y + found->y),
		float(found->width), float(found->height), found->image.get());
}
void Painter::label(const Geometry &g, const Ring &ring, double from, double to,
	const std::string &text, Font *font, double textScale, const Color &color,
	Sprite *icon, int iconFrame, const Color *iconTint, bool trashIcon)
{
	if (to <= from) return;
	auto *gfx = globalContainer->gfx;
	const double density = std::max(.25, double(gfx->getRasterScale()));
	const int numberWidth = text.empty() ? 0 : font->getStringWidth(text);
	const int textHeight = font->getStringHeight(text.empty() ? "0" : text);
	if (textHeight <= 0) return;
	if (icon && (iconFrame < 0 || iconFrame >= icon->getFrameCount() ||
		icon->getW(iconFrame) <= 0 || icon->getH(iconFrame) <= 0)) icon = nullptr;
	if (!icon && numberWidth <= 0) return;
	const Color tint = iconTint ? *iconTint : Color(255, 255, 255);
	const int iconHeight = (icon || trashIcon) ? std::min(16, textHeight) : 0;
	const int iconWidth = trashIcon ? std::max(1, iconHeight * 3 / 4) : icon ? std::max(1, int(std::round(double(iconHeight) * icon->getW(iconFrame) / icon->getH(iconFrame)))) : 0;
	const int numberX = (icon || trashIcon) ? iconWidth + (numberWidth > 0 ? 2 : 0) : 0;
	const int textWidth = numberX + numberWidth;
	const double radius = ring.middle() * g.unit;
	const double start = radians(from), end = radians(to), mid = (start + end) / 2;
	// Padding includes the rounded corners. Shrink the complete label together,
	// keeping its icon and every translated character inside the same button.
	const double scale = std::min({textScale, (radius * (end - start) - 4 * g.unit) / textWidth,
		((ring.outer - ring.inner) * g.unit - 6 * g.unit) / textHeight});
	if (scale <= 0) return;
	const std::array<double, 11> key{ring.inner, ring.outer, from, to, g.unit, density,
		double(g.mirrored), scale, double(textWidth), double(textHeight), double(trashIcon)};
	auto found = std::find_if(cache->labels.begin(), cache->labels.end(), [&](const auto &entry) {
		return entry.key == key && entry.text == text && entry.color == color &&
			entry.icon == icon && entry.iconFrame == iconFrame && entry.iconTint == tint;
	});
	if (found == cache->labels.end())
	{
		DrawableSurface source(textWidth, textHeight);
		source.drawFilledRect(0, 0, textWidth, textHeight, Color(0, 0, 0, 0));
		if (icon)
		{
			icon->setBaseColor(tint);
			source.drawSprite(0, (textHeight - iconHeight) / 2, iconWidth, iconHeight, icon, iconFrame);
		}
		if (trashIcon)
		{
			const int top = (textHeight - iconHeight) / 2;
			auto rect = [&](double x, double y, double w, double h) {
				source.drawFilledRect(int(std::round(x * iconWidth)), top + int(std::round(y * iconHeight)),
					std::max(1, int(std::round(w * iconWidth))), std::max(1, int(std::round(h * iconHeight))), color);
			};
			rect(.33, .05, .34, .10); // handle
			rect(.08, .22, .84, .10); // lid
			rect(.17, .37, .10, .53);
			rect(.73, .37, .10, .53);
			rect(.17, .85, .66, .10);
			rect(.42, .42, .08, .35);
			rect(.58, .42, .08, .35);
		}
		font->pushStyle(Font::Style(Font::STYLE_NORMAL, color));
		source.drawString(numberX, 0, font, text);
		font->popStyle();
		const auto *sourcePixels = source.getSDLSurface();
		const double r0 = ring.inner * g.unit, r1 = ring.outer * g.unit;
		const double across0 = r0 * std::cos(end), across1 = r1 * std::cos(start);
		const double up0 = r0 * std::sin(start), up1 = r1 * std::sin(end);
		const double x = g.mirrored ? across0 : -across1, y = -up1;
		const int width = std::max(1, int(std::ceil((across1 - across0) * density)));
		const int height = std::max(1, int(std::ceil((up1 - up0) * density)));
		auto *pixels = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
		if (!pixels) return;
		SDL_SetSurfaceBlendMode(pixels, SDL_BLENDMODE_BLEND);
		auto pixelAt = [&](int sx, int sy) -> Uint32 {
			if (sx < 0 || sy < 0 || sx >= textWidth || sy >= textHeight) return 0;
			const auto *line = reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(sourcePixels->pixels) + sy * sourcePixels->pitch);
			return line[sx];
		};
		for (int row = 0; row < height; ++row)
			for (int column = 0; column < width; ++column)
			{
				const double across = g.mirrored ? x + (column + .5) / density : -x - (column + .5) / density;
				const double up = -y - (row + .5) / density;
				const double angle = std::atan2(up, across), r = std::hypot(across, up);
				const double sx = textWidth / 2. + (g.mirrored ? -1 : 1) * (angle - mid) * radius / scale - .5;
				const double sy = textHeight / 2. + (radius - r) / scale - .5;
				const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
				const double fx = sx - ix, fy = sy - iy;
				const Uint32 samples[] = {pixelAt(ix, iy), pixelAt(ix + 1, iy), pixelAt(ix, iy + 1), pixelAt(ix + 1, iy + 1)};
				const double weights[] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
				double alpha = 0;
				for (int i = 0; i < 4; ++i) alpha += weights[i] * (samples[i] >> 24);
				// Filter premultiplied colour so transparent sprite edges stay clean.
				auto channel = [&](int shift) -> Uint32 {
					if (alpha <= 0) return 0;
					double value = 0;
					for (int i = 0; i < 4; ++i)
						value += weights[i] * ((samples[i] >> shift) & 255) * (samples[i] >> 24);
					return Uint32(std::clamp(std::round(value / alpha), 0., 255.));
				};
				auto *line = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels->pixels) + row * pixels->pitch);
				line[column] = (Uint32(std::round(alpha)) << 24) | (channel(16) << 16) |
					(channel(8) << 8) | channel(0);
			}
		if (cache->labels.size() >= 24) cache->labels.erase(cache->labels.begin());
		cache->labels.push_back({key, text, color, icon, iconFrame, tint, x, y, width / density, height / density,
			std::make_unique<DrawableSurface>(pixels, DrawableSurface::AdoptPixels{})});
		found = std::prev(cache->labels.end());
	}
	gfx->drawSurface(float(g.center.x + found->x), float(g.center.y + found->y),
		float(found->width), float(found->height), found->image.get());
}
} // namespace TouchDial
