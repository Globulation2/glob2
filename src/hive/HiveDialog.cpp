// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiveDialog.h"
namespace Hive
{
Glob2UI::Element Dialog::build(const Glob2UI::Presentation &p)
{
	namespace ui = Glob2UI;
	std::string log;
	for (auto &report : client->reports)
	{
		if (!log.empty())
			log += "\n\n";
		log += report;
	}
	if (!client->progress.empty())
		log += "\n\n" + client->progress;
	std::vector<ui::Element> items;
	items.push_back(ui::paragraph("Hive Mind", {ui::FontRole::Heading}));
	items.push_back(ui::paragraph("Give orders. Receive reports. Lead your colony."));
	items.push_back(
		ui::textEditor("hive/reports", log, [](const auto &) {}, {true, 7, false, true}));
	items.push_back(ui::textField("hive/command", draft,
								  [this](const auto &value)
								  {
									  draft = value;
									  invalidate();
								  }));
	items.push_back(ui::toggle("hive/ongoing", "Keep overseeing this order", ongoing,
							   [this](bool value)
							   {
								   ongoing = value;
								   invalidate();
							   }));
	items.push_back(ui::row({ui::button("hive/send", "Give order",
										[this]
										{
											client->command(draft, ongoing);
											draft.clear();
											invalidate();
										}),
							 ui::button("hive/stop", "Stop commander", [this] { client->stop(); })},
							{p.pt(8)}));
	items.push_back(ui::paragraph("Standing orders", {ui::FontRole::Heading}));
	for (auto &order : client->standingOrders())
	{
		const auto id = order.at("id").get<std::string>();
		const bool paused = order.at("paused");
		items.push_back(ui::paragraph(order.at("name").get<std::string>() +
									  (paused ? " — Paused" : " — Active")));
		items.push_back(ui::paragraph(order.at("description").get<std::string>()));
		items.push_back(ui::row({ui::button("hive/toggle/" + id, paused ? "Resume" : "Pause",
											[this, id, paused]
											{
												client->change(id, paused ? "resume" : "pause");
												invalidate();
											}),
								 ui::button("hive/cancel/" + id, "Cancel order",
											[this, id]
											{
												client->change(id, "remove");
												invalidate();
											})},
								{p.pt(8)}));
	}
	items.push_back(
		ui::paragraph("Credits: " + std::to_string(client->account.value("available", 0))));
	items.push_back(ui::paragraph("Standing orders keep running when credits run out. Commander "
								  "assistance is permitted in ranked play."));
	items.push_back(
		ui::row({ui::button("hive/credits", "Add credits", [this] { client->buyCredits(); }),
				 ui::button("hive/close", "Return to colony", [this] { finish(0); })},
				{p.pt(8)}));
	return ui::scroll("hive/panel", ui::column(std::move(items), {p.pt(8)}));
}
} // namespace Hive
