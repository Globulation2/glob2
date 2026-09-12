// SPDX-License-Identifier: GPL-3.0-or-later
#include "LandscapePickerScreen.h"
#include "GlobalContainer.h"
#include "LobbyControls.h"
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>

namespace
{
std::string tr(const std::string &s) { return Toolkit::getStringTable()->getString("[" + s + "]"); }
} // namespace

std::vector<GenerationRequest>
LandscapePickerScreen::requestsOf(const std::vector<Entry> &entries)
{
	std::vector<GenerationRequest> requests;
	for (const auto &entry : entries)
		requests.push_back(entry.request);
	return requests;
}

LandscapePickerScreen::LandscapePickerScreen(const std::string &title, std::vector<Entry> entries,
											 int selected)
	: title(title), entries(std::move(entries)), tiles(this->entries.size()),
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

void LandscapePickerScreen::onTimer(Uint32) { refresh(); }

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
		if (tile.preview.state == LandscapePreviewer::State::Ready)
		{
			// Surfaces belong to the UI thread; the worker only hands over pixels.
			tile.surface = std::make_unique<DrawableSurface>(MapPreview::PreviewSize,
															 MapPreview::PreviewSize);
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
		const int step = key == SDLK_LEFT	 ? -1
						 : key == SDLK_RIGHT ? 1
						 : key == SDLK_UP	 ? -columns
						 : key == SDLK_DOWN	 ? columns
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
	columns = std::clamp((w - scrollbar + gap) / (tileMin + gap), 1,
						 std::max(1, int(entries.size())));
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
	ui.button(
		"landscape/back", {x, height - 55, 100, 34}, tr("Back"), [this] { endExecute(CANCEL); },
		false, true, true);
	const int regenerateW = compact ? 150 : 190;
	ui.button(
		"landscape/regenerate", {x + 110, height - 55, regenerateW, 34}, tr("Regenerate all"),
		[this] { previewer.regenerate(); });
	const int useX = x + 110 + regenerateW + 10;
	const bool valid = selected >= 0 && selected < int(entries.size());
	ui.button(
		"landscape/use", {useX, height - 55, x + w - useX, 34},
		tr("Use") + (valid ? " " + entries[selected].name : ""), [this] { confirm(); }, true,
		valid);
}
