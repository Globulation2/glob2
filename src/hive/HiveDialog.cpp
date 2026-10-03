// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiveDialog.h"
namespace Hive
{
using namespace Glob2UI;
Rect Dialog::available(const Presentation &p, const Metrics &m)
{
	auto area = p.dialog.inset(m.padding);
	area.w = std::min(area.w, p.pt(maxWidth()));
	area.h = std::max(1, area.h - p.pt(composerOpen ? 112 : 32));
	return area;
}
Rect Dialog::place(Size size, Rect area)
{
	const int h = std::min(area.h, size.h);
	return {area.x, area.bottom() - h, area.w, h};
}
bool Dialog::handle(const SDL_Event &e)
{
	int x = 0, y = 0;
	if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
	{
		x = e.button.x;
		y = e.button.y;
	}
	else if (e.type == SDL_EVENT_MOUSE_MOTION)
	{
		x = e.motion.x;
		y = e.motion.y;
	}
	else if (e.type == SDL_EVENT_MOUSE_WHEEL)
	{
		x = e.wheel.mouse_x;
		y = e.wheel.mouse_y;
	}
	else
		return false;
	const auto r = panelBounds();
	const bool inside = x >= r.x && x < r.right() && y >= r.y && y < r.bottom();
	if (!inside && !captured)
		return false;
	if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
		captured = true;
	eventLogical(e);
	if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
		captured = false;
	return true;
}
Element Dialog::build(const Presentation &p)
{
	ButtonOptions small;
	small.minHeight = 26;
	small.role = FontRole::Caption;
	CardOptions compact;
	compact.padding = p.pt(8);
	std::vector<Element> cards;
	cards.push_back(row({button(
							 "hive/compose", "Give order",
							 [this]
							 {
								 if (compose)
									 compose();
							 },
							 small),
						 button(
							 "hive/details", expanded ? "Less" : "Reports / details",
							 [this]
							 {
								 expanded = !expanded;
								 invalidate();
							 },
							 small)},
						{p.pt(6)}));
	if (!client->progress.empty())
		cards.push_back(
			card(paragraph(client->progress.substr(0, 240), {FontRole::Caption}), compact));
	else if (!client->reports.empty())
		cards.push_back(card(
			paragraph(expanded ? client->reports.back() : client->reports.back().substr(0, 180),
					  {FontRole::Caption}),
			compact));
	for (auto &order : client->standingOrders())
	{
		const std::string id = order.at("id"), name = order.at("name"),
						  status = order.value("controlStatus", std::string());
		const bool paused = order.at("paused"), missing = order.value("missingCheckpoint", false),
				   pending = status == "Updating…" || status == "Requested";
		std::vector<Element> body{label(name), caption(missing  ? "Needs a fresh order"
													   : paused ? "Paused"
																: "Active")};
		if (!status.empty())
			body.push_back(caption(status));
		if (expanded)
			body.push_back(
				paragraph(order.at("description").get<std::string>(), {FontRole::Caption}));
		auto toggle = small;
		toggle.enabled = !missing && !pending;
		auto cancel = small;
		cancel.enabled = !pending;
		body.push_back(row({button(
								"hive/toggle/" + id, paused ? "Resume" : "Pause",
								[this, id, paused]
								{
									client->change(id, paused ? "resume" : "pause");
									invalidate();
								},
								toggle),
							button(
								"hive/cancel/" + id, "Cancel",
								[this, id]
								{
									client->change(id, "remove");
									invalidate();
								},
								cancel)},
						   {p.pt(6)}));
		cards.push_back(card(column(std::move(body), {p.pt(2)}), compact));
	}
	return scroll("hive/cards", column(std::move(cards), {p.pt(6)}));
}
} // namespace Hive
