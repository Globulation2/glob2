// SPDX-License-Identifier: GPL-3.0-or-later
#include "LandscapePickerScreen.h"
#include "GlobalContainer.h"
#include "LobbyControls.h"
#include "GenerationContext.h"
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <functional>

namespace
{
std::string tr(const std::string &s)
{
	return Toolkit::getStringTable()->getString("[" + s + "]");
}
} // namespace

std::vector<GenerationRequest> LandscapePickerScreen::requestsOf(const std::vector<Entry> &entries)
{
	std::vector<GenerationRequest> requests;
	for (const auto &entry : entries)
		requests.push_back(entry.request);
	return requests;
}

LandscapePickerScreen::LandscapePickerScreen(const std::string &title, std::vector<Entry> entries,
											 int selected)
	: title(title), entries(std::move(entries)), tiles(this->entries.size()),
	  redraws(this->entries.size(), 0),
	  selected(std::clamp(selected, 0, std::max(0, int(this->entries.size()) - 1))),
	  previewer(requestsOf(this->entries))
{
	gfx = globalContainer->gfx;
	controls = new LobbyControls();
	controls->render = [this] { render(); };
	controls->focus = "landscape/" + std::to_string(this->selected);
	addWidget(controls);
}

LandscapePickerScreen::~LandscapePickerScreen() = default;

void LandscapePickerScreen::onAction(Widget *, Action, int, int) {}

std::optional<std::uint32_t> LandscapePickerScreen::chosenSeed() const
{
	if (selected < 0 || selected >= int(tiles.size()))
		return {};
	const auto &preview = tiles[selected].preview;
	if (preview.state != LandscapePreviewer::State::Ready)
		return {};
	return preview.seed;
}

GenerationRequest LandscapePickerScreen::chosenRequest() const
{
	if (selected < 0 || selected >= int(entries.size()))
		return GenerationRequest();
	return previewer.request(std::size_t(selected));
}

void LandscapePickerScreen::randomizeParameters()
{
	std::vector<GenerationRequest> requests;
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		GenerationRequest request = entries[i].request;
		// Each landscape draws from its own stream; a draw the generator refuses up front is
		// redrawn inside randomizeControls, so what goes to the workers is at least a valid
		// request. Should no draw at all be accepted, the entry keeps its own parameters.
		request.randomizeControls(
			GenerationContext::deriveSeed(GenerationContext::randomSeed(), "random/" + std::to_string(i)));
		redraws[i] = kRandomDraws;
		requests.push_back(request);
	}
	previewer.restart(std::move(requests));
}

void LandscapePickerScreen::resetParameters()
{
	// FEEDBACK 2026-09-14: "we will also need a 'reset to defaults' button beside the 'randomize
	// parameters'". Every landscape on its registered controls at the sheet's size, colony count
	// and workers; nothing to redraw, since the defaults are known to make maps.
	std::vector<GenerationRequest> requests;
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		const GenerationRequest &shown = entries[i].request;
		GenerationRequest request;
		request.setMethodDefaults(shown.method);
		request.wDec = shown.wDec;
		request.hDec = shown.hDec;
		request.nbTeams = shown.nbTeams;
		request.nbWorkers = shown.nbWorkers;
		request.terrainType = shown.terrainType;
		redraws[i] = 0;
		requests.push_back(request);
	}
	previewer.restart(std::move(requests));
}

void LandscapePickerScreen::select(int index)
{
	if (index < 0 || index >= int(entries.size()))
		return;
	selected = index;
	controls->focus = "landscape/" + std::to_string(index);
	reveal = true;
}

void LandscapePickerScreen::confirm()
{
	if (selected >= 0 && selected < int(entries.size()))
		endExecute(selected);
}

void LandscapePickerScreen::onTimer(Uint32)
{
    previewer.poll();
	refresh();
}

void LandscapePickerScreen::refresh()
{
	for (std::size_t i = 0; i < tiles.size(); ++i)
	{
		auto &tile = tiles[i];
		const unsigned revision = previewer.revision(i);
		if (revision == tile.revision)
			continue;
		tile.revision = revision;
		tile.preview = previewer.preview(i);
		tile.surface.reset();
		// A random set of parameters the world refused (every seed failed) is drawn again while
		// draws remain: the sheet should show maps, not failures, and the user asked for variety.
		if (tile.preview.state == LandscapePreviewer::State::Failed && redraws[i] > 0)
		{
			--redraws[i];
			GenerationRequest request = entries[i].request;
			request.randomizeControls(GenerationContext::deriveSeed(
				GenerationContext::randomSeed(), "redraw/" + std::to_string(i)));
			previewer.reroll(i, request);
			continue;
		}
		if (tile.preview.state == LandscapePreviewer::State::Ready)
		{
			// Surfaces belong to the UI thread; the worker only hands over pixels.
			tile.surface =
				std::make_unique<DrawableSurface>(MapPreview::PreviewSize, MapPreview::PreviewSize);
			tile.preview.thumbnail.loadIntoSurface(tile.surface.get());
		}
	}
}

void LandscapePickerScreen::onSDLEvent(SDL_Event *event)
{
	if (event->type == SDL_KEYDOWN && !controls->popup.open)
	{
		const auto key = event->key.keysym.sym;
		if (key == SDLK_ESCAPE)
		{
			endExecute(CANCEL);
			return;
		}
		const int step = key == SDLK_LEFT    ? -1
						 : key == SDLK_RIGHT ? 1
						 : key == SDLK_UP    ? -columns
						 : key == SDLK_DOWN  ? columns
											 : 0;
		if (step != 0)
		{
			select(std::clamp(selected + step, 0, int(entries.size()) - 1));
			return;
		}
		// Return on a tile, or with nothing focused, confirms; on a button it presses that.
		if (key == SDLK_RETURN &&
			(controls->focus.empty() || controls->focus == "landscape/" + std::to_string(selected)))
		{
			confirm();
			return;
		}
	}
	controls->handle(event);
}

void LandscapePickerScreen::render()
{
	auto &ui = *controls;
	const int width = gfx->getW(), height = gfx->getH();
	const int w = std::min(width - 32, 1120), x = (width - w) / 2;
	const bool compact = width < 800;
	ui.setScreenPosition(0, 0);
	ui.setDimensions(width, height);
	ui.box({x - 8, 8, w + 16, height - 16}, Color(232, 237, 218), 8);
	ui.text(x + 8, 20, title, compact ? "standard" : "menu", w - 16);
	if (previewer.busy())
	{
		const std::string status = tr("Generating preview...") + "  " +
								   std::to_string(previewer.finished()) + " / " +
								   std::to_string(previewer.size());
		const int sw = Toolkit::getFont("little")->getStringWidth(status);
		ui.text(x + w - 8 - sw, compact ? 26 : 30, status, "little", sw + 2, true);
	}
	const int subtitleY = compact ? 46 : 56;
	const int top = subtitleY + 6 +
					ui.paragraph(x + 8, subtitleY, w - 16,
								 tr("Each landscape is shown as a real map at your current size "
									"and colony count. Use one to play the map shown."));
	const int bottom = height - 66;
	const int gap = 12, scrollbar = 12;
	const int tileMin = compact ? 176 : 200;
	columns =
		std::clamp((w - scrollbar + gap) / (tileMin + gap), 1, std::max(1, int(entries.size())));
	const int tileW = (w - scrollbar - gap * (columns - 1)) / columns;
	const int image = tileW - 16;
	const int nameH = Toolkit::getFont("standard")->getStringHeight("Ag");
	const int noteH = Toolkit::getFont("little")->getStringHeight("Ag");
	const int tileH = 8 + image + 8 + nameH + 4 + noteH + 8;
	const int stride = tileH + gap;
	const int rows = (int(entries.size()) + columns - 1) / columns;
	auto &region = ui.regions[30];
	if (reveal && selected >= 0)
	{
		// Keep the selection in view, for keyboard moves and the initial choice.
		const int ty = (selected / columns) * stride, viewH = bottom - top;
		region.maximum = std::max(0, rows * stride - gap + 4 - viewH);
		if (ty < region.offset)
			region.offset = ty;
		else if (ty + tileH > region.offset + viewH)
			region.offset = ty + tileH - viewH;
		region.offset = std::clamp(region.offset, 0, region.maximum);
		reveal = false;
	}
	ui.beginRegion(30, {x, top, w, bottom - top});
	const int offset = region.offset;
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		const int column = int(i) % columns, row = int(i) / columns;
		const SDL_Rect r{x + column * (tileW + gap), top + row * stride - offset, tileW, tileH};
		const bool current = int(i) == selected;
		ui.button(
			"landscape/" + std::to_string(i), r, "",
			[this, i]
			{
				if (int(i) == selected)
					confirm();
				else
					select(int(i));
			},
			current);
		const SDL_Rect frame{r.x + 8, r.y + 8, image, image};
		ui.box(frame, Color(211, 223, 197), 3);
		const auto &tile = tiles[i];
		std::string note;
		if (tile.preview.state == LandscapePreviewer::State::Ready && tile.surface)
		{
			SDL_Rect map = frame;
			if (tile.preview.width >= tile.preview.height)
				map.h = std::max(1, image * tile.preview.height / tile.preview.width);
			else
				map.w = std::max(1, image * tile.preview.width / tile.preview.height);
			map.x += (image - map.w) / 2;
			map.y += (image - map.h) / 2;
			drawMapThumbnail(ui.surface(), map, tile.surface.get(), tile.preview.width,
							 tile.preview.height, tile.preview.starts, compact ? 12 : 14);
			note = std::to_string(tile.preview.width) + " x " +
				   std::to_string(tile.preview.height) + "  /  " +
				   std::to_string(tile.preview.starts.size()) + " " + tr("colonies");
		}
		else
		{
			note = tr(tile.preview.state == LandscapePreviewer::State::Failed
						  ? "Preview unavailable"
						  : "Generating preview...");
			ui.paragraph(frame.x + 12, frame.y + image / 2 - noteH, image - 24, note);
		}
		ui.text(r.x + 8, r.y + 8 + image + 8, entries[i].name, "standard", tileW - 16);
		ui.text(r.x + 8, r.y + 8 + image + 8 + nameH + 4, note, "little", tileW - 16, true);
	}
	ui.endRegion(rows * stride - gap + 4);
	// A label the standard font cannot fit in its button on a compact screen takes the small one
	// rather than being cut off.
	const auto fitting = [](const std::string &label, int width)
	{
		return Toolkit::getFont("standard")->getStringWidth(label) + 16 <= width ? "standard"
																				 : "little";
	};
	// The row: Back; Regenerate all; beside it Randomize parameters, the same sheet with every
	// landscape's parameters drawn at random (the size and colony count stay), for an even wider
	// spread of maps to pick from; Reset to defaults, the way back; and Use, taking what is left.
	int bx = x;
	const auto place = [&](const std::string &id, int width, const std::string &label,
						   std::function<void()> action)
	{
		ui.button(id, {bx, height - 55, width, 34}, label, std::move(action), false, true, false,
				  fitting(label, width));
		bx += width + 10;
	};
	place("landscape/back", compact ? 80 : 100, tr("Back"), [this] { endExecute(CANCEL); });
	place("landscape/regenerate", compact ? 110 : 170, tr("Regenerate all"),
		  [this] { previewer.regenerate(); });
	place("landscape/randomize", compact ? 130 : 190, tr("Randomize parameters"),
		  [this] { randomizeParameters(); });
	place("landscape/reset", compact ? 110 : 150, tr("Reset to defaults"),
		  [this] { resetParameters(); });
	const int useX = bx;
	const bool valid = selected >= 0 && selected < int(entries.size());
	ui.button(
		"landscape/use", {useX, height - 55, x + w - useX, 34},
		tr("Use") + (valid ? " " + entries[selected].name : ""), [this] { confirm(); }, true,
		valid);
}
