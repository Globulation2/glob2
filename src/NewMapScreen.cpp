#include "GenerationContext.h"
#include "GenerationValidation.h"
#include "GeneratorRegistry.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GlobalContainer.h"
#include "NewMapScreen.h"
#include <FormatableString.h>
#include "LobbyControls.h"
#include "LandscapePickerScreen.h"
#include "GUIMapPreview.h"
#include "GenerationService.h"
#include "Game.h"
#include "gui/MobileSafeArea.h"
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

NewMapScreen::NewMapScreen(const GeneratorRegistry &registry, GAGGUI::ScreenStack *screens)
	: registry(registry), screens(screens)
{
	descriptor.setMethodDefaults(registry.methods().front(), registry);
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
		Number *number = nullptr;
		OnOffButton *toggle = nullptr;
		if (c.isToggle())
		{
			toggle = new OnOffButton(x, y, 18, 18, A, A, c.defaultValue != 0, TOGGLE);
			addWidget(toggle);
		}
		else
		{
			number = new Number(x, y, 114, 18, A, A, 18, "menu");
			for (int value : c.values())
				if (c.isChoice())
					number->add(value, tr(c.valueLabel(value)));
				else
					number->add(c.displayValue(value));
			addWidget(number);
		}
		auto *label = new Text(x + 120, y, A, A, "standard", tr(c.label));
		addWidget(label);
		controlWidgets.push_back({c, method, number, toggle, label});
	};
	for (const auto &c : GenerationRequest::sharedControls())
	{
		bool size = c.id == "width" || c.id == "height";
		int y = c.id == "width" ? 50 : c.id == "height" ? 75 : c.id == "teams" ? 100 : 125;
		addControl(c, -1, size ? 20 : 310, y);
	}
	for (int m : registry.methods())
	{
		const auto &controls = registry.at(m).controls;
		// Rows close up for generators with many controls, keeping clear of the OK and Cancel row.
		const int pitch = std::min(20, 258 / std::max(1, int(controls.size())));
		int y = 160;
		for (const auto &c : controls)
		{
			addControl(c, m, 310, y);
			y += pitch;
		}
	}
	updateControls();
	addWidget(new TextButton(10, 420, 300, 40, A, A, "menu", tr("ok"), OK, 13));
	addWidget(new TextButton(330, 420, 300, 40, A, A, "menu", tr("Cancel"), CANCEL, 27));
	addWidget(new Text(0, 18, ALIGN_FILL, A, "menu", tr("create map")));
	preview = new MapPreview(0, 0, ALIGN_LEFT, ALIGN_TOP);
	addWidget(preview);
	composition = new LobbyControls();
	composition->render = [this] { compose(); };
	addWidget(composition);
}

Widget *NewMapScreen::ControlWidget::field() const
{
	return number ? static_cast<Widget *>(number) : static_cast<Widget *>(toggle);
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
		widget.field()->visible = widget.label->visible = visible;
		if (!visible)
			continue;
		if (widget.toggle)
			widget.toggle->setState(c.get(descriptor) != 0);
		else
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
			if (widget.number && source == widget.number && widget.number->visible)
				widget.definition.set(descriptor,
									  widget.definition.valueAt(widget.number->getNth()));
	}
	else if (action == BUTTON_STATE_CHANGED)
	{
		for (const auto &widget : controlWidgets)
			if (widget.toggle && source == widget.toggle && widget.toggle->visible)
				widget.definition.set(descriptor, widget.toggle->getState() ? 1 : 0);
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

void NewMapScreen::invalidatePreview()
{
	descriptor.seed = 0;
	previewDirty = true;
	previewDue = SDL_GetTicks() + 250;
	preview->setState(MapPreview::State::Loading);
}

void NewMapScreen::onTimer(Uint32 tick)
{
	// One representative roll, debounced after parameter edits. The creation
	// operation retains its existing best-seed selection and error handling.
	if (!previewDirty || tick < previewDue)
		return;
	previewDirty = false;
	auto request = descriptor;
	request.seed = descriptor.seed ? descriptor.seed : 0x45444954;
	Game sample(nullptr);
	if (GenerationService(registry).generate(sample, request))
	{
		MapThumbnail thumbnail;
		thumbnail.loadFromMap(sample.map);
		preview->setMapThumbnail(thumbnail);
	}
	else
		preview->setState(MapPreview::State::Failed);
}

void NewMapScreen::compose()
{
	auto &ui = *composition;
	const auto safe = mobileDialogSafe(globalContainer->gfx);
	const int w = std::min(940, int(safe.w) - 24), x = int(safe.x) + (int(safe.w) - w) / 2;
	// The overview is a bounded preview card; only the settings workspace uses
	// all available height for its scrollable controls. Tablets should reveal
	// the colony below/beside the card instead of filling unused paper space.
	const bool wide = w >= 500;
	const int overviewHeight =
		84 + std::min(340, wide ? w / 2 - 20 : w) + 28 + (wide ? 0 : 132) + 8 + 48;
	const int height = std::min(int(safe.h) - 24, parameters ? int(safe.h) - 24 : overviewHeight);
	const int top = int(safe.y) + (int(safe.h) - height) / 2, bottom = top + height;
	ui.box({x - 8, top - 8, w + 16, bottom - top + 16}, Color(232, 237, 218));
	ui.text(x, top, parameters ? tr("Parameters") : tr("create map"), "menu",
			parameters ? w - 144 : w);
	const int tabs = top + 32;
	auto choose = [this](int method)
	{
		history.select(descriptor, method, registry);
		updateControls();
		invalidatePreview();
		composition->resetFocus();
	};
	if (!parameters)
	{
		ui.button(
			"blank", {x, tabs, w / 2 - 4, 44}, tr("Blank map"),
			[choose] { choose(GenerationRequest::eUNIFORM); },
			descriptor.method == GenerationRequest::eUNIFORM);
		ui.button(
			"generated", {x + w / 2 + 4, tabs, w / 2 - 4, 44}, tr("Generated"),
			[this, choose]
			{
				if (descriptor.method == GenerationRequest::eUNIFORM)
					choose(registry.methods().at(1));
				chooseLandscape();
			},
			descriptor.method != GenerationRequest::eUNIFORM);
	}
	else
		ui.button("preview", {x + w - 132, top - 4, 132, 44}, tr("Preview"),
				  [this]
				  {
					  parameters = false;
					  composition->regions[1].offset = 0;
				  });
	const int contentTop = parameters ? top + 48 : tabs + 52, footer = bottom - 48;
	const int previewSize =
		std::max(64, std::min({wide ? w / 2 - 20 : w,
							   wide ? footer - contentTop - 36 : (footer - contentTop) / 2, 340}));
	const int right = !parameters && wide ? x + w / 2 + 12 : x;
	const int fieldsW = !parameters && wide ? w / 2 - 12 : w;
	const int fieldsTop = parameters || wide ? contentTop : contentTop + previewSize + 28;
	if (!parameters)
	{
		preview->setScreenRectangle(x + (wide ? (w / 2 - previewSize) / 2 : (w - previewSize) / 2),
									contentTop, previewSize, previewSize);
		preview->paint();
		ui.text(x, contentTop + previewSize + 4,
				descriptor.seed ? tr("Selected landscape") : tr("Representative preview"), "little",
				wide ? w / 2 : w);
	}
	ui.beginRegion(1, {right, fieldsTop, fieldsW, std::max(1, footer - fieldsTop - 8)});
	int row = fieldsTop - ui.regions[1].offset, start = row;
	if (!parameters)
	{
		if (descriptor.method != GenerationRequest::eUNIFORM)
		{
			ui.button("landscape", {right, row, fieldsW - 8, 48},
					  GAGCore::FormattableString(tr("%0 / Browse"))
						  .arg(tr(registry.at(descriptor.method).nameKey)),
					  [this] { chooseLandscape(); });
			row += 56;
		}
		else
		{
			ui.text(right, row, tr("Starting terrain"), "standard", fieldsW);
			row += 24;
			std::vector<std::string> names{tr("water"), tr("sand"), tr("grass")};
			ui.dropdown("terrain", {right, row, fieldsW - 8, 48}, names, descriptor.terrainType,
						[this](int i)
						{
							descriptor.terrainType = TerrainType(i);
							invalidatePreview();
						});
			row += 56;
		}
		ui.button(
			"parameters", {right, row, fieldsW - 8, 44},
			parameters ? tr("Hide parameters") : tr("Size and parameters"),
			[this]
			{
				parameters = !parameters;
				composition->regions[1].offset = 0;
			},
			parameters);
		row += 52;
	}
	if (parameters)
		for (auto &widget : controlWidgets)
		{
			const auto c = widget.definition;
			const bool shared =
				widget.method == -1 && (descriptor.method != GenerationRequest::eUNIFORM ||
										c.id == "width" || c.id == "height");
			if (widget.method != descriptor.method && !shared)
				continue;
			const int labelWidth = std::max(90, fieldsW / 2 - 16);
			const int labelHeight = c.isToggle()
										? 0
										: ui.paragraph(right, row + 10, labelWidth, tr(c.label),
													   "standard", false, false);
			const int rowHeight = std::max(48, labelHeight + 16);
			if (c.isToggle())
				ui.checkbox(c.id, {right, row, fieldsW - 8, 44}, tr(c.label),
							c.get(descriptor) != 0,
							[this, c](bool value)
							{
								c.set(descriptor, value);
								invalidatePreview();
							});
			else
			{
				std::vector<std::string> values;
				for (int value : c.values())
					values.push_back(c.isChoice() ? tr(c.valueLabel(value))
												  : std::to_string(c.displayValue(value)));
				ui.dropdown(c.id, {right + fieldsW / 2, row, fieldsW / 2 - 8, rowHeight}, values,
							c.indexOf(c.get(descriptor)),
							[this, c](int index)
							{
								c.set(descriptor, c.valueAt(index));
								invalidatePreview();
							});
			}
			row += rowHeight + 8;
		}
	ui.endRegion(row - start);
	ui.button("cancel", {x, footer, w / 2 - 4, 48}, tr("Cancel"), [this] { endExecute(CANCEL); });
	ui.button(
		"create", {x + w / 2 + 4, footer, w / 2 - 4, 48}, tr("create map"),
		[this] { onAction(nullptr, BUTTON_RELEASED, OK, 0); }, true);
}

void NewMapScreen::drawExecution()
{
	if (!isExecutionRunning())
		return;
	Style::style->onFrame();
	if (FrontendTheme::current)
		FrontendTheme::current->background(gfx, false);
	composition->paint();
	globalContainer->gfx->nextFrame();
}
void NewMapScreen::handleExecutionEvent(SDL_Event event)
{
	if (composition->handle(&event))
		return;
	if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
		endExecute(CANCEL);
	else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RETURN)
		onAction(nullptr, BUTTON_SHORTCUT, OK, 0);
}
void NewMapScreen::cancelExecutionInput()
{
	composition->cancelTouch();
}

LandscapePickerScreen *NewMapScreen::chooseLandscape()
{
	if (!screens)
		return nullptr;
	std::vector<LandscapePickerScreen::Entry> entries;
	int selected = 0;
	for (int method : registry.methods())
	{
		if (method == GenerationRequest::eUNIFORM)
			continue;
		auto request = descriptor;
		if (method != descriptor.method)
		{
			request.setMethodDefaults(method, registry);
			request.wDec = descriptor.wDec;
			request.hDec = descriptor.hDec;
			request.nbTeams = descriptor.nbTeams;
		}
		if (method == descriptor.method)
			selected = int(entries.size());
		entries.push_back(
			{tr(registry.at(method).nameKey), request, method, registry.at(method).tags});
	}
	auto picker = std::make_unique<LandscapePickerScreen>(tr("Choose a landscape"),
														  std::move(entries), selected);
	auto *result = picker.get();
	screens->push(std::move(picker),
				  [this](Screen &screen, int result)
				  {
					  auto &picker = static_cast<LandscapePickerScreen &>(screen);
					  if (result < 0)
						  return;
					  descriptor = picker.chosenRequest();
					  descriptor.seed = picker.chosenSeed().value_or(0);
					  updateControls();
					  previewDirty = true;
					  previewDue = 0;
				  });
	return result;
}
