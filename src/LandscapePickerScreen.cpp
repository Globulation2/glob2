// SPDX-License-Identifier: GPL-3.0-or-later
#include "LandscapePickerScreen.h"
#include "GlobalContainer.h"
#include "LobbyControls.h"
#include "GenerationContext.h"
#include "GenerationValidation.h"
#include "GeneratorRegistry.h"
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <functional>
#include <map>
#include <random>

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
// Every tag on any of `entries` is "category:value"; the filter row groups by category, reading
// the vocabulary straight from what these entries' own generators declared (each generator sets
// its GeneratorDefinition::tags itself, so there is no separate catalog to keep in sync here).
std::vector<std::string>
categoriesOf(const std::vector<LandscapePickerScreen::Entry> &entries)
{
	std::vector<std::string> found;
	for (const auto &entry : entries)
		for (const auto &tag : entry.tags)
		{
			const auto category = tag.substr(0, tag.find(':'));
			if (std::find(found.begin(), found.end(), category) == found.end())
				found.push_back(category);
		}
	std::sort(found.begin(), found.end());
	return found;
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
	  filterCategories(categoriesOf(this->entries)),
	  filters(filterCategories.size()),
	  selected(std::clamp(selected, 0, std::max(0, int(this->entries.size()) - 1))),
	  sortOrder(sortOrder), previewer(requestsOf(this->entries))
{
	gfx = globalContainer->gfx;
	controls = new LobbyControls();
	controls->render = [this] { render(); };
	controls->focus = "landscape/" + std::to_string(this->selected);
	addWidget(controls);
	recomputeIncompatible();
	rebuild();
}

LandscapePickerScreen::~LandscapePickerScreen() = default;

void LandscapePickerScreen::recomputeIncompatible()
{
	incompatible.assign(entries.size(), {});
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		const auto &entry = entries[i];
		if (entry.method < 0)
			continue;
		if (const auto *definition = GeneratorRegistry::builtins().find(entry.method))
			incompatible[i] = validateGenerationRequest(entry.request, *definition);
	}
}

void LandscapePickerScreen::setShared(const GeneratorControl &control, int value)
{
	for (auto &entry : entries)
		control.set(entry.request, value);
	recomputeIncompatible();
	previewer.restart(requestsOf(entries));
	rebuild();
}

bool LandscapePickerScreen::matchesFilters(int index, int skip) const
{
	const auto &tags = entries[index].tags;
	for (int c = 0; c < int(filterCategories.size()); ++c)
		if (c != skip && !filters[c].empty() &&
			std::find(tags.begin(), tags.end(), filterCategories[c] + ":" + filters[c]) == tags.end())
			return false;
	return true;
}

void LandscapePickerScreen::rebuild()
{
	visible.clear();
	for (int i = 0; i < int(entries.size()); ++i)
		if (matchesFilters(i, -1))
			visible.push_back(i);
	if (sortOrder == SortOrder::Alphabetical)
		std::stable_sort(visible.begin(), visible.end(), [this](int a, int b)
						 { return entries[a].name < entries[b].name; });
	else
	{
		// A fresh shuffle, not a stable pseudo-random order: FEEDBACK 2026-09-17, "random isn't
		// actually randomizing the order... I want it to be dynamically random" - every dialog
		// open (this runs once from the constructor) and every return to Random after
		// Alphabetical draws a new permutation, from real entropy so it never repeats a fixed
		// sequence the way a seed derived from the entries themselves would.
		std::mt19937 random(std::random_device{}());
		std::shuffle(visible.begin(), visible.end(), random);
	}
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
		// A preview's own scroll-to-zoom is switched off on this sheet (FEEDBACK 2026-09-17: it
		// competed with scrolling the grid of landscapes itself, so scroll wheel here is always
		// grid scrolling - never zoom, whether or not the pointer sits over a card's image).
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
					 // Not tr(): this replaces an existing translated sentence with one describing
					 // the new inline size/colony controls below, and there is no translation
					 // key yet for the new wording (see the sort/filter labels above for the
					 // same reasoning - catalog and UI-only text added this pass stays plain).
					 ui.paragraph(x + 8, subtitleY, w - 16,
								  "Each landscape is shown as a real map at the size and colony "
								  "count below, which you can change here. Use one to play the "
								  "map shown.");
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
		// Faceted like a shopping catalog: an option is only offered if it leaves at least one
		// result once the OTHER active filters are applied too, so picking any shown option can
		// never empty the sheet (FEEDBACK 2026-09-17). The count after each label is how many
		// landscapes that choice would leave, "Any" showing how many the other filters alone
		// allow; the currently chosen value stays listed (at 0) even on the rare frame it would
		// otherwise have dropped out, so the dropdown never silently loses the person's choice.
		int pool = 0;
		std::map<std::string, int> counts;
		const std::string prefix = filterCategories[c] + ":";
		for (int i = 0; i < int(entries.size()); ++i)
			if (matchesFilters(i, c))
			{
				++pool;
				for (const auto &tag : entries[i].tags)
					if (tag.compare(0, prefix.size(), prefix) == 0)
						++counts[tag.substr(prefix.size())];
			}
		if (!filters[c].empty())
			counts.try_emplace(filters[c], 0);
		std::vector<std::string> values;
		std::vector<std::string> options{"Any " + tagLabel(filterCategories[c]) + " (" +
										 std::to_string(pool) + ")"};
		for (const auto &[value, count] : counts)
		{
			values.push_back(value);
			options.push_back(tagLabel(value) + " (" + std::to_string(count) + ")");
		}
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
	// Map size and colony count, editable right here rather than only inherited from the lobby
	// behind this sheet (FEEDBACK 2026-09-17: "so that i can instantly see how it impacts all of
	// the various available maps" - changing either reforms every entry's request and rerolls
	// every visible preview at once via setShared(), so the whole grid updates together for fast
	// comparison, the same immediacy "Regenerate all" already has). sharedWDec/HDec/Teams read
	// back the sheet's current values (uniform across entries[]) so a caller can keep its own
	// lobby setup showing the same size and count, in both directions, once this sheet closes.
	const int sizeY = barY + barH + 8;
	int sizeX = x + 8;
	for (const auto &id : {"width", "height", "teams"})
	{
		const auto found = std::find_if(GenerationRequest::sharedControls().begin(),
										GenerationRequest::sharedControls().end(),
										[&](const GeneratorControl &c) { return c.id == id; });
		const auto &control = *found;
		const int labelW = compact ? 60 : 80, fieldW = compact ? 90 : 110;
		ui.text(sizeX, sizeY + 7, tr(control.label), "little", labelW, true);
		std::vector<std::string> options;
		for (int v : control.values())
			options.push_back(std::to_string(control.displayValue(v)));
		ui.dropdown("landscape/" + std::string(id), {sizeX + labelW, sizeY, fieldW, barH}, options,
					control.indexOf(control.get(entries.front().request)),
					[this, control](int index) { setShared(control, control.valueAt(index)); });
		sizeX += labelW + fieldW + 14;
	}
	const int top = sizeY + barH + 10;
	const int bottom = height - 66;
	const int gap = 12, scrollbar = 12;
	// Card size is fixed by the sheet's own width, never by how many results a filter leaves: a
	// single match sits at its normal size in an otherwise-empty grid rather than stretching to
	// fill the row (FEEDBACK 2026-09-17, seen with the tag filters narrowed to one landscape).
	const int tileMin = compact ? 176 : 200;
	columns = std::max(1, (w - scrollbar + gap) / (tileMin + gap));
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
