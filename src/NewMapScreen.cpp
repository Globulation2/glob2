// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "NewMapScreen.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <GUIText.h>
#include <GUINumber.h>
#include <GUIButton.h>
#include <GUIList.h>
#include <GUIMessageBox.h>
using namespace GAGCore;
using namespace GAGGUI;

namespace
{
std::string tr(const char *label)
{
	return Toolkit::getStringTable()->getString(std::string("[") + label + "]");
}
constexpr Uint32 A = ALIGN_SCREEN_CENTERED;
} // namespace

NewMapScreen::NewMapScreen()
{
	methods = new List(20, 100, 280, 300, A, A, "menu");
	for (int m = 0; m <= MapGenerationDescriptor::eOLDISLANDS; ++m)
		methods->addText(tr(
			MapGenerationDescriptor::methodName(static_cast<MapGenerationDescriptor::Method>(m))));
	methods->setSelectionIndex(descriptor.method);
	addWidget(methods);
	terrains = new List(340, 100, 280, 300, A, A, "menu");
	for (const char *name : {"water", "sand", "grass"})
		terrains->addText(tr(name));
	terrains->setSelectionIndex(descriptor.terrainType);
	addWidget(terrains);

	auto addControl = [&](const MapGenerationDescriptor::Control &c, int method, int x, int y)
	{
		auto *number = new Number(x, y, 114, 18, A, A, 18, "menu");
		for (int value = c.minimum; value <= c.maximum; value += c.step)
			number->add(c.powerOfTwo ? (1 << value) : value);
		addWidget(number);
		auto *label = new Text(x + 120, y, A, A, "standard", tr(c.label));
		addWidget(label);
		controlWidgets.push_back({c, method, number, label});
	};
	for (const auto &c : MapGenerationDescriptor::sharedControls())
	{
		bool size =
			c.field == &MapGenerationDescriptor::wDec || c.field == &MapGenerationDescriptor::hDec;
		int y = c.field == &MapGenerationDescriptor::wDec	   ? 50
				: c.field == &MapGenerationDescriptor::hDec	   ? 75
				: c.field == &MapGenerationDescriptor::nbTeams ? 100
															   : 125;
		addControl(c, -1, size ? 20 : 310, y);
	}
	for (int m = 0; m <= MapGenerationDescriptor::eOLDISLANDS; ++m)
	{
		int y = 160;
		for (const auto &c :
			 MapGenerationDescriptor::controls(static_cast<MapGenerationDescriptor::Method>(m)))
		{
			bool repeat = c.group == MapGenerationDescriptor::ControlGroup::Layout;
			addControl(c, m, 310, repeat ? 75 : y);
			if (!repeat)
				y += 20;
		}
	}
	updateControls();
	addWidget(new TextButton(10, 420, 300, 40, A, A, "menu", tr("ok"), OK, 13));
	addWidget(new TextButton(330, 420, 300, 40, A, A, "menu", tr("Cancel"), CANCEL, 27));
	addWidget(new Text(0, 18, ALIGN_FILL, A, "menu", tr("create map")));
}

void NewMapScreen::updateControls()
{
	terrains->visible = descriptor.method == MapGenerationDescriptor::eUNIFORM;
	for (auto &widget : controlWidgets)
	{
		const auto &c = widget.definition;
		bool size =
			c.field == &MapGenerationDescriptor::wDec || c.field == &MapGenerationDescriptor::hDec;
		bool visible = widget.method == descriptor.method ||
					   (widget.method == -1 && (size || !terrains->visible));
		widget.number->visible = widget.label->visible = visible;
		if (visible)
			widget.number->setNth((c.get(descriptor) - c.minimum) / c.step);
	}
}

void NewMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action == BUTTON_RELEASED || action == BUTTON_SHORTCUT)
	{
		if (par1 == OK && !descriptor.hasTerrainWeight())
			MessageBox(globalContainer->gfx, "standard", MB_ONEBUTTON,
					   tr("Give at least one terrain type a nonzero weight."), tr("ok"));
		else if (par1 == OK || par1 == CANCEL)
			endExecute(par1);
	}
	else if (action==NUMBER_ELEMENT_SELECTED)
	{
		for (const auto &widget : controlWidgets)
			if (source == widget.number && widget.number->visible)
				widget.definition.set(descriptor,
									  widget.definition.minimum +
										  widget.number->getNth() * widget.definition.step);
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		if (source==terrains)
		{
			if (auto selection = terrains->selection())
				descriptor.terrainType = static_cast<TerrainType>(*selection);
		}
		else if (source == methods)
		{
			if (auto selection = methods->selection())
			{
				history.select(descriptor,
							   static_cast<MapGenerationDescriptor::Method>(*selection));
				updateControls();
			}
		}
	}
}
