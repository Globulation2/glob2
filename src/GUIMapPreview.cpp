// SPDX-License-Identifier: GPL-3.0-or-later
#include "GUIMapPreview.h"
#include <StringTable.h>
#include <Toolkit.h>
#include <GUIStyle.h>
#include <algorithm>
namespace
{
bool inside(MapPreviewGeometry::Rect r, int x, int y)
{
	return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
std::string tr(const char *key)
{
	return Toolkit::getStringTable()->getString(key);
}
} // namespace
void MapPreview::paintOverlay(DrawableSurface *target, MapPreviewGeometry::Rect area)
{
	if (starts.empty() || getLastWidth() <= 0 || getLastHeight() <= 0)
		return;
	auto font = Toolkit::getFont(markerSize >= 16 ? "standard" : "little");
	const int half = markerSize / 2;
	for (size_t i = 0; i < starts.size(); ++i)
	{
		const int anchorX = view.x(starts[i].x, getLastWidth(), area);
		const int anchorY = view.y(starts[i].y, getLastHeight(), area);
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int px = anchorX + dx * area.w, py = anchorY + dy * area.h;
				target->drawFilledRect(px - half - 2, py - half - 2, markerSize + 4, markerSize + 4,
									   Color(20, 30, 20));
				target->drawFilledRect(px - half, py - half, markerSize, markerSize,
									   starts[i].color);
				font->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(0, 0, 0)));
				const auto label = std::to_string(i + 1);
				target->drawString(px - font->getStringWidth(label) / 2, py - half, font, label);
				font->popStyle();
			}
	}
}
MapPreview::MapPreview(int x, int y, Uint32 ha, Uint32 va)
	: MapPreview(x, y, ha, va, tr("[Map preview controls]"), "standard")
{
}
MapPreview::MapPreview(int x, int y, Uint32 ha, Uint32 va, const std::string &tip,
					   const std::string &font)
	: RectangularWidget(tip, font)
{
	this->x = x;
	this->y = y;
	hAlignFlag = ha;
	vAlignFlag = va;
	w = h = PreviewSize;
}
MapPreview::~MapPreview()
{
	cancelDrag();
	delete surface;
	delete raster;
}
void MapPreview::setMapThumbnail(const std::string &name)
{
	MapThumbnail next;
	next.loadFromMap(name);
	setMapThumbnail(next);
	if (name.empty())
		setState(State::Empty);
}
void MapPreview::setMapThumbnail(const MapThumbnail &next)
{
	if (next.pixels() == thumbnail.pixels() && next.isLoaded())
		return;
	if (animateChanges && next.isLoaded())
	{
		if (raster)
		{
			auto frame = std::make_unique<DrawableSurface>(raster->getW(), raster->getH());
			frame->drawSurface(0, 0, raster);
			auto visible = mapArea(), area = worldArea();
			area.x -= visible.x;
			area.y -= visible.y;
			paintOverlay(frame.get(), area);
			// A rapid reroll starts from the current blend, not a hidden endpoint.
			if (transitioning && previousFrame && previousFrame->getW() == frame->getW() &&
				previousFrame->getH() == frame->getH())
				frame->drawSurface(0, 0, previousFrame.get(), transitionAlpha());
			previousFrame = std::move(frame);
		}
		else if (!transitioning)
			previousFrame.reset();
		transitioning = true;
		transitionPending = true;
	}
	else
	{
		transitioning = transitionPending = false;
		previousFrame.reset();
	}
	resetView();
	thumbnail = next;
	delete raster;
	raster = nullptr;
	delete surface;
	surface = nullptr;
	state = next.isLoaded() ? State::Ready : State::Failed;
	if (next.isLoaded())
	{
		surface = new DrawableSurface(next.pixels()->width, next.pixels()->height);
		thumbnail.loadIntoSurface(surface);
	}
}
void MapPreview::setState(State next)
{
	if (state == next)
		return;
	state = next;
	if (next != State::Ready)
	{
		transitioning = transitionPending = false;
		previousFrame.reset();
		resetView();
		thumbnail = MapThumbnail();
		delete surface;
		surface = nullptr;
		delete raster;
		raster = nullptr;
	}
}
Uint8 MapPreview::transitionAlpha() const
{
	if (transitionPending)
		return 255;
	const double t =
		std::min(1.0, double(Uint32(SDL_GetTicks() - transitionStarted)) / TransitionDurationMs);
	return Uint8(255.0 * (1.0 - t * t * (3.0 - 2.0 * t)));
}
MapPreviewGeometry::Rect MapPreview::box()
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
	if (hAlignFlag == ALIGN_FILL)
	{
		x += (w - this->w) / 2;
		w = this->w;
	}
	if (vAlignFlag == ALIGN_FILL)
	{
		y += (h - this->h) / 2;
		h = this->h;
	}
	return {x, y, w, h};
}
MapPreviewGeometry::Rect MapPreview::mapArea()
{
	return MapPreviewGeometry::fit(box(), getLastWidth(), getLastHeight());
}
MapPreviewGeometry::Rect MapPreview::worldArea()
{
	auto r = mapArea();
	int w = int(r.w * zoom), h = int(r.h * zoom);
	return {r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h};
}
void MapPreview::cancelDrag()
{
	if (dragging)
		SDL_CaptureMouse(SDL_FALSE);
	dragging = false;
}
bool MapPreview::handlePreviewEvent(SDL_Event *e)
{
	if (e->type == SDL_WINDOWEVENT && (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
									   e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
	{
		cancelDrag();
		return false;
	}
	if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT)
	{
		bool was = dragging;
		cancelDrag();
		return was;
	}
	if (e->type == SDL_MOUSEMOTION)
	{
		if (dragging && !(e->motion.state & SDL_BUTTON_LMASK))
			cancelDrag();
		if (dragging)
			view.drag(e->motion.x - mouseX, e->motion.y - mouseY, worldArea());
		mouseX = e->motion.x;
		mouseY = e->motion.y;
		return dragging;
	}
	if (e->type == SDL_MOUSEWHEEL && surface && inside(mapArea(), mouseX, mouseY))
	{
		const auto before = worldArea();
		const double mapX = double(mouseX - before.x) / before.w - view.offsetX;
		const double mapY = double(mouseY - before.y) / before.h - view.offsetY;
		int direction = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
		zoom = std::clamp(zoom * std::pow(1.5, std::clamp(direction, -4, 4)), 1.0, 4.0);
		const auto after = worldArea();
		view.offsetX = MapPreviewGeometry::wrap(double(mouseX - after.x) / after.w - mapX);
		view.offsetY = MapPreviewGeometry::wrap(double(mouseY - after.y) / after.h - mapY);
		return true;
	}
	if (e->type != SDL_MOUSEBUTTONDOWN || !inside(box(), e->button.x, e->button.y))
		return false;
	if (e->button.button == SDL_BUTTON_LEFT && state == State::Failed && retry)
	{
		retry();
		return true;
	}
	if (!surface || !inside(mapArea(), e->button.x, e->button.y))
		return false;
	if (e->button.button == SDL_BUTTON_RIGHT ||
		(e->button.button == SDL_BUTTON_LEFT && e->button.clicks >= 2))
	{
		resetView();
		return true;
	}
	if (e->button.button == SDL_BUTTON_LEFT)
	{
		dragging = true;
		mouseX = e->button.x;
		mouseY = e->button.y;
		SDL_CaptureMouse(SDL_TRUE);
		return true;
	}
	return false;
}
void MapPreview::paint()
{
	// Keep the layout slot, but paint only the fitted map and its outline.
	// The menu owns the unused space around rectangular maps.
	auto b = surface ? mapArea() : box();
	if (b.w <= 0 || b.h <= 0)
		return;
	auto target = parent->getSurface();
	int cx, cy, cw, ch;
	target->getClipRect(&cx, &cy, &cw, &ch);
	const int left = std::max(cx, b.x), top = std::max(cy, b.y);
	target->setClipRect(left, top, std::max(0, std::min(cx + cw, b.x + b.w) - left),
						std::max(0, std::min(cy + ch, b.y + b.h) - top));
	target->drawFilledRect(b.x, b.y, b.w, b.h, Color(15, 24, 19));
	if (surface)
	{
		const auto visible = mapArea();
		auto area = worldArea();
		const int ax = std::max(left, visible.x), ay = std::max(top, visible.y);
		target->setClipRect(ax, ay, std::max(0, std::min(cx + cw, visible.x + visible.w) - ax),
							std::max(0, std::min(cy + ch, visible.y + visible.h) - ay));
		const int dx = int(view.offsetX * area.w), dy = int(view.offsetY * area.h);
		// DrawableSurface's scaled blit is unimplemented in software mode.
		// Compose only the visible rectangle with SDL, then use the ordinary
		// blit on both renderers. Allocation is bounded by viewport size, not
		// zoom, and the raster is reused while the view remains unchanged.
		if (!raster || raster->getW() != visible.w || raster->getH() != visible.h ||
			rasterOffsetX != view.offsetX || rasterOffsetY != view.offsetY || rasterZoom != zoom)
		{
			delete raster;
			raster = new DrawableSurface(visible.w, visible.h);
			rasterOffsetX = view.offsetX;
			rasterOffsetY = view.offsetY;
			rasterZoom = zoom;
			for (int yy = -1; yy <= 0; ++yy)
				for (int xx = -1; xx <= 0; ++xx)
				{
					SDL_Rect destination{area.x - visible.x + dx + xx * area.w,
										 area.y - visible.y + dy + yy * area.h, area.w, area.h};
					SDL_BlitScaled(surface->getSDLSurface(), nullptr, raster->getSDLSurface(),
								   &destination);
				}
		}
		target->drawSurface(visible.x, visible.y, raster);
		paintOverlay(target, area);
		if (transitioning)
		{
			if (transitionPending)
			{
				transitionStarted = SDL_GetTicks();
				transitionPending = false;
			}
			const Uint8 alpha = transitionAlpha();
			if (alpha)
			{
				if (!previousFrame || previousFrame->getW() != visible.w ||
					previousFrame->getH() != visible.h)
				{
					auto frame = std::make_unique<DrawableSurface>(visible.w, visible.h);
					frame->drawFilledRect(0, 0, visible.w, visible.h, Color(211, 223, 197));
					if (previousFrame)
					{
						// Dimension changes may resize the old image, but never stretch it.
						const auto fitted =
							MapPreviewGeometry::fit({0, 0, visible.w, visible.h},
													previousFrame->getW(), previousFrame->getH());
						SDL_Rect destination{fitted.x, fitted.y, fitted.w, fitted.h};
						SDL_BlitScaled(previousFrame->getSDLSurface(), nullptr,
									   frame->getSDLSurface(), &destination);
					}
					previousFrame = std::move(frame);
				}
				target->drawSurface(visible.x, visible.y, previousFrame.get(), alpha);
			}
			else
			{
				transitioning = false;
				previousFrame.reset();
			}
		}
	}
	else
	{
		const char *key = state == State::Loading  ? "[Map preview loading]"
						  : state == State::Failed ? "[Map preview failed]"
												   : "[GUIMapPreview text 0]";
		auto font = Toolkit::getFont("standard");
		const auto label = tr(key);
		const int fh = font->getStringHeight(label);
		font->pushStyle(Font::Style(Font::STYLE_NORMAL, Color(235, 240, 224)));
		target->drawString(b.x + std::max(4, (b.w - font->getStringWidth(label)) / 2),
						   b.y + b.h / 2 - fh, font, label);
		const auto second = tr(state == State::Empty    ? "[GUIMapPreview text 1]"
							   : state == State::Failed ? (retry ? "[Map preview retry]"
																 : "[Map preview choose another]")
														: "[Map preview please wait]");
		target->drawString(b.x + std::max(4, (b.w - font->getStringWidth(second)) / 2),
						   b.y + b.h / 2, font, second);
		font->popStyle();
	}
	target->setClipRect(cx, cy, cw, ch);
	Style::style->drawFrame(target, b.x, b.y, b.w, b.h, Color::ALPHA_TRANSPARENT);
}
