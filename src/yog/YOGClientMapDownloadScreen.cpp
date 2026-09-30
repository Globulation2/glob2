// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#include "YOGClientMapDownloadScreen.h"
#include "ChooseMapScreen.h"
#include "GUIMapPreview.h"
#include "GlobalContainer.h"
#include "TextSort.h"
#include "YOGClient.h"
#include "YOGClientDownloadableMapList.h"
#include "YOGClientDownloadingMapScreen.h"
#include "YOGClientMapUploadScreen.h"
#include "YOGClientRatedMapList.h"
#include <FormatableString.h>
#include <ScreenStack.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

YOGClientMapDownloadScreen::YOGClientMapDownloadScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client) : client(client), screens(screens)
{
	client->getDownloadableMapList()->addListener(this);
}

YOGClientMapDownloadScreen::~YOGClientMapDownloadScreen() { client->getDownloadableMapList()->removeListener(this); }

std::string YOGClientMapDownloadScreen::title() const { return fe::tr("[Download Maps]"); }

std::string YOGClientMapDownloadScreen::selectedMap() const
{
	return selected >= 0 && selected < int(mapNames.size()) ? mapNames[std::size_t(selected)] : std::string();
}

void YOGClientMapDownloadScreen::onTimer(Uint32)
{
	const bool nowWaiting = client->getDownloadableMapList()->waitingForListFromServer();
	if (nowWaiting != waiting)
	{
		waiting = nowWaiting;
		refresh();
	}
	updateMapPreview();
}

void YOGClientMapDownloadScreen::onActivated()
{
	if (!mapsRequested)
		requestMaps();
	if (!mapPreview && session)
	{
		mapPreview = std::make_unique<MapPreview>();
		mapPreview->retry = [this]
		{
			if (selectedMap().empty())
				return;
			client->getDownloadableMapList()->requestThumbnail(selectedMap(), true);
			updateMapPreview();
		};
	}
	refresh();
}

void YOGClientMapDownloadScreen::mapListUpdated()
{
	const std::string previous = selectedMap();
	std::vector<YOGDownloadableMapInfo> maps = client->getDownloadableMapList()->getDownloadableMapList();
	std::sort(maps.begin(), maps.end(), MapListSorter(static_cast<MapListSorter::SortMethod>(sortMethod)));
	mapNames.clear();
	for (const auto &map : maps)
		mapNames.push_back(map.getMapHeader().getMapName());
	selected = -1;
	for (std::size_t i = 0; i < mapNames.size(); ++i)
		if (mapNames[i] == previous)
			selected = int(i);
	updateMapPreview();
	refresh();
}

void YOGClientMapDownloadScreen::mapThumbnailsUpdated()
{
	updateMapPreview();
	refresh();
}

void YOGClientMapDownloadScreen::requestMaps()
{
	mapNames.clear();
	selected = -1;
	updateMapPreview();
	client->getDownloadableMapList()->requestMapListUpdate();
	mapsRequested = true;
	refresh();
}

void YOGClientMapDownloadScreen::updateMapPreview()
{
	if (!mapPreview)
		return;
	const auto name = selectedMap();
	if (name.empty())
	{
		mapPreview->setState(MapPreview::State::Empty);
		return;
	}
	MapThumbnail &thumbnail = client->getDownloadableMapList()->getMapThumbnail(name);
	if (thumbnail.isLoaded())
		mapPreview->setMapThumbnail(thumbnail);
	else
	{
		if (client->getDownloadableMapList()->getThumbnailState(name) != YOGClientDownloadableMapList::ThumbnailState::Failed)
			client->getDownloadableMapList()->requestThumbnail(name);
		auto state = client->getDownloadableMapList()->getThumbnailState(name);
		mapPreview->setState(state == YOGClientDownloadableMapList::ThumbnailState::Failed ? MapPreview::State::Failed : MapPreview::State::Loading);
	}
}

void YOGClientMapDownloadScreen::uploadMap()
{
	screens.push(std::make_unique<ChooseMapScreen>("maps", "map", false),
				 [this](GAGGUI::Screen &selection, int rc)
				 {
					 if (rc != ChooseMapScreen::OK)
						 return;
					 const auto file = static_cast<ChooseMapScreen &>(selection).getMapHeader().getFileName();
					 screens.push(std::make_unique<YOGClientMapUploadScreen>(screens, client, file), [this](GAGGUI::Screen &, int) { requestMaps(); });
				 });
}

void YOGClientMapDownloadScreen::downloadSelected()
{
	const auto name = selectedMap();
	if (name.empty())
		return;
	screens.push(std::make_unique<YOGClientDownloadingMapScreen>(screens, client, client->getDownloadableMapList()->getMap(name)));
}

Element YOGClientMapDownloadScreen::build(const Presentation &p)
{
	auto &strings = *Toolkit::getStringTable();
	const auto name = selectedMap();
	fe::ListOptions listOptions;
	listOptions.visibleRows = 10;
	listOptions.emptyText = waiting ? strings.getString("[loading map list]") : fe::tr("[No items]");
	listOptions.activate = [this](int) { downloadSelected(); };
	auto list = fe::listView("maps/list", mapNames, selected,
							 [this](int i)
							 {
								 selected = i;
								 updateMapPreview();
							 },
							 listOptions);
	auto sort = fe::field(strings.getString("[Sort By]"),
						  fe::choice("maps/sort", {strings.getString("[sort by name]"), strings.getString("[sort by size]"), strings.getString("[sort by rating]")}, sortMethod,
									 [this](int v)
									 {
										 sortMethod = v;
										 mapListUpdated();
									 }));
	std::vector<Element> details;
	if (mapPreview)
		details.push_back(fe::center(fe::mapPreview("maps/preview", *mapPreview, 160)));
	if (!name.empty())
	{
		const YOGDownloadableMapInfo info = client->getDownloadableMapList()->getMap(name);
		const MapHeader &header = info.getMapHeader();
		details.push_back(fe::label(header.getMapName()));
		details.push_back(fe::caption(FormattableString("%0%1").arg(header.getNumberOfTeams()).arg(strings.getString("[teams]"))));
		details.push_back(fe::caption(FormattableString("%0 x %1").arg(info.getWidth()).arg(info.getHeight())));
		details.push_back(fe::caption(info.getAuthorName()));
		details.push_back(fe::caption(info.getNumberOfRatings() > 5 ? FormattableString(strings.getString("[Rated %0]")).arg(info.getRatingTotal() / info.getNumberOfRatings())
																	 : std::string(FormattableString(strings.getString("[Not Enough Ratings]")))));
		details.push_back(fe::caption(FormattableString("%0 kb").arg((info.getSize() + 512) / 1024)));
		if (client->getRatedMapList()->isMapRated(name))
			details.push_back(fe::caption(strings.getString("[map rated]")));
		else
			details.push_back(fe::row({fe::expanded(fe::stepper("maps/rating", rating, 1, 10, [this](int v) { rating = v; })),
									   fe::button("maps/rate", strings.getString("[submit rating]"),
												  [this, name]
												  {
													  client->getDownloadableMapList()->submitRating(name, rating);
													  client->getRatedMapList()->addRatedMap(name);
												  })},
									  {p.pt(6), fe::CrossAlign::Center}));
	}
	auto detailColumn = fe::column(std::move(details), {p.pt(6)});
	auto listColumn = fe::column({sort, list}, {p.pt(6)});
	Element body = fe::adaptive(
		[listColumn, detailColumn](const fe::LayoutContext &ctx, fe::Size available) -> fe::Element
		{
			if (available.w < ctx.presentation.pt(640))
				return fe::scroll("maps/scroll", fe::column({listColumn, detailColumn}, {ctx.presentation.pt(12)}));
			return fe::row({fe::expanded(fe::scroll("maps/scroll", listColumn)), fe::width(ctx.presentation.pt(260), fe::scroll("maps/details", detailColumn))},
						   {ctx.presentation.pt(16), fe::CrossAlign::Stretch});
		});
	return fe::column({fe::expanded(body), fe::divider(),
					   fe::actions({{"maps/refresh", strings.getString("[refresh map list]"), [this] { requestMaps(); }},
									{"maps/download", strings.getString("[Download Map]"), [this] { downloadSelected(); }, true, SDLK_UNKNOWN, !name.empty()},
									{"maps/upload", strings.getString("[upload map]"), [this] { uploadMap(); }},
									{"maps/quit", strings.getString("[quit]"), [this] { finish(QUIT); }, false, SDLK_ESCAPE}},
								   p)},
					  {p.pt(8)});
}

MapListSorter::MapListSorter(SortMethod sortMethod)
	: sortMethod(sortMethod)
{

}



bool MapListSorter::operator()(const YOGDownloadableMapInfo& lhs, const YOGDownloadableMapInfo& rhs)
{
	switch(sortMethod)
	{
	default:
	case Name:
		return GAGCore::naturalStringSort(lhs.getMapHeader().getMapName(), rhs.getMapHeader().getMapName());
	case Size:
		{
			int lw = lhs.getWidth();
			int lh = lhs.getHeight();
			int la = lw*lh;
			int rw = rhs.getWidth();
			int rh = rhs.getHeight();
			int ra = lw*rh;
			
			if(la==ra)
			{
				if(lw == rw)
				{
					return GAGCore::naturalStringSort(lhs.getMapHeader().getMapName(), rhs.getMapHeader().getMapName());
				}
				else
				{
					return lw > rw;
				}
			}
			else
			{
				return la > ra;
			}
		}
	case Rating:
		{
			int lt = lhs.getRatingTotal();
			int ln = lhs.getNumberOfRatings();
			int rt = rhs.getRatingTotal();
			int rn = rhs.getNumberOfRatings();
			
			if(lt>5 && rt>5 && lt/ln!=rt/rn)
			{
				return lt/ln > rt/rn;
			}
			else
			{
				return GAGCore::naturalStringSort(lhs.getMapHeader().getMapName(), rhs.getMapHeader().getMapName());
			}
		}
	}
}
