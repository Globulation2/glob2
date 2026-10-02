// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineMapsScreen.h"

#include "ChooseMapScreen.h"
#include "Engine.h"
#include "FormatableString.h"
#include "GUIMapPreview.h"
#include "MapHeader.h"
#include "OnlineHandoff.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "SimVersion.h"
#include "ui/OnlineUI.h"

#include <FileManager.h>
#include <ScreenStack.h>
#include <StreamBackend.h>
#include <Toolkit.h>

#include <algorithm>

using namespace Glob2UI;
using GAGCore::FormattableString;

namespace
{
const char *SORTS[] = {"plays", "likes", "recent", "downloads"};
const char *SORT_LABELS[] = {"[maps most played]", "[maps most liked]", "[maps newest]", "[maps most downloaded]"};
const char *SIZE_LABELS[] = {"[maps any size]", "[maps up to 128]", "[maps 256]", "[maps 512 and larger]"};
const int SIDE_MIN[] = {0, 0, 256, 512};
const int SIDE_MAX[] = {0, 128, 256, 0};
const int COLONIES[] = {0, 2, 3, 4, 6, 8};
const char *REPORT_REASONS[] = {"broken", "offensive", "copyright", "other"};
const char *REPORT_LABELS[] = {"[maps report broken]", "[maps report offensive]", "[maps report copyright]",
							   "[maps report other]"};
const char *VISIBILITIES[] = {"public", "unlisted", "private"};
const char *VISIBILITY_LABELS[] = {"[maps public]", "[maps unlisted]", "[maps private]"};

int visibilityIndex(const std::string &visibility)
{
	for (int i = 0; i < 3; ++i)
		if (visibility == VISIBILITIES[i])
			return i;
	return 1;
}

std::vector<std::string> labels(const char *const *keys, std::size_t count)
{
	std::vector<std::string> out;
	for (std::size_t i = 0; i < count; ++i)
		out.push_back(tr(keys[i]));
	return out;
}

std::string sizeText(const Online::MapVersionInfo &version)
{
	if (!version.width || !version.height)
		return {};
	if (*version.width == *version.height)
		return std::to_string(*version.width) + "\xC2\xB2";
	return std::to_string(*version.width) + " \xC3\x97 " + std::to_string(*version.height);
}

// The map file's uncompressed bytes (what the catalog hashes and validates).
std::string readMapBytes(const std::string &path, std::string &error)
{
	auto &files = *GAGCore::Toolkit::getFileManager();
	const std::string readable = glob2PreferGzipReadPath(files, path);
	std::unique_ptr<GAGCore::StreamBackend> backend(files.openInflatingInputStreamBackend(readable));
	if (!backend || !backend->isValid())
	{
		error = tr("[maps cannot read file]");
		return {};
	}
	backend->seekFromEnd(0);
	const std::size_t size = backend->getPosition();
	backend->seekFromStart(0);
	if (size > Online::MapCache::MAX_MAP_BYTES)
	{
		error = tr("[maps file too large]");
		return {};
	}
	std::string bytes(size, '\0');
	if (size && !backend->readExact(bytes.data(), size))
	{
		error = tr("[maps cannot read file]");
		return {};
	}
	return bytes;
}

CardOptions plainCard(const Presentation &p, bool selected = false)
{
	CardOptions options;
	options.shadow = false;
	options.border = selected ? frontendTheme().palette.focus : frontendTheme().palette.line;
	options.color = selected ? frontendTheme().palette.selected : frontendTheme().palette.field;
	options.padding = p.pt(8);
	return options;
}
} // namespace

// ================================================================ browser

OnlineMapsScreen::OnlineMapsScreen(GAGGUI::ScreenStack &screens, Tab tab)
	: screens(screens), live(true), tab(tab), previews(std::make_unique<PreviewImages>())
{
	data.instance = onlineClient().origin();
}

OnlineMapsScreen::OnlineMapsScreen(GAGGUI::ScreenStack &screens, Tab tab, Data fixed)
	: screens(screens), live(false), data(std::move(fixed)), tab(tab),
	  previews(std::make_unique<PreviewImages>())
{
	for (const auto &[url, file] : data.previewFiles)
		previews->insertFile(url, file);
	started = true;
	if (!list().empty())
		selected = 0;
}

OnlineMapsScreen::~OnlineMapsScreen()
{
	*alive = false;
}

const std::vector<Online::MapInfo> &OnlineMapsScreen::list() const
{
	return tab == Tab::Browse ? data.browse : data.mine;
}

const Online::MapInfo *OnlineMapsScreen::selectedMap() const
{
	const auto &maps = list();
	return selected >= 0 && selected < int(maps.size()) ? &maps[selected] : nullptr;
}

void OnlineMapsScreen::onEscape()
{
	if (reporting)
	{
		reporting = false;
		invalidate();
	}
	else if (detailOpen)
		openDetail(false);
	else
		endExecute(BACK);
}

void OnlineMapsScreen::onTimer(Uint32)
{
	if (live && !started)
	{
		auto &client = Online::services().client;
		if (client.auth() == Online::PlatformClient::Auth::SignedIn)
		{
			started = true;
			data.instance = client.origin();
			data.signedIn = true;
			data.guest = client.account() && client.account()->kind == "guest";
			reload();
		}
	}
	if (download)
	{
		const auto state = download->state();
		if (state != Online::MapCache::Download::State::Pending)
		{
			if (state == Online::MapCache::Download::State::Done && downloading)
			{
				const auto &map = *downloading;
				Online::RoomMapChoice choice{map.id, map.latestVersion->hash, map.title, map.latestVersion->width,
											 map.latestVersion->height, map.latestVersion->teamCount};
				status = Online::useMapInRoom(choice) ? std::string() : tr("[maps saved for next room]");
			}
			else
				status = download->error().empty() ? tr("[maps download failed]") : download->error();
			download.reset();
			downloading.reset();
			invalidate();
		}
	}
}

void OnlineMapsScreen::reload()
{
	if (!live)
		return;
	query.mine = tab == Tab::Mine;
	query.sort = query.mine ? "recent" : SORTS[sortChoice];
	query.minSide = query.mine ? 0 : SIDE_MIN[sizeChoice];
	query.maxSide = query.mine ? 0 : SIDE_MAX[sizeChoice];
	query.teams = query.mine ? 0 : COLONIES[coloniesChoice];
	query.cursor.clear();
	loading = true;
	problem.clear();
	const Tab asked = tab;
	auto alive = this->alive;
	Online::services().client.rest(
		HttpFetch::Method::Get, query.path(), Online::Json(),
		[this, alive, asked](const Online::PlatformClient::Response &response)
		{
			if (!*alive)
				return;
			loading = false;
			if (!response.ok)
			{
				problem = response.error.message.empty() ? tr("[online connection problem]") : response.error.message;
				invalidate();
				return;
			}
			auto page = Online::parseMapList(response.result);
			(asked == Tab::Browse ? data.browse : data.mine) = std::move(page.items);
			cursor[int(asked)] = page.nextCursor;
			data.now = wallClockMs();
			if (asked == tab)
				selected = list().empty() ? -1 : std::clamp(selected, 0, int(list().size()) - 1);
			// My maps: versions still checking or rejected are only in the detail.
			if (asked == Tab::Mine)
				for (const auto &map : data.mine)
					if (!map.latestVersion)
						fetchDetail(map.id);
			if (const auto *map = selectedMap())
				fetchDetail(map->id);
			invalidate();
		});
	invalidate();
}

void OnlineMapsScreen::loadMore()
{
	const std::string next = cursor[int(tab)];
	if (!live || next.empty() || loading)
		return;
	Online::MapQuery more = query;
	more.cursor = next;
	loading = true;
	const Tab asked = tab;
	auto alive = this->alive;
	Online::services().client.rest(HttpFetch::Method::Get, more.path(), Online::Json(),
								   [this, alive, asked](const Online::PlatformClient::Response &response)
								   {
									   if (!*alive)
										   return;
									   loading = false;
									   if (response.ok)
									   {
										   auto page = Online::parseMapList(response.result);
										   auto &target = asked == Tab::Browse ? data.browse : data.mine;
										   for (auto &map : page.items)
											   target.push_back(std::move(map));
										   cursor[int(asked)] = page.nextCursor;
									   }
									   invalidate();
								   });
}

void OnlineMapsScreen::fetchDetail(const std::string &mapId)
{
	if (!live || detailRequested[mapId])
		return;
	detailRequested[mapId] = true;
	auto alive = this->alive;
	Online::services().client.rest(HttpFetch::Method::Get, "/api/v1/maps/" + Online::urlEncode(mapId), Online::Json(),
								   [this, alive, mapId](const Online::PlatformClient::Response &response)
								   {
									   if (!*alive)
										   return;
									   if (response.ok)
										   if (auto detail = Online::MapDetail::fromJson(response.result))
											   data.details[mapId] = std::move(*detail);
									   invalidate();
								   });
}

GAGCore::DrawableSurface *OnlineMapsScreen::previewOf(const Online::MapInfo &map)
{
	std::string url;
	if (map.latestVersion)
		url = map.latestVersion->previewUrl;
	else if (auto found = data.details.find(map.id); found != data.details.end() && !found->second.versions.empty())
		url = found->second.versions.front().previewUrl;
	auto *client = live ? &Online::services().client : nullptr;
	return previews->get(client, url, [this] { invalidate(); });
}

std::string OnlineMapsScreen::facts(const Online::MapInfo &map, bool withVersion) const
{
	std::vector<std::string> parts;
	if (!map.ownerName.empty())
		parts.push_back(map.ownerName);
	if (map.latestVersion)
	{
		const auto &v = *map.latestVersion;
		if (!sizeText(v).empty())
			parts.push_back(sizeText(v));
		if (v.teamCount)
			parts.push_back(FormattableString(tr("[maps %0 colonies]")).arg(*v.teamCount));
	}
	if (withVersion)
	{
		if (auto found = data.details.find(map.id); found != data.details.end() && !found->second.versions.empty())
			parts.push_back(FormattableString(tr("[maps version %0]")).arg(int(found->second.versions.size())));
		if (map.updatedAt)
			parts.push_back(FormattableString(tr("[maps updated %0]")).arg(ageText(*map.updatedAt, data.now ? data.now : wallClockMs())));
	}
	std::string out;
	for (const auto &part : parts)
		out += (out.empty() ? "" : " \xC2\xB7 ") + part;
	return out;
}

void OnlineMapsScreen::selectTab(Tab value)
{
	if (tab == value)
		return;
	tab = value;
	selected = list().empty() ? -1 : 0;
	detailOpen = false;
	reporting = false;
	if (list().empty())
		reload();
	invalidate();
}

void OnlineMapsScreen::select(int index)
{
	selected = index;
	reporting = false;
	if (const auto *map = selectedMap())
		fetchDetail(map->id);
	invalidate();
}

void OnlineMapsScreen::openDetail(bool open)
{
	detailOpen = open && selectedMap();
	reporting = false;
	invalidate();
}

void OnlineMapsScreen::setSearch(const std::string &text)
{
	if (text == query.search)
		return;
	query.search = text;
	reload();
}

void OnlineMapsScreen::setSort(int index)
{
	sortChoice = std::clamp(index, 0, 3);
	reload();
	invalidate();
}

void OnlineMapsScreen::setSize(int index)
{
	sizeChoice = std::clamp(index, 0, 3);
	reload();
	invalidate();
}

void OnlineMapsScreen::setColonies(int index)
{
	coloniesChoice = std::clamp(index, 0, 5);
	reload();
	invalidate();
}

void OnlineMapsScreen::useInRoom()
{
	const auto *map = selectedMap();
	if (!map || !map->latestVersion)
		return;
	if (!live)
	{
		status = tr("[maps downloading]");
		invalidate();
		return;
	}
	auto &services = Online::services();
	HttpFetch::Headers headers;
	if (!services.client.accessToken().empty())
		headers.emplace_back("Authorization", "Bearer " + services.client.accessToken());
	downloading = *map;
	download = services.maps.fetch(services.client.origin(), map->latestVersion->hash, headers);
	status = tr("[maps downloading]");
	invalidate();
}

void OnlineMapsScreen::toggleLike()
{
	const auto *map = selectedMap();
	if (!map || !live)
		return;
	auto &detail = data.details[map->id];
	const bool like = !detail.liked;
	const std::string id = map->id;
	auto alive = this->alive;
	Online::services().client.rest(like ? HttpFetch::Method::Put : HttpFetch::Method::Delete,
								   "/api/v1/maps/" + Online::urlEncode(id) + "/like", Online::Json(),
								   [this, alive, id](const Online::PlatformClient::Response &response)
								   {
									   if (!*alive)
										   return;
									   if (!response.ok)
									   {
										   status = response.error.code == "forbidden" ? tr("[maps like needs account]")
																					   : response.error.message;
										   invalidate();
										   return;
									   }
									   data.details[id].liked = response.result.value("liked", false);
									   const auto likes = response.result.value("likes", std::int64_t(0));
									   for (auto *maps : {&data.browse, &data.mine})
										   for (auto &m : *maps)
											   if (m.id == id)
												   m.likes = likes;
									   invalidate();
								   });
}

void OnlineMapsScreen::openMapPage()
{
	if (const auto *map = selectedMap())
		openInstancePage(data.instance, "/maps/" + map->id);
}

void OnlineMapsScreen::beginReport()
{
	reporting = !reporting;
	invalidate();
}

void OnlineMapsScreen::sendReport(int reason)
{
	const auto *map = selectedMap();
	reporting = false;
	if (!map || !live)
	{
		invalidate();
		return;
	}
	auto alive = this->alive;
	Online::services().client.rest(HttpFetch::Method::Post, "/api/v1/maps/" + Online::urlEncode(map->id) + "/reports",
								   Online::Json{{"reason", REPORT_REASONS[std::clamp(reason, 0, 3)]}, {"details", ""}},
								   [this, alive](const Online::PlatformClient::Response &response)
								   {
									   if (!*alive)
										   return;
									   status = response.ok ? tr("[maps report sent]") : response.error.message;
									   invalidate();
								   });
	invalidate();
}

void OnlineMapsScreen::shareFile(const std::string &path, const std::string &mapId)
{
	std::string title;
	if (!mapId.empty())
		for (const auto &map : data.mine)
			if (map.id == mapId)
				title = map.title;
	screens.push(std::make_unique<MapShareScreen>(path, mapId, title),
				 [this](GAGGUI::Screen &, int result)
				 {
					 if (result == MapShareScreen::SHARED)
					 {
						 detailRequested.clear();
						 reload();
					 }
				 });
}

void OnlineMapsScreen::upload()
{
	screens.push(std::make_unique<ChooseMapScreen>("maps", "map", false),
				 [this](GAGGUI::Screen &screen, int result)
				 {
					 if (result == ChooseMapScreen::OK)
						 shareFile(static_cast<ChooseMapScreen &>(screen).getMapHeader().getFileName(), {});
				 });
}

void OnlineMapsScreen::update(const std::string &mapId)
{
	screens.push(std::make_unique<ChooseMapScreen>("maps", "map", false),
				 [this, mapId](GAGGUI::Screen &screen, int result)
				 {
					 if (result == ChooseMapScreen::OK)
						 shareFile(static_cast<ChooseMapScreen &>(screen).getMapHeader().getFileName(), mapId);
				 });
}

void OnlineMapsScreen::setVisibility(const std::string &mapId, const std::string &visibility)
{
	for (auto &map : data.mine)
		if (map.id == mapId)
			map.visibility = visibility;
	invalidate();
	if (!live)
		return;
	auto alive = this->alive;
	Online::services().client.rest(HttpFetch::Method::Patch, "/api/v1/maps/" + Online::urlEncode(mapId),
								   Online::Json{{"visibility", visibility}},
								   [this, alive](const Online::PlatformClient::Response &response)
								   {
									   if (!*alive)
										   return;
									   if (!response.ok)
									   {
										   status = response.error.message;
										   reload();
									   }
									   invalidate();
								   });
}

void OnlineMapsScreen::remove(const std::string &mapId)
{
	auto &mine = data.mine;
	mine.erase(std::remove_if(mine.begin(), mine.end(), [&](const auto &m) { return m.id == mapId; }), mine.end());
	selected = std::min(selected, int(list().size()) - 1);
	invalidate();
	if (!live)
		return;
	auto alive = this->alive;
	Online::services().client.rest(HttpFetch::Method::Delete, "/api/v1/maps/" + Online::urlEncode(mapId), Online::Json(),
								   [this, alive](const Online::PlatformClient::Response &response)
								   {
									   if (!*alive)
										   return;
									   if (!response.ok)
									   {
										   status = response.error.message;
										   reload();
									   }
									   invalidate();
								   });
}

Element OnlineMapsScreen::mapCard(int index, const Presentation &p, bool phone)
{
	const auto &map = list()[index];
	const bool isSelected = index == selected;
	const int picture = p.pt(phone ? 132 : 128);
	std::string line = map.latestVersion ? sizeText(*map.latestVersion) : std::string();
	if (map.latestVersion && map.latestVersion->teamCount)
		line += (line.empty() ? "" : " \xC2\xB7 ") + std::to_string(*map.latestVersion->teamCount);
	if (!phone && !map.ownerName.empty())
		line = map.ownerName + (line.empty() ? "" : " \xC2\xB7 ") + line;
	std::string stats = "\xE2\x96\xB6 " + compactCount(map.plays);
	if (!phone)
		stats += " \xC2\xB7 \xE2\x99\xA5 " + compactCount(map.likes);
	else
		line += " \xC2\xB7 " + stats;
	std::vector<Element> parts{previewPicture(previewOf(map), picture), label(map.title), caption(line)};
	if (!phone)
		parts.push_back(caption(stats));
	ButtonOptions options;
	options.flat = true;
	options.selected = isSelected;
	options.accessibleLabel = map.title;
	return stack({button("maps/card/" + map.id, "",
						 [this, index, phone]
						 {
							 if (phone && index == selected)
								 openDetail(true);
							 else
								 select(index);
						 },
						 options),
				  card(column(std::move(parts), {p.pt(3)}), plainCard(p, isSelected))});
}

Element OnlineMapsScreen::detailPanel(const Presentation &p, bool phone)
{
	const auto *map = selectedMap();
	if (!map)
		return paragraph(tr("[maps select a map]"), {FontRole::Body, true});
	const auto found = data.details.find(map->id);
	const Online::MapDetail *detail = found != data.details.end() ? &found->second : nullptr;
	std::vector<Element> parts{center(previewPicture(previewOf(*map), p.pt(phone ? 220 : 210))),
							   label(map->title, {FontRole::Heading}),
							   caption(FormattableString(tr("[maps by %0]")).arg(facts(*map, true)))};
	if (!map->description.empty())
		parts.push_back(paragraph(map->description, {FontRole::Support}));
	parts.push_back(caption(FormattableString(tr("[maps stats %0 %1 %2]"))
								.arg(compactCount(map->plays))
								.arg(compactCount(map->downloads))
								.arg(compactCount(map->likes)),
							false));
	ButtonOptions use;
	use.primary = true;
	use.icon = uiIcon(UIIcon::Map);
	use.enabled = map->latestVersion.has_value() && !download;
	ButtonOptions like;
	like.icon = uiIcon(UIIcon::Heart);
	like.selected = detail && detail->liked;
	like.enabled = data.signedIn && !data.guest;
	like.tooltip = data.guest ? tr("[maps like needs account]") : std::string();
	ButtonOptions page;
	page.icon = uiIcon(UIIcon::ExternalLink);
	ButtonOptions report;
	report.danger = true;
	report.enabled = !(detail && detail->reported) && data.signedIn;
	std::vector<Element> actionsRow;
	if (!phone)
		actionsRow.push_back(button("maps/use", tr("[maps use in room]"), [this] { useInRoom(); }, use));
	actionsRow.push_back(button("maps/like", like.selected ? tr("[maps liked]") : tr("[maps like]"), [this] { toggleLike(); }, like));
	actionsRow.push_back(button("maps/page", tr("[maps map page]"), [this] { openMapPage(); }, page));
	actionsRow.push_back(button("maps/report", detail && detail->reported ? tr("[maps reported]") : tr("[maps report]"),
								[this] { beginReport(); }, report));
	WrapOptions grid;
	grid.minChildWidth = p.pt(120);
	grid.stretch = false;
	parts.push_back(wrap(std::move(actionsRow), grid));
	if (reporting)
	{
		std::vector<Element> reasons;
		for (int i = 0; i < 4; ++i)
			reasons.push_back(button("maps/report/" + std::string(REPORT_REASONS[i]), tr(REPORT_LABELS[i]),
									 [this, i] { sendReport(i); }));
		parts.push_back(card(column({caption(tr("[maps report why]"), false), wrap(std::move(reasons), grid)}, {p.pt(6)}),
							 plainCard(p)));
	}
	return column(std::move(parts), {p.pt(8)});
}

Element OnlineMapsScreen::browseBody(const Presentation &p, bool phone)
{
	TextFieldOptions search;
	search.placeholder = tr("[maps search placeholder]");
	search.commitOnSubmit = true;
	search.submit = [this](const std::string &text) { setSearch(text); };
	Element searchField = textField("maps/search", query.search, [this](const std::string &text) { setSearch(text); }, search);
	std::vector<Element> cards;
	for (int i = 0; i < int(data.browse.size()); ++i)
		cards.push_back(mapCard(i, p, phone));
	if (!cursor[0].empty())
		cards.push_back(button("maps/more", tr("[online load more]"), [this] { loadMore(); }));
	Element grid = cards.empty()
					   ? paragraph(loading ? tr("[online loading]") : problem.empty() ? tr("[maps none found]") : problem,
								   {FontRole::Body, true})
					   : wrap(std::move(cards), {p.pt(8), p.pt(phone ? 130 : 140), phone ? 2 : 4});
	if (phone)
	{
		if (detailOpen)
			return detailPanel(p, true);
		return column({searchField, grid}, {p.pt(8)});
	}
	auto filters = row({expanded(searchField, 3),
						width(p.pt(150), choice("maps/size", labels(SIZE_LABELS, 4), sizeChoice, [this](int i) { setSize(i); })),
						width(p.pt(150), choice("maps/colonies",
												{tr("[maps any colonies]"), "2", "3", "4", "6", "8"}, coloniesChoice,
												[this](int i) { setColonies(i); })),
						width(p.pt(170), choice("maps/sort", labels(SORT_LABELS, 4), sortChoice, [this](int i) { setSort(i); }))},
					   {p.pt(8), CrossAlign::Center});
	return column({filters, row({expanded(grid, 3), expanded(detailPanel(p, false), 2)}, {p.pt(12), CrossAlign::Start})},
				  {p.pt(10)});
}

Element OnlineMapsScreen::mineRow(const Online::MapInfo &map, const Presentation &p, bool phone)
{
	const auto found = data.details.find(map.id);
	const Online::MapVersionInfo *newest =
		found != data.details.end() && !found->second.versions.empty() ? &found->second.versions.front()
		: map.latestVersion ? &*map.latestVersion
							: nullptr;
	const auto &palette = frontendTheme().palette;
	std::string sub, stateText;
	GAGCore::Color stateColor = palette.success;
	Element hint;
	if (!newest)
	{
		stateText = tr("[maps checking]");
		stateColor = palette.muted;
		sub = tr("[maps no version yet]");
	}
	else if (newest->validation == "invalid")
	{
		stateText = tr("[maps rejected]");
		stateColor = palette.danger;
		sub = FormattableString(tr("[maps did not load %0]")).arg(newest->reason.empty() ? tr("[maps unknown reason]") : newest->reason);
		hint = caption(tr("[maps fix and upload again]"));
	}
	else if (newest->validation == "pending" || newest->preview == "pending")
	{
		stateText = tr("[maps checking]");
		stateColor = palette.muted;
		sub = tr("[maps uploaded just now]");
		hint = caption(tr("[maps loading on server]"));
	}
	else
	{
		stateText = tr(VISIBILITY_LABELS[visibilityIndex(map.visibility)]);
		sub = facts(map, true);
		hint = caption("\xE2\x96\xB6 " + std::string(FormattableString(tr("[maps %0 plays]")).arg(compactCount(map.plays))) +
					   " \xC2\xB7 \xE2\x99\xA5 " + compactCount(map.likes));
	}
	if (map.hidden)
	{
		stateText = tr("[maps hidden]");
		stateColor = palette.danger;
		if (!map.hiddenReason.empty())
			sub = map.hiddenReason;
	}
	const bool rejected = newest && newest->validation == "invalid";
	const bool checking = !newest || newest->validation == "pending";
	std::vector<std::string> visibilityOptions = labels(VISIBILITY_LABELS, 3);
	ChoiceOptions visibilityExtra;
	visibilityExtra.enabled = {!data.guest, true, true};
	Element visibility = choice("maps/mine/" + map.id + "/visibility", visibilityOptions, visibilityIndex(map.visibility),
								[this, id = map.id](int i) { setVisibility(id, VISIBILITIES[i]); }, visibilityExtra);
	ButtonOptions updateOptions;
	updateOptions.enabled = !checking;
	ButtonOptions deleteOptions;
	deleteOptions.danger = true;
	Element actionsElement =
		rejected ? row({button("maps/mine/" + map.id + "/update", tr("[maps upload again]"), [this, id = map.id] { update(id); }),
						button("maps/mine/" + map.id + "/delete", tr("[maps delete]"), [this, id = map.id] { remove(id); }, deleteOptions)},
					   {p.pt(6)})
				 : row({width(p.pt(130), visibility),
						button("maps/mine/" + map.id + "/update", tr("[maps update]"), [this, id = map.id] { update(id); }, updateOptions)},
					   {p.pt(6), CrossAlign::Center});
	Element text = column({label(map.title, {FontRole::Heading}), paragraph(sub, {FontRole::Support, true})}, {p.pt(2)});
	Element picture = previewPicture(previewOf(map), p.pt(phone ? 56 : 60));
	if (phone)
		return column({row({picture, expanded(text), badge(stateText, stateColor)}, {p.pt(8), CrossAlign::Center}),
					   hint ? hint : empty(), actionsElement},
					  {p.pt(6)});
	return row({picture, expanded(text, 3), badge(stateText, stateColor), hint ? width(p.pt(150), hint) : empty(), actionsElement},
			   {p.pt(10), CrossAlign::Center});
}

Element OnlineMapsScreen::mineBody(const Presentation &p, bool phone)
{
	std::vector<Element> rows;
	ButtonOptions uploadOptions;
	uploadOptions.primary = true;
	uploadOptions.icon = uiIcon(UIIcon::Upload);
	uploadOptions.enabled = data.signedIn;
	if (!phone)
		rows.push_back(row({button("maps/upload", tr("[maps upload a map]"), [this] { upload(); }, uploadOptions),
							expanded(caption(tr("[maps upload hint]")))},
						   {p.pt(10), CrossAlign::Center}));
	std::vector<Element> items;
	for (const auto &map : data.mine)
	{
		if (!items.empty())
			items.push_back(divider());
		items.push_back(mineRow(map, p, phone));
	}
	if (items.empty())
		items.push_back(paragraph(loading ? tr("[online loading]") : problem.empty() ? tr("[maps no maps yet]") : problem,
								  {FontRole::Body, true}));
	if (!cursor[1].empty())
		items.push_back(button("maps/more", tr("[online load more]"), [this] { loadMore(); }));
	rows.push_back(card(column(std::move(items), {p.pt(8)}), plainCard(p)));
	return column(std::move(rows), {p.pt(10)});
}

Element OnlineMapsScreen::build(const Presentation &p)
{
	const bool phone = p.touch && p.compact();
	std::vector<Element> body{tab == Tab::Browse ? browseBody(p, phone) : mineBody(p, phone)};
	if (!status.empty())
		body.push_back(caption(status, false));
	const std::string mineLabel = data.mine.empty() ? tr("[maps my maps]")
													: std::string(FormattableString(tr("[maps my maps %0]")).arg(int(data.mine.size())));
	Element tabs = segments("maps/tab", {tr("[maps browse]"), mineLabel}, int(tab),
							[this](int i) { selectTab(i == 0 ? Tab::Browse : Tab::Mine); });

	OnlinePanel panel;
	panel.title = tr("[maps title]");
	panel.headerRight = width(p.pt(phone ? 190 : 300), tabs);
	panel.body = scroll("maps/body", column(std::move(body), {p.pt(10)}));
	panel.note = tab == Tab::Browse
					 ? std::string(FormattableString(tr("[maps browse note %0]")).arg(originHost(data.instance)))
					 : tr("[maps visibility note]");
	panel.actions = {{"back", tr("[goto main menu]"), [this] { endExecute(BACK); }, false, SDLK_ESCAPE}};
	if (phone)
	{
		ButtonOptions backOptions;
		backOptions.icon = uiIcon(UIIcon::Back);
		backOptions.accessibleLabel = tr("[goto main menu]");
		backOptions.shortcut = SDLK_ESCAPE;
		Element backButton = width(p.pt(56), button("back", "", [this] { onEscape(); }, backOptions));
		if (tab == Tab::Mine)
		{
			ButtonOptions uploadOptions;
			uploadOptions.primary = true;
			uploadOptions.icon = uiIcon(UIIcon::Upload);
			panel.thumbBlock = row({backButton, expanded(button("maps/upload", tr("[maps upload a map]"), [this] { upload(); }, uploadOptions))},
								   {p.pt(8)});
		}
		else
		{
			const auto *map = selectedMap();
			ButtonOptions use;
			use.primary = true;
			use.icon = uiIcon(UIIcon::Map);
			use.enabled = map && map->latestVersion && !download;
			const std::string useText = map ? std::string(FormattableString(tr("[maps use %0]")).arg(map->title)) : tr("[maps use in room]");
			std::vector<Element> rows;
			if (!detailOpen)
				rows.push_back(row({expanded(choice("maps/sort", labels(SORT_LABELS, 4), sortChoice, [this](int i) { setSort(i); })),
									expanded(choice("maps/size", labels(SIZE_LABELS, 4), sizeChoice, [this](int i) { setSize(i); }))},
								   {p.pt(8)}));
			rows.push_back(row({backButton, expanded(button("maps/use", useText, [this] { useInRoom(); }, use))}, {p.pt(8)}));
			panel.thumbBlock = column(std::move(rows), {p.pt(8)});
		}
	}
	return onlinePanel(std::move(panel), p);
}

// ================================================================== share

MapShareScreen::MapShareScreen(std::string mapPath, std::string targetMap, std::string knownTitle)
	: path(std::move(mapPath)), mapId(std::move(targetMap)), title(std::move(knownTitle))
{
	fileName = path.substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
	bytes = readMapBytes(path, readError);
	fileSize = bytes.size();
	try
	{
		MapHeader header = Engine().loadMapHeader(path);
		colonies = header.getNumberOfTeams();
		if (title.empty())
			title = header.getMapName();
	}
	catch (const std::exception &)
	{
	}
	if (title.empty())
		title = glob2FilenameToName(fileName);
	preview = std::make_unique<MapPreview>();
	preview->setMapThumbnail(path);
	if (Online::servicesCreated())
	{
		auto &client = Online::services().client;
		guest = client.account() && client.account()->kind == "guest";
	}
}

MapShareScreen::~MapShareScreen() = default;

void MapShareScreen::setTitle(const std::string &value)
{
	title = value.substr(0, 128);
	invalidate();
}

void MapShareScreen::setDescription(const std::string &value)
{
	description = value.substr(0, 4000);
	invalidate();
}

void MapShareScreen::setVisibility(int index)
{
	visibility = std::clamp(index, 0, 2);
	invalidate();
}

void MapShareScreen::submit()
{
	if (share || bytes.empty() || (mapId.empty() && title.empty()))
		return;
	Online::MapShare::Details details;
	details.title = title;
	details.description = description;
	details.visibility = VISIBILITIES[visibility];
	auto &client = onlineClient();
	share = std::make_unique<Online::MapShare>(client, mapId, details, bytes, Online::SimVersion::local().key(), wallClockMs);
	invalidate();
}

void MapShareScreen::close()
{
	const bool shared = share && share->stage() == Online::MapShare::Stage::Ready;
	endExecute(shared ? SHARED : CLOSED);
}

void MapShareScreen::presentProgress(Online::MapShare::Stage stage, const Online::MapVersionInfo &version)
{
	shownStage = stage;
	shownVersion = version;
	invalidate();
}

void MapShareScreen::onTimer(Uint32)
{
	if (!share)
		return;
	const auto before = share->stage();
	share->update();
	if (share->stage() != before || share->stage() == Online::MapShare::Stage::Checking)
		invalidate();
}

Element MapShareScreen::build(const Presentation &p)
{
	const bool phone = p.touch && p.compact();
	const auto stage = shownStage ? shownStage : share ? std::optional(share->stage()) : std::nullopt;
	const auto *version = shownVersion ? &*shownVersion : share && share->version() ? &*share->version() : nullptr;

	std::string sizeLine;
	if (fileSize)
		sizeLine = FormattableString(tr("[maps %0 kb]")).arg(int((fileSize + 1023) / 1024));
	if (colonies > 0)
		sizeLine = std::string(FormattableString(tr("[maps %0 colonies]")).arg(colonies)) +
				   (sizeLine.empty() ? "" : " \xC2\xB7 " + sizeLine);
	Element fileRow = row({mapPreview("share/preview", *preview, phone ? 64 : 96),
						   expanded(column({label(fileName, {FontRole::Heading}), caption(sizeLine)}, {p.pt(2)}))},
						  {p.pt(10), CrossAlign::Center});

	std::vector<Element> parts{fileRow};
	std::vector<MenuAction> buttons;
	if (!stage)
	{
		if (!readError.empty())
			parts.push_back(paragraph(readError, {FontRole::Body, false, TextAlign::Left, frontendTheme().palette.danger}));
		if (mapId.empty())
		{
			parts.push_back(field(tr("[maps field title]"),
								  textField("share/title", title, [this](const std::string &v) { setTitle(v); }, {.maxLength = 128}),
								  {.stacked = true}));
			parts.push_back(field(tr("[maps field description]"),
								  textEditor("share/description", description, [this](const std::string &v) { setDescription(v); },
											 {.lines = 3}),
								  {.stacked = true}));
			std::vector<bool> enabled{!guest, true, true};
			parts.push_back(field(tr("[maps who can use it]"),
								  segments("share/visibility", labels(VISIBILITY_LABELS, 3), visibility,
										   [this](int i) { setVisibility(i); }, enabled),
								  {.stacked = true}));
			parts.push_back(paragraph(tr(guest ? "[maps guests unlisted]" : "[maps visibility help]"), {FontRole::Support, true}));
		}
		else
			parts.push_back(paragraph(FormattableString(tr("[maps new version of %0]")).arg(title), {FontRole::Body}));
		buttons = {{"share/upload", tr("[maps upload]"), [this] { submit(); }, true, SDLK_RETURN, !bytes.empty() && !title.empty()},
				   {"back", tr("[Cancel]"), [this] { close(); }, false, SDLK_ESCAPE}};
	}
	else
	{
		std::string headline, detail;
		GAGCore::Color color = frontendTheme().palette.ink;
		switch (*stage)
		{
		case Online::MapShare::Stage::Creating:
		case Online::MapShare::Stage::Uploading:
			headline = tr("[maps uploading]");
			break;
		case Online::MapShare::Stage::Checking:
			headline = tr("[maps checking]");
			detail = tr("[maps checking detail]");
			break;
		case Online::MapShare::Stage::Ready:
			headline = tr("[maps ready]");
			detail = tr(visibility == 0 ? "[maps ready public]" : visibility == 1 ? "[maps ready unlisted]" : "[maps ready private]");
			color = frontendTheme().palette.success;
			break;
		case Online::MapShare::Stage::Rejected:
			headline = tr("[maps rejected]");
			detail = FormattableString(tr("[maps did not load %0]"))
						 .arg(version && !version->reason.empty() ? version->reason : tr("[maps unknown reason]"));
			color = frontendTheme().palette.danger;
			break;
		case Online::MapShare::Stage::Failed:
			headline = tr("[maps upload failed]");
			detail = share && !share->error().message.empty() ? share->error().message : tr("[online connection problem]");
			color = frontendTheme().palette.danger;
			break;
		}
		parts.push_back(label(headline, {FontRole::Heading, false, TextAlign::Left, color}));
		if (!detail.empty())
			parts.push_back(paragraph(detail, {FontRole::Body}));
		const bool busy = *stage == Online::MapShare::Stage::Creating || *stage == Online::MapShare::Stage::Uploading ||
						  *stage == Online::MapShare::Stage::Checking;
		if (busy)
			parts.push_back(progress(*stage == Online::MapShare::Stage::Checking ? 2 : 1, 3));
		if (version && version->validation == "valid" && version->width && version->height)
			parts.push_back(caption(FormattableString(tr("[maps server read %0 %1]"))
										.arg(sizeText(*version))
										.arg(version->teamCount.value_or(0))));
		buttons = {{"back", busy ? tr("[maps continue in background]") : tr("[ok]"), [this] { close(); }, !busy, SDLK_ESCAPE}};
	}

	OnlinePanel panel;
	panel.title = mapId.empty() ? tr("[maps upload a map title]") : tr("[maps update map title]");
	panel.body = scroll("share/body", column(std::move(parts), {p.pt(10)}));
	panel.note = tr("[maps upload note]");
	panel.actions = std::move(buttons);
	if (phone)
	{
		std::vector<Element> thumb;
		for (const auto &action : panel.actions)
		{
			ButtonOptions options;
			options.primary = action.primary;
			options.enabled = action.enabled;
			options.shortcut = action.shortcut;
			if (action.key == "share/upload")
				options.icon = uiIcon(UIIcon::Upload);
			thumb.insert(thumb.begin(), expanded(button(action.key, action.label, action.action, options)));
		}
		panel.thumbBlock = row(std::move(thumb), {p.pt(8)});
	}
	if (!p.touch)
		return center(maxWidth(p.pt(720), onlinePanel(std::move(panel), p)));
	return onlinePanel(std::move(panel), p);
}
