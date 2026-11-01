// SPDX-License-Identifier: GPL-3.0-or-later
#include "PhoneEditor.h"
#include "MapEdit.h"
#include "GlobalContainer.h"
#include "ui/FrontendUI.h"
#include <cmath>

using namespace GAGCore;
namespace ui = GAGGUI::ui;
namespace
{
ui::Rect rect(ViewRect r)
{
	const int x = int(std::floor(r.x)), y = int(std::floor(r.y));
	return {x, y, std::max(0, int(std::ceil(r.x + r.w)) - x),
			std::max(0, int(std::ceil(r.y + r.h)) - y)};
}
} // namespace
ui::Host &PhoneEditor::inspectionHost()
{
	prepare();
	if (!controlsHost)
		controlsHost = std::make_unique<ui::Host>(
			Glob2UI::inGameTheme(), [this](const ui::Presentation &) { return buildControls(); });
	controlsHost->setPresentation(ui::resolvePresentation(*globalContainer->gfx));
	controlsHost->invalidate();
	controlsHost->layoutIfNeeded();
	return *controlsHost;
}
ui::Element PhoneEditor::buildControls()
{
	std::vector<ui::PositionedControl> controls;
	for (auto &target : controlTargets())
		controls.push_back(std::move(target.control));
	return ui::positionedControls(std::move(controls));
}
void PhoneEditor::scrollSurface(bool propertySheet, int pixels)
{
	stopScrolling();
	if (propertySheet)
	{
		inspectorScroll = std::clamp(inspectorScroll + double(pixels), 0., inspectorMaximum);
		syncInspector();
	}
	else
	{
		offset = std::clamp(offset + double(pixels), 0., maximum);
		syncTray();
	}
}
std::vector<PhoneEditor::Target> PhoneEditor::controlTargets()
{
	const double u = globalContainer->gfx->logicalUnitsPerPoint();
	std::vector<Target> targets;
	auto add = [&](int id, std::string key, std::string label, ViewRect bounds,
				   std::optional<ViewRect> clip = {},
				   bool actionable = true) -> ui::PositionedControl &
	{
		ui::PositionedControl control;
		control.key = std::move(key);
		control.label = std::move(label);
		control.bounds = rect(bounds);
		if (clip)
			control.clip = rect(*clip);
		if (actionable)
			control.tap = [this, id](ui::Point p)
			{ activateTarget(id, {double(p.x), double(p.y)}); };
		else
			control.actions.clear();
		targets.push_back({id, std::move(control)});
		return targets.back().control;
	};
	auto scroll = [&](std::string key, ViewRect area, bool propertySheet)
	{
		auto &control =
			add(propertySheet ? -22 : -23, std::move(key),
				propertySheet ? "Inspector properties" : "Brush palette", area, {}, false);
		control.role = "scroll";
		control.value = int(propertySheet ? inspectorScroll : offset);
		control.actions = {ui::ActionKind::Scroll};
		control.perform = [this, propertySheet](const ui::SemanticAction &action)
		{
			const auto *pixels = std::get_if<int>(&action.value);
			if (action.kind != ui::ActionKind::Scroll || !pixels)
				return false;
			scrollSurface(propertySheet, *pixels);
			return true;
		};
	};
	auto &map = add(-1, "editor/map", "Map canvas", content, {}, false);
	map.role = "canvas";
	if (peekOpen)
	{
		add(-304, "editor/peek/backdrop", "Close map preview", safe);
		auto &mini = add(-300, "editor/minimap", "Map preview canvas", peekRect());
		mini.role = "canvas";
		const char *labels[] = {"Done", "Zoom out", "Zoom in"};
		const char *keys[] = {"done", "out", "in"};
		const auto buttons = peekButtons();
		for (int i = 0; i < int(buttons.size()); ++i)
			add(-301 - i, std::string("editor/peek/") + keys[i], labels[i], buttons[i]);
		return targets;
	}
	if (showsMapButton())
		add(-305, "editor/show-minimap", "Map", mapButton());
	if (inspecting())
	{
		add(-22, "editor/inspector", "Selected object", inspector, {}, false).role = "group";
		add(-2, "editor/menu", "Menu", {safe.x, safe.y, 88 * u, 44 * u});
		add(-20, "editor/inspector/close", "Close inspector",
			{inspector.x + inspector.w - 48 * u, inspector.y + 4 * u, 44 * u, 44 * u});
		scroll("editor/inspector/scroll", inspectorBody, true);
		for (size_t i = 0; i < properties.size(); ++i)
		{
			const auto &property = properties[i];
			const auto r = property.rect;
			// Painting and human input intentionally expose complete property rows only.
			if (r.y < inspectorBody.y || r.y + r.h > inspectorBody.y + inspectorBody.h)
				continue;
			const std::string key =
				"editor/inspect/" + std::to_string(inspectorIdentity) + "/" + property.key;
			add(1000 + int(i) * 3, key + "/minus", "Decrease " + property.caption,
				{r.x, r.y + 24 * u, 44 * u, 44 * u});
			add(1002 + int(i) * 3, key + "/plus", "Increase " + property.caption,
				{r.x + r.w - 44 * u, r.y + 24 * u, 44 * u, 44 * u});
			auto &value = add(1001 + int(i) * 3, key, property.caption,
							  {r.x + 48 * u, r.y + 24 * u, r.w - 96 * u, 44 * u});
			value.role = "slider";
			value.value = property.value->currentValue();
			value.actions = {ui::ActionKind::SetValue};
			value.perform = [this, binding = property.value](const ui::SemanticAction &action)
			{
				const auto *requested = std::get_if<int>(&action.value);
				if (action.kind != ui::ActionKind::SetValue || !requested || *requested < 0 ||
					*requested > binding->maximumValue())
					return false;
				binding->setValue(*requested);
				editor.hasMapBeenModified = true;
				return true;
			};
		}
		for (auto &target : targets)
			target.control.identity = "editor-inspector:" + std::to_string(inspectorIdentity) +
									  ":" + std::to_string(inspectorGeneration);
		return targets;
	}
	const auto labels = headerLabels();
	for (int i = 0; i < 4; ++i)
	{
		const char *keys[] = {"menu", "team", "level", "selection"};
		auto &control = add(-2 - i, std::string("editor/") + keys[i], labels[i],
							headerButtons[i], {},
							i != 1 || paletteMode >= 2);
		if (i == 1 && paletteMode >= 2)
			control.value = editor.team;
		if (i == 2 && paletteMode >= 2)
			control.value = editor.selectionMode == MapEdit::PlaceUnit ? editor.placingUnitLevel
																	   : editor.buildingLevel;
	}
	if (!tools)
		add(-6, "tray/show", "Show palette", modeBar);
	else
	{
		const char *labels[] = {"Terrain", "Resources", "Buildings", "Flags and units", "Teams"};
		for (int i = 0; i < modeCount; ++i)
			add(-10 - i, "tray/mode/" + std::to_string(i), labels[i],
				modeRect(i))
				.selected = paletteMode == i;
		if (editor.panelMode == MapEdit::Teams)
			add(-21, "editor/teams", "Edit teams", tray);
		else
		{
			scroll("tray/scroll", cardBar, false);
			if (!chips.empty())
				add(-23, "tray/groups", "Palette groups", chipBar, {}, false).role = "group";
			for (size_t i = 0; i < chips.size(); ++i)
			{
				auto &control = add(-400 - int(i), "tray/chip/" + std::to_string(i), chips[i].title,
									chips[i].rect, chipBar);
				control.selected = activeChip() == int(i);
				control.identity =
					"catalog:" + std::to_string(editor.catalogRevision()) + ":" + chips[i].id;
			}
			for (size_t i = 0; i < rows.size(); ++i)
			{
				const auto &row = rows[i];
				std::string label;
				for (const auto &line : row.lines)
					label += (label.empty() ? "" : " ") + line;
				auto &control = add(int(i), "tray/" + row.id, label, row.rect, cardBar);
				control.identity =
					"catalog:" + std::to_string(editor.catalogRevision()) + ":" + row.id;
				if (row.kind == Row::Kind::Widget && editor.areaNumber)
					control.identity += ":area:" + std::to_string(editor.areaNumber->getIndex());
				control.selected = row.id == editor.currentBrushId();
				if (row.kind == Row::Kind::Fertility)
					control.selected = editor.isFertilityOn;
				if (row.id == "area/number" && editor.areaNumber)
					control.value = editor.areaNumber->getIndex();
			}
		}
	}
	if (paintMode())
	{
		const auto layout = rail();
		for (size_t i = 0; i < layout.detents.size(); ++i)
			add(-200 - int(i), "editor/brush/shape/" + std::to_string(i),
				"Brush " + std::to_string(BrushTool::getBrushWidth(i)) + "x" +
					std::to_string(BrushTool::getBrushHeight(i)),
				layout.detents[i], content)
				.selected = editor.brush.getFigure() == i;
		if (layout.pan.w > 0)
			add(-210, "editor/brush/pan", "Pan", layout.pan, content).selected = pan;
		if (layout.mode.w > 0)
			add(-211, "editor/brush/mode", "Paint or erase", layout.mode, content).selected =
				editor.brush.getType() == BrushTool::MODE_DEL;
		if (layout.undo.w > 0)
			add(-213, "editor/brush/undo", "Undo stroke", layout.undo, content);
	}
	return targets;
}
