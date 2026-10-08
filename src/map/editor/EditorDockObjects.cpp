// SPDX-License-Identifier: GPL-3.0-or-later
// The editor dock's Teams tab, active-team picker and the inspector shown while
// a unit or building on the map is selected.

#include "EditorDock.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "MapEditInspector.h"
#include "TeamDisplay.h"
#include <FormatableString.h>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

Element EditorDock::teamPicker(const Presentation &p)
{
	std::vector<Element> swatches;
	const int side = p.pt(p.touch ? 48 : 28);
	for (int i = 0; editor.view.scene && i < editor.view.scene->entities.teamCount; ++i)
	{
		const auto *team = &editor.view.scene->entities.teams[i];
		if (!team->mask)
			continue;
		fe::ButtonOptions options;
		options.selected = editor.team == i;
		options.minHeight = p.touch ? 48 : 28;
		options.accessibleLabel = GAGCore::FormattableString(fe::tr("[Team %0]")).arg(i + 1);
		options.tooltip = options.accessibleLabel;
		swatches.push_back(fe::stack({fe::button("dock/team/" + std::to_string(i), "",
												 [this, i]
												 {
													 editor.selectActiveTeam(i);
													 invalidate();
												 },
												 options),
									  fe::center(fe::swatch(presentationColor(team->color), p.touch ? 28 : 16))}));
	}
	fe::WrapOptions grid;
	grid.gap = p.pt(3);
	grid.minChildWidth = side;
	grid.maxColumns = 8;
	return fe::column({fe::caption(fe::tr("[dock active team]")), fe::wrap(std::move(swatches), grid)}, {p.pt(3)});
}

Element EditorDock::teamsTab(const Presentation &p)
{
	const int count = editor.view.scene ? editor.view.scene->entities.teamCount : 0;
	std::vector<Element> rows;
	for (int i = 0; editor.view.scene && i < editor.view.scene->entities.teamCount; ++i)
	{
		const auto *team = &editor.view.scene->entities.teams[i];
		if (!team->mask)
			continue;
		fe::ButtonOptions options;
		options.selected = editor.team == i;
		options.alignLeft = true;
		options.flat = editor.team != i;
		options.minHeight = p.touch ? 48 : 30;
		const std::string label = GAGCore::FormattableString(fe::tr("[Team %0]")).arg(i + 1);
		rows.push_back(fe::stack({fe::button("dock/teams/select/" + std::to_string(i), "",
											 [this, i]
											 {
												 editor.selectActiveTeam(i);
												 invalidate();
											 },
											 [&]
											 {
												 auto o = options;
												 o.accessibleLabel = label;
												 return o;
											 }()),
								  fe::padding(fe::Insets::symmetric(p.pt(8), 0),
											  fe::row({fe::swatch(presentationColor(team->color), 18), fe::label(label)},
													  {p.pt(8), fe::CrossAlign::Center}))}));
	}
	fe::ButtonOptions add, remove, editTeams;
	add.icon = fe::uiIcon(fe::UIIcon::Plus);
	add.enabled = count < Team::MAX_COUNT;
	remove.enabled = count > 1;
	remove.danger = true;
	editTeams.icon = fe::uiIcon(fe::UIIcon::Users);
	auto changed = [this](const char *action)
	{
		return [this, action]
		{
			editor.performAction(action);
			invalidate();
		};
	};
	return fe::column(
		{fe::paragraph(GAGCore::FormattableString(fe::tr("[dock team count %0]")).arg(count), {fe::FontRole::Support, true}),
		 fe::row({fe::expanded(fe::button("dock/teams/add", fe::tr("[dock add team]"), changed("add team"), add)),
				  fe::expanded(fe::button("dock/teams/remove", fe::tr("[dock remove team]"), changed("remove team"), remove))},
				 {p.pt(6)}),
		 fe::column(std::move(rows), {p.pt(2)}),
		 fe::button("dock/teams/editor", fe::tr("[open teams editor]"), changed("open teams editor"), editTeams)},
		{p.pt(8)});
}

Element EditorDock::inspector(const Presentation &p)
{
	const auto model = buildInspectorModel(editor);
	if (model.kind == InspectorModel::Kind::None)
		return fe::hint(fe::tr("[dock nothing selected]"));
	const int side = p.pt(56);
	auto picture = fe::canvas("dock/inspect/picture", {side, side},
							  [this](fe::Canvas &canvas, fe::Rect r, const fe::Frame &)
							  {
								  canvas.fillRounded(r, host().metrics().radius, theme().palette.field);
								  paintInspectorPicture(canvas, r.inset(4), buildInspectorModel(editor));
							  });
	fe::ButtonOptions close;
	close.icon = fe::uiIcon(fe::UIIcon::Close);
	close.accessibleLabel = fe::tr("[Close]");
	close.tooltip = close.accessibleLabel;
	close.shortcut = SDLK_UNKNOWN;
	auto closeButton = fe::button("dock/inspect/close", "",
								  [this]
								  {
									  editor.performAction("unselect");
									  showTab(lastTab);
								  },
								  close);
	auto titleBlock = fe::column({fe::heading(model.title), fe::caption(model.detail)}, {p.pt(2)});
	auto top = fe::row({fe::sized({side, side}, picture), fe::expanded(titleBlock), closeButton},
					   {p.pt(8), fe::CrossAlign::Center});
	std::vector<Element> parts{top, fe::divider()};
	if (model.rows.empty())
		parts.push_back(fe::hint(fe::tr("[dock no properties]")));
	else
		parts.push_back(inspectorRows(model, p, "dock/inspect", [this] { invalidate(); }));
	return fe::column(std::move(parts), {p.pt(8)});
}
