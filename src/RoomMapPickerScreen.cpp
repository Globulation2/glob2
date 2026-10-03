// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#include "RoomMapPickerScreen.h"
#include "Engine.h"
#include "GUIMapPreview.h"
#include "GeneratorRegistry.h"
#include "LobbyMapCatalog.h"
#include "MapThumbnail.h"
#include "RoomSetup.h"
#include <FileManager.h>
#include <FormatableString.h>
#include <Toolkit.h>
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>

using namespace Glob2UI;

namespace
{
constexpr int SIZES[] = {6, 7, 8}; // log2 of the side: 64, 128, 256
constexpr int PREMADES_PER_TICK = 2;

std::string sideText(int log2)
{
	const int side = 1 << log2;
	return std::to_string(side) + " × " + std::to_string(side);
}

// "balanced for 2" -> "Balanced for 2": file names are not consistently capitalised.
std::string displayName(std::string name)
{
	if (!name.empty())
		name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
	return name;
}
} // namespace

RoomMapPickerScreen::RoomMapPickerScreen(int people, const CustomGameSetup &current)
	: base(current), people(std::max(1, people))
{
	// Colonies for everyone in the room, at least two; the fair landscapes come in 2 and 4.
	colonies = this->people <= 2 ? 2 : 4;
	for (const auto &id : Online::defaultRoomGenerators())
	{
		Landscape landscape;
		landscape.id = id;
		try
		{
			landscape.method = GeneratorRegistry::builtins().idOf(id);
		}
		catch (const std::exception &)
		{
			continue;
		}
		landscape.name = tr(GenerationRequest::methodName(landscape.method));
		landscape.preview = std::make_unique<MapPreview>();
		landscape.preview->setAnimateChanges(false);
		landscapes.push_back(std::move(landscape));
	}
	// The room's own generator first, when it is one of them.
	for (std::size_t i = 0; i < landscapes.size(); ++i)
		if (current.random && landscapes[i].method == current.generator.method)
		{
			generatedIndex = int(i);
			if (const int log2 = current.generator.wDec; log2 >= 6 && log2 <= 8)
				sizeIndex = log2 - 6;
		}
	restartPreviews();

	std::vector<std::filesystem::path> roots;
	auto *files = GAGCore::Toolkit::getFileManager();
	for (unsigned d = 0; d < files->getDirCount(); ++d)
		roots.push_back(std::filesystem::path(files->getDir(d)) / "maps");
	for (auto &entry : lobbyMapCatalog(roots))
	{
		Premade map;
		map.path = entry.path;
		map.name = displayName(entry.name);
		map.preview = std::make_unique<MapPreview>();
		map.preview->setAnimateChanges(false);
		premades.push_back(std::move(map));
	}
}

RoomMapPickerScreen::~RoomMapPickerScreen() = default;

std::vector<GenerationRequest> RoomMapPickerScreen::requests() const
{
	std::vector<GenerationRequest> result;
	for (const auto &landscape : landscapes)
	{
		GenerationRequest request;
		request.setMethodDefaults(landscape.method);
		request.wDec = SIZES[sizeIndex];
		request.hDec = SIZES[sizeIndex];
		request.nbTeams = colonies;
		request.seed = 0;
		result.push_back(request);
	}
	return result;
}

void RoomMapPickerScreen::restartPreviews()
{
	if (!previewer)
		previewer = std::make_unique<LandscapePreviewer>(requests(), 2, false);
	else
		previewer->restart(requests());
	seenRevisions.assign(landscapes.size(), ~0u);
	for (auto &landscape : landscapes)
		landscape.preview->setState(MapPreview::State::Loading);
}

void RoomMapPickerScreen::refreshPreviews()
{
	if (!previewer)
		return;
	bool changed = false;
	for (std::size_t i = 0; i < landscapes.size() && i < previewer->size(); ++i)
	{
		const unsigned revision = previewer->revision(i);
		if (revision == seenRevisions[i])
			continue;
		seenRevisions[i] = revision;
		const auto preview = previewer->preview(i);
		if (preview.state == LandscapePreviewer::State::Ready)
		{
			landscapes[i].preview->setMapThumbnail(preview.thumbnail);
			landscapes[i].preview->starts = preview.starts;
		}
		else if (preview.state == LandscapePreviewer::State::Failed)
			landscapes[i].preview->setState(MapPreview::State::Failed);
		changed = true;
	}
	if (changed)
		invalidate();
}

void RoomMapPickerScreen::loadSomePremades()
{
	for (int n = 0; n < PREMADES_PER_TICK && nextPremade < premades.size(); ++n, ++nextPremade)
	{
		auto &map = premades[nextPremade];
		try
		{
			map.players = Engine::loadMapHeader(map.path).getNumberOfTeams();
			MapThumbnail thumbnail;
			thumbnail.loadFromMap(map.path);
			map.width = thumbnail.getMapWidth();
			map.height = thumbnail.getMapHeight();
			map.preview->setMapThumbnail(thumbnail);
			map.loaded = true;
		}
		catch (const std::exception &)
		{
			map.failed = true;
		}
		invalidate();
	}
}

void RoomMapPickerScreen::onTimer(Uint32 tick)
{
	if (previewer && previewer->threadCount() == 0)
		previewer->poll();
	refreshPreviews();
	if (currentTab == PremadeTab)
		loadSomePremades();
	(void)tick;
}

void RoomMapPickerScreen::selectTab(int tab)
{
	currentTab = std::clamp(tab, 0, 2);
	invalidate();
}

void RoomMapPickerScreen::selectGenerated(int index)
{
	if (index >= 0 && index < int(landscapes.size()))
		generatedIndex = index;
	invalidate();
}

void RoomMapPickerScreen::selectPremade(int index)
{
	if (index >= 0 && index < int(premades.size()))
		premadeIndex = index;
	invalidate();
}

void RoomMapPickerScreen::setSize(int index)
{
	const int next = std::clamp(index, 0, 2);
	if (next == sizeIndex)
		return;
	sizeIndex = next;
	restartPreviews();
	invalidate();
}

void RoomMapPickerScreen::setColonies(int value)
{
	const int next = value <= 2 ? 2 : 4;
	if (next == colonies)
		return;
	colonies = next;
	restartPreviews();
	invalidate();
}

bool RoomMapPickerScreen::canUse() const
{
	if (currentTab == GeneratedTab)
		return previewer && generatedIndex < int(previewer->size()) &&
			   previewer->preview(std::size_t(generatedIndex)).state == LandscapePreviewer::State::Ready;
	if (currentTab == PremadeTab)
		return premadeIndex >= 0 && premades[std::size_t(premadeIndex)].loaded && premades[std::size_t(premadeIndex)].players >= 2;
	return false;
}

CustomGameSetup RoomMapPickerScreen::generatedSetup() const
{
	CustomGameSetup setup = base;
	setup.random = true;
	setup.premadeMap.clear();
	if (previewer && generatedIndex < int(previewer->size()))
	{
		const auto i = std::size_t(generatedIndex);
		setup.generator = previewer->request(i);
		// The room plays exactly the map the preview showed.
		setup.generator.seed = previewer->preview(i).seed;
	}
	setup.generator.nbTeams = colonies;
	setup.setCapacity(colonies);
	return setup;
}

std::string RoomMapPickerScreen::premadePath() const
{
	return premadeIndex >= 0 ? premades[std::size_t(premadeIndex)].path : std::string();
}

std::string RoomMapPickerScreen::premadeTitle() const
{
	return premadeIndex >= 0 ? premades[std::size_t(premadeIndex)].name : std::string();
}

Element RoomMapPickerScreen::generatedTab(const Presentation &p)
{
	std::vector<std::string> sizes;
	for (int log2 : SIZES)
		sizes.push_back(sideText(log2));
	auto controls = wrap({field(tr("[room picker size]"), segments("picker/size", sizes, sizeIndex, [this](int v) { setSize(v); })),
						  field(tr("[room picker colonies]"), segments("picker/colonies", {"2", "4"}, colonies == 2 ? 0 : 1,
																	   [this](int v) { setColonies(v == 0 ? 2 : 4); }))},
						 {p.pt(12), p.pt(220), 2});
	std::vector<Element> cards;
	for (std::size_t i = 0; i < landscapes.size(); ++i)
	{
		const auto state = previewer && i < previewer->size() ? previewer->preview(i).state : LandscapePreviewer::State::Pending;
		const char *status = state == LandscapePreviewer::State::Ready ? "[room picker fair]"
							 : state == LandscapePreviewer::State::Failed ? "[room picker does not fit]"
																		  : "[room picker preparing]";
		ButtonOptions pick;
		pick.selected = int(i) == generatedIndex;
		pick.icon = pick.selected ? uiIcon(UIIcon::Check) : IconRef();
		const int index = int(i);
		cards.push_back(card(column({center(mapPreview("picker/generated/" + std::to_string(i) + "/preview", *landscapes[i].preview, p.compact() ? 120 : 150)),
									 button("picker/generated/" + std::to_string(i), landscapes[i].name, [this, index] { selectGenerated(index); }, pick),
									 caption(tr(status))},
									{p.pt(6)}),
							 {.color = theme().palette.field, .padding = p.pt(8), .shadow = false,
							  .border = pick.selected ? theme().palette.accent : theme().palette.line}));
	}
	return column({controls, caption(tr("[room picker generated note]")), wrap(std::move(cards), {p.pt(10), p.pt(170), 3})}, {p.pt(10)});
}

Element RoomMapPickerScreen::premadeTab(const Presentation &p)
{
	// Maps with a colony for everyone first; each row says players and size.
	std::vector<int> order(premades.size());
	for (std::size_t i = 0; i < order.size(); ++i)
		order[i] = int(i);
	std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
		const auto fits = [this](const Premade &m) { return !m.loaded || m.players >= people; };
		return fits(premades[std::size_t(a)]) && !fits(premades[std::size_t(b)]);
	});
	std::vector<std::string> rows;
	std::vector<bool> enabled;
	int selectedRow = -1;
	for (std::size_t r = 0; r < order.size(); ++r)
	{
		const auto &map = premades[std::size_t(order[r])];
		std::string line = map.name;
		if (map.loaded)
			line += "  ·  " + std::string(GAGCore::FormattableString(tr("[room picker players %0]")).arg(map.players)) + "  ·  " +
					std::to_string(map.width) + " × " + std::to_string(map.height);
		else if (map.failed)
			line += "  ·  " + tr("[room picker unreadable]");
		rows.push_back(line);
		enabled.push_back(!map.failed);
		if (order[r] == premadeIndex)
			selectedRow = int(r);
	}
	ListOptions list;
	list.enabled = enabled;
	list.visibleRows = p.compact() ? 6 : 10;
	list.emptyText = tr("[room picker no premade]");
	auto listElement = listView("picker/premade", rows, selectedRow, [this, order](int row) {
		if (row >= 0 && row < int(order.size()))
			selectPremade(order[std::size_t(row)]);
	}, list);
	std::vector<Element> detail;
	if (premadeIndex >= 0)
	{
		const auto &map = premades[std::size_t(premadeIndex)];
		detail.push_back(center(mapPreview("picker/premade/preview", *map.preview, p.compact() ? 160 : 220)));
		detail.push_back(heading(map.name));
		if (map.loaded)
			detail.push_back(caption(std::string(GAGCore::FormattableString(tr("[room picker players %0]")).arg(map.players)) + " · " +
									 std::to_string(map.width) + " × " + std::to_string(map.height)));
		if (map.loaded && map.players < people)
			detail.push_back(paragraph(GAGCore::FormattableString(tr("[room picker too small %0]")).arg(people), {FontRole::Support, true}));
	}
	else
		detail.push_back(paragraph(tr("[room picker pick premade]"), {FontRole::Support, true}));
	if (p.compact())
		return column({listElement, column(std::move(detail), {p.pt(6)})}, {p.pt(10)});
	return row({expanded(listElement, 3), expanded(column(std::move(detail), {p.pt(6)}), 2)}, {p.pt(12), CrossAlign::Start});
}

Element RoomMapPickerScreen::catalogTab(const Presentation &p)
{
	return column({paragraph(tr("[room picker catalog intro]")),
				   align(Alignment::Left, button("picker/catalog", tr("[room picker browse catalog]"), [this] { endExecute(Catalog); },
												 {.primary = true, .icon = uiIcon(UIIcon::Map)}))},
				  {p.pt(10)});
}

Element RoomMapPickerScreen::build(const Presentation &p)
{
	const std::vector<std::string> tabs{tr("[room picker generated]"), tr("[room picker premade]"), tr("[room picker catalog]")};
	Element body = currentTab == GeneratedTab ? generatedTab(p) : currentTab == PremadeTab ? premadeTab(p) : catalogTab(p);
	const std::string forWhom = people == 1 ? tr("[room picker for one]")
											: std::string(GAGCore::FormattableString(tr("[room picker for %0]")).arg(people));
	auto top = column({caption(forWhom),
					   segments("picker/tab", tabs, currentTab, [this](int v) { selectTab(v); })},
					  {p.pt(8)});
	std::vector<MenuAction> buttons{
		{"picker/more", tr("[room picker more options]"), [this] { endExecute(MoreOptions); }},
		{"picker/cancel", tr("[Cancel]"), [this] { endExecute(Cancelled); }, false, SDLK_ESCAPE},
	};
	MenuAction use{"picker/use", tr("[room picker use]"), [this] {
					   if (canUse())
						   endExecute(Chosen);
				   }};
	use.primary = true;
	use.shortcut = SDLK_RETURN;
	use.enabled = canUse();
	if (currentTab != CatalogTab)
		buttons.push_back(std::move(use));
	return page(tr("[room picker title]"), column({top, expanded(scroll("picker/body", body))}, {p.pt(10)}),
				actions(std::move(buttons), p), p, 900);
}
