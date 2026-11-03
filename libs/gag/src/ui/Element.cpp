// SPDX-License-Identifier: GPL-3.0-or-later
#include <ui/Element.h>
#include <ui/Host.h>

namespace GAGGUI::ui
{
void Node::activate(Host &host, int direction)
{
	if (direction == 0 && interactive() && enabled())
		tap(bounds.center(), host);
}

Node *Node::hitTest(Point point, const std::function<bool(const Node &)> &accept)
{
	if (clipsChildren() && !bounds.contains(point))
		return nullptr;
	for (auto it = children.rbegin(); it != children.rend(); ++it)
		if (auto *hit = (*it)->hitTest(point, accept))
			return hit;
	if (bounds.contains(point) && accept(*this))
		return this;
	return nullptr;
}

void Node::visit(const std::function<void(Node &)> &visitor)
{
	visitor(*this);
	for (auto &child : children)
		child->visit(visitor);
}

Node *Node::find(const std::string &wanted)
{
	if (!wanted.empty() && key == wanted)
		return this;
	for (auto &child : children)
		if (auto *found = child->find(wanted))
			return found;
	return nullptr;
}

namespace
{
class Empty : public Node
{
  public:
	Size measure(const LayoutContext &, Constraints constraints) override
	{
		return constraints.clamp({});
	}
	const char *name() const override { return "empty"; }
};
} // namespace

Element empty() { return std::make_shared<Empty>(); }
} // namespace GAGGUI::ui
