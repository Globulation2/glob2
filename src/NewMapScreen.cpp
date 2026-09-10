#include "GenerationContext.h"
#include "GenerationValidation.h"
#include "GeneratorRegistry.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GlobalContainer.h"
#include "NewMapScreen.h"
#include <GUIButton.h>
#include <GUIList.h>
#include <GUIMessageBox.h>
#include <GUINumber.h>
#include <GUIText.h>
#include <StringTable.h>
#include <Toolkit.h>
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

NewMapScreen::NewMapScreen(const GeneratorRegistry &registry) : registry(registry)
{
	methods = new List(20, 100, 280, 300, A, A, "menu");
	for (int m : registry.methods())
		methods->addText(tr(registry.at(m).nameKey));
	methods->setSelectionIndex(registry.selectionIndex(descriptor.method));
	addWidget(methods);
	terrains = new List(340, 100, 280, 300, A, A, "menu");
	for (const char *name : {"water", "sand", "grass"})
		terrains->addText(tr(name));
	terrains->setSelectionIndex(descriptor.terrainType);
	addWidget(terrains);

	auto addControl = [&](const GenerationRequest::Control &c, int method, int x, int y)
	{
		auto *number = new Number(x, y, 114, 18, A, A, 18, "menu");
		for (int value : c.values())
			number->add(c.displayValue(value));
		addWidget(number);
		auto *label = new Text(x + 120, y, A, A, "standard", tr(c.label));
		addWidget(label);
		controlWidgets.push_back({c, method, number, label});
	};
	for (const auto &c : GenerationRequest::sharedControls())
	{
		bool size = c.id == "width" || c.id == "height";
		int y = c.id == "width" ? 50 : c.id == "height" ? 75 : c.id == "teams" ? 100 : 125;
		addControl(c, -1, size ? 20 : 310, y);
	}
	for (int m : registry.methods())
	{
		int y = 160;
		for (const auto &c : registry.at(m).controls)
		{
			addControl(c, m, 310, y);
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
	terrains->visible = descriptor.method == GenerationRequest::eUNIFORM;
	for (auto &widget : controlWidgets)
	{
		const auto &c = widget.definition;
		bool size = c.id == "width" || c.id == "height";
		bool visible = widget.method == descriptor.method ||
					   (widget.method == -1 && (size || !terrains->visible));
		widget.number->visible = widget.label->visible = visible;
		if (visible)
			widget.number->setNth(c.indexOf(c.get(descriptor)));
	}
}

void NewMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action == BUTTON_RELEASED || action == BUTTON_SHORTCUT)
	{
		const auto error =
			par1 == OK ? validateGenerationRequest(descriptor, registry.at(descriptor.method))
					   : std::string{};
		if (!error.empty())
			MessageBox(globalContainer->gfx, "standard", MB_ONEBUTTON, error, tr("ok"));
		else if (par1 == OK || par1 == CANCEL)
			endExecute(par1);
	}
	else if (action == NUMBER_ELEMENT_SELECTED)
	{
		for (const auto &widget : controlWidgets)
			if (source == widget.number && widget.number->visible)
				widget.definition.set(descriptor,
									  widget.definition.valueAt(widget.number->getNth()));
	}
	else if (action == LIST_ELEMENT_SELECTED)
	{
		if (source == terrains)
		{
			if (auto selection = terrains->selection())
				descriptor.terrainType = static_cast<TerrainType>(*selection);
		}
		else if (source == methods)
		{
			if (auto selection = methods->selection())
			{
				history.select(descriptor, registry.methods().at(*selection), registry);
				updateControls();
			}
		}
	}
}
