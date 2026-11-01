// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiveDialog.h"
#include <utility>
#include <algorithm>
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
	if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST || e.type == SDL_EVENT_WINDOW_HIDDEN ||
		e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
		e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
	{
		cancelInput();
		return false;
	}
	refreshPresentation();
	host().layoutIfNeeded();
	const auto inside = [&](Point p) { return panelBounds().contains(p); };
	if (const auto scroll = GAGCore::scrollGesture(e))
	{
		if (scroll->phase == GAGCore::ScrollGesturePhase::Began)
			capturedScroll = inside({int(scroll->x), int(scroll->y)})
				? std::optional<std::uint64_t>(scroll->sequence) : std::nullopt;
		// Momentum can start after the contact's Ended event. Keep ownership
		// until the next sequence so a late tail cannot reach the map.
		if (!capturedScroll || *capturedScroll != scroll->sequence) return false;
	}
	else if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION || e.type == SDL_EVENT_FINGER_UP)
	{
		const auto key = std::make_pair(e.tfinger.touchID, e.tfinger.fingerID);
		const Point point{int(e.tfinger.x * presentation().viewport.w), int(e.tfinger.y * presentation().viewport.h)};
		if (e.type == SDL_EVENT_FINGER_DOWN)
		{
			// Devices may reuse an ID after dropping a release. A new outside
			// sequence belongs to the map even if its old ID belonged to us.
			if (capturedFingers.erase(key)) InGameDialog::cancelInput();
			if (inside(point)) capturedFingers.insert(key);
		}
		if (!capturedFingers.contains(key)) return false;
		if (e.type == SDL_EVENT_FINGER_UP) capturedFingers.erase(key);
	}
	else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
	{
		const auto key = std::make_pair(e.button.which, e.button.button);
		if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
		{
			if (capturedMouseButtons.erase(key)) InGameDialog::cancelInput();
			if (inside({int(e.button.x), int(e.button.y)})) capturedMouseButtons.insert(key);
		}
		if (!capturedMouseButtons.contains(key)) return false;
		if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) capturedMouseButtons.erase(key);
	}
	else if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_WHEEL)
	{
		const auto device = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.which : e.wheel.which;
		const bool captured = std::any_of(capturedMouseButtons.begin(), capturedMouseButtons.end(),
			[&](const auto &key) { return key.first == device; });
		const Point point = e.type == SDL_EVENT_MOUSE_MOTION
			? Point{int(e.motion.x), int(e.motion.y)} : Point{int(e.wheel.mouse_x), int(e.wheel.mouse_y)};
		// A map drag keeps its sequence when it crosses this nonmodal card.
		if (!captured && (!inside(point) || (e.type == SDL_EVENT_MOUSE_MOTION && e.motion.state))) return false;
	}
	else return false;
	eventLogical(e);
	return true;
}
void Dialog::cancelInput()
{
	capturedFingers.clear();
	capturedMouseButtons.clear();
	capturedScroll.reset();
	InGameDialog::cancelInput();
}
namespace
{
// Client status tokens also drive controls; translate only at the display boundary.
std::string controlStatusText(const std::string &status)
{
    for (const auto &entry : {std::pair{"Stopping…", "[hive stopping]"},
             {"Stopped", "[hive stopped]"},
             {"Stop failed; retry the stop shortcut", "[hive stop failed]"},
             {"Updating…", "[hive updating]"},
             {"Requested", "[hive requested]"},
             {"Update failed; try again", "[hive update failed]"}})
        if (status == entry.first) return tr(entry.second);
    return status;
}
}
Element Dialog::build(const Presentation &p)
{
	ButtonOptions small;
	small.minHeight = 26;
	small.role = FontRole::Caption;
	CardOptions compact;
	compact.padding = p.pt(8);
	std::vector<Element> cards;
	if (!client->local())
		cards.push_back(row(
			{button(
				 "hive/compose", tr("[hive give order]"),
				 [this]
				 {
					 if (compose)
						 compose();
				 },
				 small),
			 button(
				 "hive/details", expanded ? tr("[hive less]") : tr("[hive reports and details]"),
				 [this]
				 {
					 expanded = !expanded;
					 invalidate();
				 },
				 small)},
			{p.pt(6)}));
	if (client->local())
		cards.push_back(button(
			"hive/details", expanded ? tr("[hive less]") : tr("[hive reports and details]"),
			[this]
			{
				expanded = !expanded;
				invalidate();
			},
			small));
	auto stop = small;
	const auto stopping = client->controlStatus.find("commander");
	stop.enabled = stopping == client->controlStatus.end() || stopping->second != "Stopping…";
	cards.push_back(button(
		"hive/stop", tr("[stop commander]"),
		[this]
		{
			client->stop();
			invalidate();
		},
		stop));
	if (stopping != client->controlStatus.end())
		cards.push_back(caption(controlStatusText(stopping->second)));
	// Trim at a UTF-8 boundary so a compact report never splits a character.
	auto preview = [](const std::string &text, std::size_t limit)
	{
		if (text.size() <= limit)
			return text;
		while (limit > 0 && (static_cast<unsigned char>(text[limit]) & 0xc0) == 0x80)
			--limit;
		return text.substr(0, limit) + "…";
	};
	if (!client->progress.empty())
		cards.push_back(
			card(paragraph(preview(client->progress, 240), {FontRole::Caption}), compact));
	if (expanded)
	{
		const auto count = std::min<std::size_t>(10, client->reports.size());
		for (std::size_t i = 0; i < count; ++i)
			cards.push_back(card(
				paragraph(controlStatusText(client->reports[client->reports.size() - 1 - i]), {FontRole::Caption}),
				compact));
	}
	else if (client->progress.empty() && !client->reports.empty())
		cards.push_back(
			card(paragraph(preview(controlStatusText(client->reports.back()), 180), {FontRole::Caption}), compact));
	for (auto &order : client->standingOrders())
	{
		const std::string id = order.at("id"), name = order.at("name"),
						  status = order.value("controlStatus", std::string());
        const std::string target = client->local()
            ? id + "/" + std::to_string(order.at("revision").get<unsigned>()) : id;
		const bool paused = order.at("paused"), missing = order.value("missingCheckpoint", false),
				   pending = status == "Updating…" || status == "Requested";
		std::vector<Element> body{label(name), caption(missing  ? tr("[hive needs fresh order]")
													   : paused ? tr("[Paused]")
																: tr("[hive active]"))};
		if (order.contains("recent") && !order["recent"].is_null())
			body.push_back(paragraph(preview(order["recent"].dump(), expanded ? 1000 : 180),
									 {FontRole::Caption}));
		if (!status.empty())
			body.push_back(caption(controlStatusText(status)));
		if (expanded)
			body.push_back(
				paragraph(order.at("description").get<std::string>(), {FontRole::Caption}));
		auto toggle = small;
		toggle.enabled = !missing && !pending;
		auto cancel = small;
		cancel.enabled = !pending;
		body.push_back(
			row({button(
					 "hive/toggle/" + target, paused ? tr("[hive resume]") : tr("[Pause]"),
					 [this, id, paused]
					 {
						 client->change(id, paused ? "resume" : "pause");
						 invalidate();
					 },
					 toggle),
				 button(
					 "hive/cancel/" + target, tr("[Cancel]"),
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
