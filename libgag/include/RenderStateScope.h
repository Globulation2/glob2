// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>

namespace GAGCore
{
// Passes borrow the facade and its backend. State changes never transfer backend
// ownership. Restore clips even when a drawing pass exits through an exception.
class MapTransformScope
{
	GraphicContext &context;
	SDL_Rect clip{};

  public:
	MapTransformScope(GraphicContext &target, float scale, float x, float y, SDL_Rect bounds)
		: context(target)
	{
		context.getClipRect(&clip.x, &clip.y, &clip.w, &clip.h);
		context.beginMapTransform(scale, x, y, bounds.x, bounds.y, bounds.w, bounds.h);
	}
	~MapTransformScope()
	{
		context.endMapTransform();
		context.setClipRect(clip.x, clip.y, clip.w, clip.h);
	}
	MapTransformScope(const MapTransformScope &) = delete;
	MapTransformScope &operator=(const MapTransformScope &) = delete;
};
class UITransformScope
{
	GraphicContext &context;

  public:
	UITransformScope(GraphicContext &target, float scale, float x, float y, const SDL_Rect *bounds)
		: context(target)
	{
		context.setUITransform(scale, x, y, bounds);
	}
	~UITransformScope() { context.setUITransform(); }
	UITransformScope(const UITransformScope &) = delete;
	UITransformScope &operator=(const UITransformScope &) = delete;
};
} // namespace GAGCore
