// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Element.h"
#include <algorithm>
#include <optional>

namespace GAGGUI::ui
{
// Layout models for controls painted by a domain renderer. Build these before
// painting or input; use these same bounds/callbacks in the domain hit-test.
// Coordinates are in the host's logical surface, not relative to root bounds.
struct PositionedControl
{
	std::string key, role = "button", label, identity;
	Rect bounds;
	std::optional<Rect> clip;
	ControlValue value;
	bool enabled = true, selected = false;
	std::vector<ActionKind> actions = {ActionKind::Activate};
	std::function<bool(const SemanticAction &)> perform;
	std::function<void(Point)> tap;
	bool contains(Point point) const
	{
		return bounds.contains(point) && (!clip || clip->contains(point));
	}
};

namespace detail
{
class PositionedNode final : public Node
{
	PositionedControl control;

  public:
	explicit PositionedNode(PositionedControl value) : control(std::move(value))
	{
		key = control.key;
		bounds = control.bounds;
	}
	Size measure(const LayoutContext &, Constraints constraints) override
	{
		return constraints.clamp({bounds.w, bounds.h});
	}
	void arrange(const LayoutContext &, Rect) override { bounds = control.bounds; }
	const char *name() const override { return control.role.c_str(); }
	std::string accessibleText() const override { return control.label; }
	ControlValue controlValue() const override { return control.value; }
	std::string semanticIdentity() const override { return control.identity; }
	bool selected() const override { return control.selected; }
	bool enabled() const override { return control.enabled; }
	bool interactive() const override { return !control.actions.empty(); }
	std::vector<ActionKind> supportedActions() const override { return control.actions; }
	bool scrollable() const override
	{
		return std::find(control.actions.begin(), control.actions.end(), ActionKind::Scroll) !=
			   control.actions.end();
	}
	void tap(Point point, Host &host) override;
	bool perform(const SemanticAction &action, Host &host) override;
	void scrollBy(int pixels, Host &host) override;
};
class PositionedClip final : public Wrapper
{
	Rect clip;

  public:
	PositionedClip(Element child, Rect area) : Wrapper(std::move(child)), clip(area) {}
	bool clipsChildren() const override { return true; }
	void arrange(const LayoutContext &context, Rect) override
	{
		bounds = clip;
		if (child())
			child()->arrange(context, clip);
	}
};
class PositionedRoot final : public Node
{
  public:
	explicit PositionedRoot(std::vector<PositionedControl> controls)
	{
		for (auto &control : controls)
		{
			const auto clip = control.clip;
			Element node = std::make_shared<PositionedNode>(std::move(control));
			if (clip)
				node = std::make_shared<PositionedClip>(std::move(node), *clip);
			children.push_back(std::move(node));
		}
	}
	Size measure(const LayoutContext &, Constraints constraints) override
	{
		return constraints.biggest();
	}
	void arrange(const LayoutContext &context, Rect rect) override
	{
		bounds = rect;
		for (auto &child : children)
			child->arrange(context, rect);
	}
};
} // namespace detail
inline Element positionedControls(std::vector<PositionedControl> controls)
{
	return std::make_shared<detail::PositionedRoot>(std::move(controls));
}
// Bind a subtree to the lifetime of the domain object its callbacks address.
// Host incorporates this context into descendant target revisions.
inline Element semanticContext(Element child, std::string identity)
{
	class Context final : public Wrapper
	{
		std::string identity;

	  public:
		Context(Element child, std::string value)
			: Wrapper(std::move(child)), identity(std::move(value))
		{
		}
		std::string semanticIdentity() const override { return identity; }
	};
	return std::make_shared<Context>(std::move(child), std::move(identity));
}
} // namespace GAGGUI::ui
