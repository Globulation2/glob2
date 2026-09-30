// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "NewMapScreen.h"
#include "GUIMapPreview.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GenerationValidation.h"
#include "LandscapePickerScreen.h"
#include <FormatableString.h>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

namespace
{
std::string tr(const char *label) { return fe::tr(std::string("[") + label + "]"); }
} // namespace

NewMapScreen::NewMapScreen(const GeneratorRegistry &registry, GAGGUI::ScreenStack *screens) : registry(registry), screens(screens)
{
	descriptor.setMethodDefaults(registry.methods().front(), registry);
	preview = std::make_unique<MapPreview>();
}

NewMapScreen::~NewMapScreen() = default;

void NewMapScreen::chooseMethod(int method)
{
	history.select(descriptor, method, registry);
	invalidatePreview();
}

void NewMapScreen::invalidatePreview()
{
	descriptor.seed = 0;
	previewDirty = true;
	previewDue = SDL_GetTicks() + 250;
	preview->setState(MapPreview::State::Loading);
	error.clear();
	invalidate();
}

void NewMapScreen::create()
{
	error = validateGenerationRequest(descriptor, registry.at(descriptor.method));
	if (!error.empty())
	{
		invalidate();
		return;
	}
	endExecute(OK);
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
	invalidate();
}

Element NewMapScreen::build(const Presentation &p)
{
	const bool blank = descriptor.method == GenerationRequest::eUNIFORM;
	std::vector<Element> fields;
	fields.push_back(fe::segments("mode", {tr("Blank map"), tr("Generated")}, blank ? 0 : 1,
								  [this](int value)
								  {
									  if (value == 0)
										  chooseMethod(GenerationRequest::eUNIFORM);
									  else
									  {
										  if (descriptor.method == GenerationRequest::eUNIFORM)
											  chooseMethod(registry.methods().at(1));
										  chooseLandscape();
									  }
								  }));
	if (!blank)
		fields.push_back(fe::field(tr("Landscape"),
								   fe::chooser("landscape", GAGCore::FormattableString(tr("%0 / Browse")).arg(tr(registry.at(descriptor.method).nameKey)),
											   [this] { chooseLandscape(); })));
	else
		fields.push_back(fe::field(tr("Starting terrain"), fe::choice("terrain", {tr("water"), tr("sand"), tr("grass")}, descriptor.terrainType,
																		[this](int i)
																		{
																			descriptor.terrainType = TerrainType(i);
																			invalidatePreview();
																		})));
	fe::ButtonOptions toggleOptions;
	toggleOptions.selected = parameters;
	fields.push_back(fe::button("parameters", parameters ? tr("Hide parameters") : tr("Size and parameters"), [this] { parameters = !parameters; }, toggleOptions));
	if (parameters)
	{
		auto control = [&](const GenerationRequest::Control &c)
		{
			if (c.isToggle())
			{
				fields.push_back(fe::toggle(c.id, tr(c.label), c.get(descriptor) != 0,
											[this, c](bool value)
											{
												c.set(descriptor, value);
												invalidatePreview();
											}));
				return;
			}
			std::vector<std::string> values;
			for (int value : c.values())
				values.push_back(c.isChoice() ? tr(c.valueLabel(value)) : std::to_string(c.displayValue(value)));
			fields.push_back(fe::field(tr(c.label), fe::choice(c.id, values, c.indexOf(c.get(descriptor)),
															   [this, c](int index)
															   {
																   c.set(descriptor, c.valueAt(index));
																   invalidatePreview();
															   })));
		};
		for (const auto &c : GenerationRequest::sharedControls())
			if (!blank || c.id == "width" || c.id == "height")
				control(c);
		if (!blank)
			for (const auto &c : registry.at(descriptor.method).controls)
				control(c);
	}
	if (!error.empty())
		fields.push_back(fe::paragraph(error, {fe::FontRole::Support}));
	auto form = fe::column(std::move(fields), {p.pt(8)});
	auto previewElement = fe::column({fe::center(fe::mapPreview("preview", *preview, 300)),
									  fe::caption(descriptor.seed ? tr("Selected landscape") : tr("Representative preview"))},
									 {p.pt(4)});
	Element body = fe::adaptive(
		[form, previewElement](const fe::LayoutContext &ctx, fe::Size available)
		{
			if (available.w < ctx.presentation.pt(640))
				return fe::scroll("newmap/scroll", fe::column({previewElement, form}, {ctx.presentation.pt(12)}));
			return fe::row({fe::expanded(fe::scroll("newmap/scroll", form)), fe::width(ctx.presentation.pt(320), previewElement)},
						   {ctx.presentation.pt(16), fe::CrossAlign::Start});
		});
	return fe::page(tr("create map"), body,
					fe::actions({{"create", tr("create map"), [this] { create(); }, true, SDLK_RETURN},
								 {"cancel", tr("Cancel"), [this] { endExecute(CANCEL); }, false, SDLK_ESCAPE}},
								p),
					p, 940);
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
		entries.push_back({tr(registry.at(method).nameKey), request, method, registry.at(method).tags});
	}
	auto picker = std::make_unique<LandscapePickerScreen>(tr("Choose a landscape"), std::move(entries), selected);
	auto *result = picker.get();
	screens->push(std::move(picker),
				  [this](GAGGUI::Screen &screen, int result)
				  {
					  auto &picker = static_cast<LandscapePickerScreen &>(screen);
					  if (result < 0)
						  return;
					  descriptor = picker.chosenRequest();
					  descriptor.seed = picker.chosenSeed().value_or(0);
					  previewDirty = true;
					  previewDue = 0;
					  invalidate();
				  });
	return result;
}
