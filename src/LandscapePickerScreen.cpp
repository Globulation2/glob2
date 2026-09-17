// SPDX-License-Identifier: GPL-3.0-or-later
#include "LandscapePickerScreen.h"
#include "GlobalContainer.h"
#include "LobbyControls.h"
#include "GenerationContext.h"
#include "GenerationValidation.h"
#include "GeneratorRegistry.h"
#include "GeneratorTags.h"
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
// A tag's value half, capitalized, for a filter menu: "wide-open" -> "Wide open". Tags are plain
// catalog strings, not translation keys, so this dresses them up rather than looking them up in
// the string table.
std::string tagLabel(const std::string &value)
{
	std::string label = value;
	std::replace(label.begin(), label.end(), '-', ' ');
	if (!label.empty())
		label[0] = char(std::toupper(static_cast<unsigned char>(label[0])));
	return label;
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
											 int selected, SortOrder sortOrder)
	: title(title), entries(std::move(entries)), tiles(this->entries.size()),
	  redraws(this->entries.size(), 0),
	  filterCategories(GeneratorTags::categories()),
	  filters(filterCategories.size()),
	  selected(std::clamp(selected, 0, std::max(0, int(this->entries.size()) - 1))),
	  sortOrder(sortOrder), previewer(requestsOf(this->entries))
{
	gfx = globalContainer->gfx;
	controls = new LobbyControls();
	controls->render = [this] { render(); };
	controls->focus = "landscape/" + std::to_string(this->selected);
	addWidget(controls);
	incompatible.resize(this->entries.size());
	for (std::size_t i = 0; i < this->entries.size(); ++i)
	{
		const auto &entry = this->entries[i];
		if (entry.method < 0)
			continue;
		if (const auto *definition = GeneratorRegistry::builtins().find(entry.method))
			incompatible[i] = validateGenerationRequest(entry.request, *definition);
	}
	rebuild();
}

LandscapePickerScreen::~LandscapePickerScreen() = default;

void LandscapePickerScreen::rebuild()
{
	visible.clear();
	for (int i = 0; i < int(entries.size()); ++i)
	{
		bool matches = true;
		for (std::size_t c = 0; c < filterCategories.size() && matches; ++c)
			if (!filters[c].empty())
				matches = std::find(entries[i].tags.begin(), entries[i].tags.end(),
									filterCategories[c] + ":" + filters[c]) != entries[i].tags.end();
		if (matches)
			visible.push_back(i);
	}
	if (sortOrder == SortOrder::Alphabetical)
		std::stable_sort(visible.begin(), visible.end(), [this](int a, int b)
						 { return entries[a].name < entries[b].name; });
	reveal = true;
}

void LandscapePickerScreen::onAction(Widget *, Action, int, int) {}

std::optional<std::uint32_t> LandscapePickerScreen::chosenSeed() const
{
	if (selected < 0 || selected >= int(tiles.size()))
		return {};
	const auto &preview = tiles[selected].preview;
	// A retained old image is not a result for the request now being generated.
	if (preview.state != LandscapePreviewer::State::Ready ||
		previewer.revision(selected) != tiles[selected].revision)
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
		request.randomizeControls(GenerationContext::deriveSeed(GenerationContext::randomSeed(),
																"random/" + std::to_string(i)));
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
	if (chosenSeed().has_value())
		endExecute(selected);
}

void LandscapePickerScreen::onTimer(Uint32)
{
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
		if (tile.widget)
			tile.widget->cancelDrag();
		if (activePreview == int(i))
			activePreview = -1;
		// A random set of parameters the world refused (every seed failed) is drawn again while
		// draws remain: the sheet should show maps, not failures, and the user asked for variety.
		if (tile.preview.state == LandscapePreviewer::State::Failed && redraws[i] > 0)
		{
			--redraws[i];
			GenerationRequest request = entries[i].request;
			request.randomizeControls(GenerationContext::deriveSeed(GenerationContext::randomSeed(),
																	"redraw/" + std::to_string(i)));
			previewer.reroll(i, request);
			continue;
		}
		if (tile.preview.state == LandscapePreviewer::State::Ready)
		{
			// Surfaces belong to the UI thread; the worker only hands over pixels.
			if (!tile.widget)
			{
				tile.widget = new MapPreview(0, 0, ALIGN_LEFT, ALIGN_TOP);
				tile.widget->visible = false;
				addWidget(tile.widget);
			}
			tile.widget->setMapThumbnail(tile.preview.thumbnail);
			tile.widget->starts = tile.preview.starts;
		}
		else if (tile.preview.state == LandscapePreviewer::State::Failed && tile.widget)
			tile.widget->setState(MapPreview::State::Failed);
	}
}

void LandscapePickerScreen::onSDLEvent(SDL_Event *event)
{
	if (event->type == SDL_WINDOWEVENT && (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
										   event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
	{
		for (auto &tile : tiles)
			if (tile.widget)
				tile.widget->cancelDrag();
		activePreview = -1;
	}
	if (event->type == SDL_MOUSEMOTION)
	{
		pointerX = event->motion.x;
		pointerY = event->motion.y;
		for (auto &tile : tiles)
			if (tile.widget)
				tile.widget->handlePreviewEvent(event);
		if (activePreview >= 0)
		{
			if (!(event->motion.state & SDL_BUTTON_LMASK))
				activePreview = -1;
			return;
		}
	}
	if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT &&
		activePreview >= 0)
	{
		tiles[activePreview].widget->handlePreviewEvent(event);
		activePreview = -1;
		return;
	}
	if (!controls->popup.open && controls->pressed.empty())
	{
		const auto clip = controls->regions[30].box;
		if (event->type == SDL_MOUSEBUTTONDOWN &&
			(event->button.button == SDL_BUTTON_LEFT || event->button.button == SDL_BUTTON_RIGHT) &&
			LobbyControls::inside(clip, event->button.x, event->button.y))
		{
			for (int i : visible)
			{
				auto &tile = tiles[i];
				if (!tile.widget || tile.preview.state != LandscapePreviewer::State::Ready ||
					!incompatible[i].empty())
					continue;
				const auto area = tile.widget->mapArea();
				if (!LobbyControls::inside({area.x, area.y, area.w, area.h}, event->button.x,
										   event->button.y))
					continue;
				// Image clicks select; labels and Use confirm. A drag must never launch a choice.
				if (event->button.button == SDL_BUTTON_LEFT)
				{
					selected = int(i);
					reveal = false;
					controls->focus = "landscape/" + std::to_string(i);
					activePreview = int(i);
				}
				tile.widget->handlePreviewEvent(event);
				return;
			}
		}
		// Keep normal grid scrolling outside the selected map image.
		if (event->type == SDL_MOUSEWHEEL && selected >= 0 && selected < int(tiles.size()) &&
			LobbyControls::inside(clip, pointerX, pointerY))
		{
			auto &tile = tiles[selected];
			if (tile.widget && tile.preview.state == LandscapePreviewer::State::Ready &&
				tile.widget->handlePreviewEvent(event))
				return;
		}
	}
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
		if (step != 0 && !visible.empty())
		{
			// step is a position delta in the rendered grid (+-1 across, +-columns up/down), so
			// it applies to `selected`'s position within the visible, sorted and filtered order,
			// not to its identity in entries[].
			const auto at = std::find(visible.begin(), visible.end(), selected);
			const int pos = at == visible.end() ? 0 : int(at - visible.begin());
			select(visible[std::clamp(pos + step, 0, int(visible.size()) - 1)]);
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
	const int subtitleY = compact ? 46 : 56;
	const int barY = subtitleY + 6 +
					 ui.paragraph(x + 8, subtitleY, w - 16,
								  tr("Each landscape is shown as a real map at your current size "
									 "and colony count. Use one to play the map shown."));
	// Sort order and tag filters, browsed like a catalog: a category narrows the list to entries
	// carrying its chosen value (AND across categories; "Any" leaves a category unfiltered).
	// These are plain catalog labels, not translated (the tags themselves are not localized
	// strings), unlike the rest of this screen's chrome.
	const int barH = 28;
	int barX = x + 8;
	const int sortW = compact ? 130 : 160;
	{
		static const std::vector<std::string> options = {"Random", "Alphabetical"};
		ui.segments("landscape/sort", {barX, barY, sortW, barH}, options, int(sortOrder),
					[this](int value)
					{
						sortOrder = SortOrder(value);
						rebuild();
					});
		barX += sortW + 8;
	}
	const int filterCount = int(filterCategories.size());
	const int remaining = std::max(0, x + w - 8 - barX);
	const int filterW = filterCount > 0 ? std::max(84, remaining / filterCount) : 0;
	for (int c = 0; c < filterCount; ++c)
	{
		const auto values = GeneratorTags::valuesFor(filterCategories[c]);
		std::vector<std::string> options{"Any " + tagLabel(filterCategories[c])};
		for (const auto &value : values)
			options.push_back(tagLabel(value));
		const auto match = std::find(values.begin(), values.end(), filters[c]);
		const int current = filters[c].empty() || match == values.end()
								? 0
								: int(match - values.begin()) + 1;
		ui.dropdown("landscape/filter/" + std::to_string(c), {barX, barY, filterW - 8, barH}, options,
					current,
					[this, c, values](int index)
					{
						filters[c] = index <= 0 ? std::string() : values[std::size_t(index - 1)];
						rebuild();
					});
		barX += filterW;
	}
	const int top = barY + barH + 10;
	const int bottom = height - 66;
	const int gap = 12, scrollbar = 12;
	const int tileMin = compact ? 176 : 200;
	columns =
		std::clamp((w - scrollbar + gap) / (tileMin + gap), 1, std::max(1, int(visible.size())));
	const int tileW = (w - scrollbar - gap * (columns - 1)) / columns;
	const int image = tileW - 16;
	const int nameH = Toolkit::getFont("standard")->getStringHeight("Ag");
	const int noteH = Toolkit::getFont("little")->getStringHeight("Ag");
	const int tileH = 8 + image + 8 + nameH + 4 + noteH + 8;
	const int stride = tileH + gap;
	const int rows = (int(visible.size()) + columns - 1) / columns;
	auto &region = ui.regions[30];
	if (reveal && selected >= 0)
	{
		// Keep the selection in view, for keyboard moves and the initial choice.
		const auto at = std::find(visible.begin(), visible.end(), selected);
		const int pos = at == visible.end() ? 0 : int(at - visible.begin());
		const int ty = (pos / columns) * stride, viewH = bottom - top;
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
	for (std::size_t vi = 0; vi < visible.size(); ++vi)
	{
		const int i = visible[vi];
		const int column = int(vi) % columns, row = int(vi) / columns;
		const SDL_Rect r{x + column * (tileW + gap), top + row * stride - offset, tileW, tileH};
		const bool current = i == selected;
		const bool enabled = incompatible[i].empty();
		ui.button(
			"landscape/" + std::to_string(i), r, "",
			[this, i]
			{
				if (i == selected)
					confirm();
				else
					select(i);
			},
			current, enabled);
		const SDL_Rect frame{r.x + 8, r.y + 8, image, image};
		auto &tile = tiles[i];
		std::string note;
		if (!enabled)
		{
			const auto &request = entries[i].request;
			const auto area = MapPreviewGeometry::fit({frame.x, frame.y, frame.w, frame.h},
													  1 << request.wDec, 1 << request.hDec);
			ui.box({area.x, area.y, area.w, area.h}, Color(222, 226, 212), 3);
			note = incompatible[i];
		}
		else if (tile.widget && tile.widget->isThumbnailLoaded())
		{
			tile.widget->setScreenPosition(frame.x, frame.y);
			tile.widget->setDimensions(frame.w, frame.h);
			tile.widget->markerSize = compact ? 12 : 14;
			if (frame.y < bottom && frame.y + frame.h > top)
				tile.widget->paint();
			note = std::to_string(tile.widget->getLastWidth()) + " x " +
				   std::to_string(tile.widget->getLastHeight()) + "  /  " +
				   std::to_string(tile.widget->starts.size()) + " " + tr("colonies");
		}
		else
		{
			const auto &request = entries[i].request;
			const auto area = MapPreviewGeometry::fit({frame.x, frame.y, frame.w, frame.h},
													  1 << request.wDec, 1 << request.hDec);
			ui.box({area.x, area.y, area.w, area.h}, Color(211, 223, 197), 3);
			if (tile.preview.state == LandscapePreviewer::State::Failed)
				note = tr("Preview unavailable");
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
		valid && chosenSeed().has_value());
}
