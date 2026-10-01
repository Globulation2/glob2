// SPDX-License-Identifier: GPL-3.0-or-later
#include "LandscapePickerScreen.h"
#include "GenerationContext.h"
#include "GenerationValidation.h"
#include "GeneratorRegistry.h"
#include "GlobalContainer.h"
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <charconv>
#include <functional>
#include <map>
#include <numeric>
#include <random>
#include <string_view>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

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
std::vector<std::string> categoriesOf(const std::vector<LandscapePickerScreen::Entry> &entries)
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
// The Random sort order's permutation of entries[] indices (0..count-1), drawn once per distinct
// entry count and reused for the rest of this process's run - not reshuffled on every dialog open
// or filter/sort change. FEEDBACK 2026-09-17, refining the first version of this fix: "I don't
// like how the random order changes sometimes while I'm using the UI... randomized once when the
// app starts and then stays consistent for the duration of execution." Relaunching the app draws
// a fresh one; a single run stays on whichever order it first drew, however many times the sheet
// is opened or narrowed. Cached by count rather than a single fixed-size vector because a caller
// could in principle open this screen on different entry lists in one run (the editor's landscape
// list versus a lobby's, say); each distinct count gets its own permutation, drawn once.
const std::vector<int> &randomOrderFor(std::size_t count)
{
	static std::map<std::size_t, std::vector<int>> cache;
	auto it = cache.find(count);
	if (it == cache.end())
	{
		std::vector<int> order(count);
		std::iota(order.begin(), order.end(), 0);
		std::mt19937 random(std::random_device{}());
		std::shuffle(order.begin(), order.end(), random);
		it = cache.emplace(count, std::move(order)).first;
	}
	return it->second;
}
} // namespace

std::vector<GenerationRequest> LandscapePickerScreen::requestsOf(const std::vector<Entry> &entries)
{
	std::vector<GenerationRequest> requests;
	for (const auto &entry : entries)
		requests.push_back(entry.request);
	return requests;
}

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
			std::find(tags.begin(), tags.end(), filterCategories[c] + ":" + filters[c]) ==
				tags.end())
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
		std::stable_sort(visible.begin(), visible.end(),
						 [this](int a, int b) { return entries[a].name < entries[b].name; });
	else
	{
		// This run's fixed random order (see randomOrderFor), narrowed to what the active filters
		// still allow, without disturbing the relative order of what remains - the same relation
		// filtering already has to Alphabetical order above.
		std::vector<unsigned char> shown(entries.size(), 0);
		for (int i : visible)
			shown[i] = 1;
		std::vector<int> ordered;
		ordered.reserve(visible.size());
		for (int i : randomOrderFor(entries.size()))
			if (shown[i])
				ordered.push_back(i);
		visible = std::move(ordered);
	}
	reveal = true;
	layoutReady = false;
	invalidate();
}

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
	invalidate();
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
	invalidate();
}

void LandscapePickerScreen::refresh()
{
	for (std::size_t i = 0; i < tiles.size(); ++i)
	{
		auto &tile = tiles[i];
		const unsigned revision = previewer.revision(i);
		if (revision == tile.revision)
			continue;
		invalidate();
		tile.preview = previewer.preview(i);
		tile.revision = tile.preview.revision;
		if (tile.widget)
			tile.widget->cancelDrag();
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
			if (!tile.widget)
			{
				previews.push_back(std::make_unique<MapPreview>());
				tile.widget = previews.back().get();
				tile.widget->setAnimateChanges(false);
			}
			// MapPreview retains CPU pixels and creates surfaces only on a visible paint.
			tile.widget->setMapThumbnail(tile.preview.thumbnail);
			tile.widget->starts = tile.preview.starts;
		}
		else if (tile.preview.state == LandscapePreviewer::State::Failed && tile.widget)
			tile.widget->setState(MapPreview::State::Failed);
	}
}

LandscapePickerScreen::LandscapePickerScreen(const std::string &title, std::vector<Entry> entries, int selected, SortOrder sortOrder)
	: title(title), entries(std::move(entries)), tiles(this->entries.size()), redraws(this->entries.size(), 0),
	  filterCategories(categoriesOf(this->entries)), filters(filterCategories.size()),
	  selected(std::clamp(selected, 0, std::max(0, int(this->entries.size()) - 1))), sortOrder(sortOrder),
	  previewer(requestsOf(this->entries), 2, true)
{
	recomputeIncompatible();
	rebuild();
}

LandscapePickerScreen::~LandscapePickerScreen() = default;

void LandscapePickerScreen::select(int index)
{
	if (index < 0 || index >= int(entries.size()))
		return;
	selected = index;
	reveal = true;
	host().focus("landscape/" + std::to_string(index), false);
	invalidate();
}

void LandscapePickerScreen::confirm()
{
	if (chosenSeed().has_value())
		endExecute(selected);
}

void LandscapePickerScreen::onTimer(Uint32 tick)
{
	// A synchronous host must present the initial layout and leave time for input between
	// attempts. Native generation runs independently and never needs polling here.
	updatePreviewPriority();
	if (layoutReady && previewer.threadCount() == 0 && Sint32(tick - nextPreviewTick) >= 0 && !host().popupOpen())
	{
		previewer.poll(viewportSlots);
		nextPreviewTick = SDL_GetTicks() + 100;
	}
	refresh();
	if (reveal && selected >= 0)
	{
		reveal = false;
		host().scrollIntoView("landscape/" + std::to_string(selected));
	}
}

// Cards nearest the viewport centre roll first; cards in view are the cooperative host's
// eligible set. Card bounds come from the laid-out tree, so grids and single columns share it.
void LandscapePickerScreen::updatePreviewPriority()
{
	host().layoutIfNeeded();
	auto *grid = host().find("landscape/grid");
	if (!grid)
		return;
	const fe::Rect viewport = grid->bounds;
	std::vector<std::pair<double, std::size_t>> distances;
	viewportSlots.clear();
	columns = 1;
	int firstY = -1;
	for (int i : visible)
	{
		auto *card = host().find("landscape/" + std::to_string(i));
		if (!card)
			continue;
		const fe::Rect box = card->bounds;
		if (firstY < 0)
			firstY = box.y;
		if (box.y == firstY)
		{
			int before = 0;
			for (int j : visible)
			{
				auto *other = host().find("landscape/" + std::to_string(j));
				if (other && other->bounds.y == firstY && other->bounds.x < box.x)
					++before;
			}
			columns = std::max(columns, before + 1);
		}
		const double dx = box.x + box.w / 2.0 - viewport.x - viewport.w / 2.0;
		const double dy = box.y + box.h / 2.0 - viewport.y - viewport.h / 2.0;
		distances.emplace_back(dx * dx + dy * dy, std::size_t(i));
		if (box.intersects(viewport))
			viewportSlots.push_back(std::size_t(i));
	}
	std::stable_sort(distances.begin(), distances.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	std::vector<std::size_t> order;
	for (const auto &[distance, index] : distances)
		order.push_back(index);
	const int offset = grid->scrollOffset();
	if (!layoutReady || offset != priorityOffset)
		nextPreviewTick = SDL_GetTicks() + 100;
	if (!layoutReady || order != priorityOrder)
	{
		priorityOrder = std::move(order);
		previewer.prioritize(priorityOrder);
	}
	priorityOffset = offset;
	layoutReady = true;
}

void LandscapePickerScreen::onEvent(const SDL_Event &event)
{
	switch (event.type)
	{
	case SDL_EVENT_MOUSE_MOTION:
	case SDL_EVENT_MOUSE_WHEEL:
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_FINGER_DOWN:
	case SDL_EVENT_FINGER_MOTION:
		nextPreviewTick = SDL_GetTicks() + 150;
		break;
	default:
		break;
	}
	if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) && (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED))
	{
		layoutReady = false;
		for (auto &tile : tiles)
			if (tile.widget)
				tile.widget->cancelDrag();
	}
}

// Arrow keys move the selection through the visible grid while a card (or nothing) has focus.
bool LandscapePickerScreen::interceptEvent(const SDL_Event &event)
{
	if (event.type != SDL_EVENT_KEY_DOWN || host().popupOpen() || !host().editing().empty())
		return false;
	// Only a card or its preview image (or nothing) leaves the arrows to the grid.
	const auto &focused = host().focused();
	const bool onTile = focused.rfind("landscape/image/", 0) == 0 ||
						(focused.rfind("landscape/", 0) == 0 && focused.size() > 10 && std::isdigit(static_cast<unsigned char>(focused[10])));
	if (!focused.empty() && !onTile)
		return false;
	const auto key = event.key.key;
	const int step = key == SDLK_LEFT ? -1 : key == SDLK_RIGHT ? 1 : key == SDLK_UP ? -columns : key == SDLK_DOWN ? columns : 0;
	if (step != 0 && !visible.empty())
	{
		const auto at = std::find(visible.begin(), visible.end(), selected);
		const int pos = at == visible.end() ? 0 : int(at - visible.begin());
		select(visible[std::size_t(std::clamp(pos + step, 0, int(visible.size()) - 1))]);
		return true;
	}
	if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
	{
		confirm();
		return true;
	}
	return false;
}

Element LandscapePickerScreen::tile(int i, const Presentation &p, bool compact)
{
	auto &entry = entries[std::size_t(i)];
	auto &tileState = tiles[std::size_t(i)];
	const bool enabled = incompatible[std::size_t(i)].empty();
	const bool current = i == selected;
	const int image = p.pt(compact ? 150 : 176);
	Element picture;
	std::string note;
	if (!enabled)
	{
		// The reason lives in the preview square itself: there is no map to preview for a
		// disabled landscape, so the square becomes the status message.
		fe::CardOptions box;
		box.color = theme().palette.disabled;
		box.shadow = false;
		box.padding = p.pt(12);
		picture = fe::sized({image, image}, fe::card(fe::paragraph(incompatible[std::size_t(i)], {fe::FontRole::Support, true}), box));
	}
	else if (tileState.widget && tileState.widget->isThumbnailLoaded())
	{
		auto *widget = tileState.widget;
		widget->markerSize = compact ? 12 : 14;
		fe::CanvasOptions options;
		options.pointer = [this, widget, i](fe::PointerPhase phase, fe::Point local, fe::Host &)
		{
			// Image presses select; drags pan the preview. A drag never launches a choice.
			SDL_Event event{};
			const fe::Point at{widget->getLeft() + local.x, widget->getTop() + local.y};
			if (phase == fe::PointerPhase::Down)
			{
				if (i != selected)
				{
					selected = i;
					reveal = false;
					invalidate();
				}
				event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
				event.button.button = SDL_BUTTON_LEFT;
				event.button.x = at.x;
				event.button.y = at.y;
			}
			else if (phase == fe::PointerPhase::Move)
			{
				event.type = SDL_EVENT_MOUSE_MOTION;
				event.motion.state = SDL_BUTTON_LMASK;
				event.motion.x = at.x;
				event.motion.y = at.y;
			}
			else if (phase == fe::PointerPhase::Up)
			{
				event.type = SDL_EVENT_MOUSE_BUTTON_UP;
				event.button.button = SDL_BUTTON_LEFT;
				event.button.x = at.x;
				event.button.y = at.y;
			}
			else
			{
				widget->cancelDrag();
				return;
			}
			widget->handlePreviewEvent(&event);
		};
		picture = fe::canvas("landscape/image/" + std::to_string(i), {image, image},
							 [widget](fe::Canvas &c, fe::Rect r, const fe::Frame &)
							 {
								 widget->setScreenRectangle(r.x, r.y, r.w, r.h);
								 widget->paint(c.surface());
							 },
							 options);
		note = std::to_string(widget->getLastWidth()) + " x " + std::to_string(widget->getLastHeight()) + "  /  " +
			   std::to_string(widget->starts.size()) + " " + tr("colonies");
	}
	else
	{
		// Still rolling, or briefly Ready with no surface yet: never a blank square.
		fe::CardOptions box;
		box.color = theme().palette.placeholder;
		box.shadow = false;
		box.padding = 0;
		picture = fe::sized({image, image}, fe::card(fe::empty(), box));
		note = tileState.preview.state == LandscapePreviewer::State::Failed ? tr("Preview unavailable") : tr("Generating preview...");
	}
	if (tileState.preview.state == LandscapePreviewer::State::Ready && enabled)
		note = std::to_string(tileState.preview.width) + " x " + std::to_string(tileState.preview.height) + "  /  " +
			   std::to_string(tileState.preview.starts.size()) + " " + tr("colonies");
	fe::ButtonOptions options;
	options.selected = current;
	options.enabled = enabled;
	options.alignLeft = true;
	options.minHeight = 1;
	auto action = [this, i]
	{
		if (i == selected)
			confirm();
		else
			select(i);
	};
	auto content = fe::padding(fe::Insets::all(p.pt(8)), fe::column({fe::center(picture), fe::label(entry.name), fe::caption(note)}, {p.pt(4)}));
	options.accessibleLabel = entry.name;
	return fe::stack({fe::button("landscape/" + std::to_string(i), "", action, options), content});
}

Element LandscapePickerScreen::build(const Presentation &p)
{
	const bool compact = p.compact() || p.safe.w < p.pt(800);
	std::vector<Element> header;
	header.push_back(fe::paragraph("Each landscape is shown as a real map at the size and colony count below, which you can change here. "
								   "Use one to play the map shown.",
								   {fe::FontRole::Support}));
	// Sort order and tag filters, browsed like a catalog: a category narrows the list to entries
	// carrying its chosen value (AND across categories; "Any" leaves a category unfiltered).
	std::vector<Element> bar;
	bar.push_back(fe::segments("landscape/sort", {"Random", "Alphabetical"}, int(sortOrder),
							   [this](int value)
							   {
								   sortOrder = SortOrder(value);
								   rebuild();
							   }));
	for (int c = 0; c < int(filterCategories.size()); ++c)
	{
		// Faceted like a shopping catalog: an option is only offered if it leaves at least one
		// result once the OTHER active filters are applied too.
		int pool = 0;
		std::map<std::string, int> counts;
		const std::string prefix = filterCategories[std::size_t(c)] + ":";
		for (int i = 0; i < int(entries.size()); ++i)
			if (matchesFilters(i, c))
			{
				++pool;
				for (const auto &tag : entries[std::size_t(i)].tags)
					if (tag.compare(0, prefix.size(), prefix) == 0)
						++counts[tag.substr(prefix.size())];
			}
		if (!filters[std::size_t(c)].empty())
			counts.try_emplace(filters[std::size_t(c)], 0);
		std::vector<std::string> values;
		std::vector<std::string> options{"Any " + tagLabel(filterCategories[std::size_t(c)]) + " (" + std::to_string(pool) + ")"};
		for (const auto &[value, count] : counts)
		{
			values.push_back(value);
			options.push_back(tagLabel(value) + " (" + std::to_string(count) + ")");
		}
		const auto match = std::find(values.begin(), values.end(), filters[std::size_t(c)]);
		const int current = filters[std::size_t(c)].empty() || match == values.end() ? 0 : int(match - values.begin()) + 1;
		bar.push_back(fe::choice("landscape/filter/" + std::to_string(c), options, current,
								 [this, c, values](int index)
								 {
									 filters[std::size_t(c)] = index <= 0 ? std::string() : values[std::size_t(index - 1)];
									 rebuild();
								 }));
	}
	// Map size and colony count, editable right here: changing either reforms every entry's
	// request and rerolls every visible preview at once, so the whole grid updates together.
	for (const auto &id : {"width", "height", "teams"})
	{
		const auto found = std::find_if(GenerationRequest::sharedControls().begin(), GenerationRequest::sharedControls().end(),
										[&](const GeneratorControl &c) { return c.id == id; });
		const auto &control = *found;
		std::vector<std::string> options;
		for (int v : control.values())
			options.push_back(std::to_string(control.displayValue(v)));
		bar.push_back(fe::row({fe::caption(tr(control.label)),
							   fe::expanded(fe::choice("landscape/" + std::string(id), options, control.indexOf(control.get(entries.front().request)),
													   [this, control](int index) { setShared(control, control.valueAt(index)); }))},
							  {p.pt(4), fe::CrossAlign::Center}));
	}
	// Larger mobile text needs wider fields, rather than wrapping numeric choices.
	header.push_back(fe::wrap(std::move(bar), {p.pt(6), p.pt(140 * (p.touch ? p.textScale : 1))}));
	std::vector<Element> cards;
	for (int i : visible)
		cards.push_back(tile(i, p, compact));
	fe::WrapOptions grid;
	grid.minChildWidth = p.pt(compact ? 150 : 200);
	grid.gap = p.pt(12);
	// Narrow screens scroll the controls away with the grid instead of squeezing it.
	Element gridElement = compact ? fe::scroll("landscape/grid", fe::column({fe::column(std::move(header), {p.pt(8)}), fe::wrap(std::move(cards), grid)}, {p.pt(10)}))
								  : fe::scroll("landscape/grid", fe::wrap(std::move(cards), grid));
	if (compact)
		header.clear();
	const bool valid = selected >= 0 && selected < int(entries.size());
	const std::string useLabel = tr("Use") + (valid && !compact ? " " + entries[std::size_t(selected)].name : "");
	Element actionRow;
	if (compact)
	{
		// Narrow screens keep Back and Use reachable and fold the sheet-wide actions into a menu.
		fe::ChoiceOptions more;
		more.compactLabel = tr("More");
		auto menu = fe::choice("landscape/more", {tr("Regenerate all"), tr("Randomize parameters"), tr("Reset to defaults")}, -1,
							   [this](int index)
							   {
								   if (index == 0)
									   previewer.regenerate();
								   else if (index == 1)
									   randomizeParameters();
								   else
									   resetParameters();
							   },
							   more);
		actionRow =
			fe::row({fe::compactButton("landscape/back", tr("Back"), fe::UIIcon::Back,
									   [this] { endExecute(CANCEL); }, p,
									   {false, false, true, false, false, false, SDLK_ESCAPE}),
					 fe::expanded(menu),
					 fe::expanded(fe::button("landscape/use", useLabel, [this] { confirm(); },
											 {true, false, valid && chosenSeed().has_value()}))},
					{p.pt(6), fe::CrossAlign::Stretch});
	}
	else
		actionRow = fe::actions({{"landscape/back", tr("Back"), [this] { endExecute(CANCEL); }, false, SDLK_ESCAPE},
								 {"landscape/regenerate", tr("Regenerate all"), [this] { previewer.regenerate(); }},
								 {"landscape/randomize", tr("Randomize parameters"), [this] { randomizeParameters(); }},
								 {"landscape/reset", tr("Reset to defaults"), [this] { resetParameters(); }},
								 {"landscape/use", useLabel, [this] { confirm(); }, true, SDLK_UNKNOWN, valid && chosenSeed().has_value()}},
								p);
	auto body = compact ? fe::column({fe::expanded(gridElement)}) : fe::column({fe::column(std::move(header), {p.pt(8)}), fe::expanded(gridElement)}, {p.pt(10)});
	fe::CardOptions cardOptions;
	cardOptions.padding = p.pt(compact ? 10 : 16);
	return fe::center(fe::maxWidth(p.pt(1120), fe::card(fe::column({fe::heading(title), fe::expanded(body), fe::divider(), actionRow}, {p.pt(8)}), cardOptions)));
}

// Completion and visual settling are separate: a delivered thumbnail may still
// be crossfading. Failed/incompatible cards are terminal, explicitly labeled UI.
bool LandscapePickerScreen::presentationSettled() const
{
	if (busy())
		return false;
	for (int i : visible)
		if (incompatible[std::size_t(i)].empty() && tiles[std::size_t(i)].widget)
		{
			auto *widget = tiles[std::size_t(i)].widget;
			const bool shown = std::find(viewportSlots.begin(), viewportSlots.end(), std::size_t(i)) != viewportSlots.end();
			if (!shown)
				continue;
			if (widget->getState() != MapPreview::State::Failed && !widget->isPresentationSettled())
				return false;
		}
	return true;
}
