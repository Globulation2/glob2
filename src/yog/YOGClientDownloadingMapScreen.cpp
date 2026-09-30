// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#include "YOGClientDownloadingMapScreen.h"
#include "FormatableString.h"
#include "GUIMapPreview.h"
#include "MapHeader.h"
#include "MessageScreen.h"
#include "YOGClient.h"
#include "YOGClientDownloadableMapList.h"
#include <ScreenStack.h>

using namespace Glob2UI;

YOGClientDownloadingMapScreen::YOGClientDownloadingMapScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client, const YOGDownloadableMapInfo &info)
	: screens(screens), info(info), client(client), downloader(client)
{
	preview = std::make_unique<MapPreview>();
	preview->setState(MapPreview::State::Loading);
	preview->retry = [this]
	{ this->client->getDownloadableMapList()->requestThumbnail(this->info.getMapHeader().getMapName(), true); };
	client->getDownloadableMapList()->requestThumbnail(info.getMapHeader().getMapName());
	const MapHeader mapHeader = info.getMapHeader();
	mapName = mapHeader.getMapName();
	mapInfo = GAGCore::FormattableString("%0%1").arg(mapHeader.getNumberOfTeams()).arg(tr("[teams]"));
	mapSize = GAGCore::FormattableString("%0 x %1").arg(info.getWidth()).arg(info.getHeight());
	authorName = info.getAuthorName();
	downloader.startDownloading(info);
}

YOGClientDownloadingMapScreen::~YOGClientDownloadingMapScreen() { downloader.cancelDownload(); }

void YOGClientDownloadingMapScreen::cancel()
{
	downloader.cancelDownload();
	endExecute(CANCEL);
}

Element YOGClientDownloadingMapScreen::build(const Presentation &p)
{
	auto details = column({paragraph(mapName), paragraph(mapInfo), paragraph(mapSize), paragraph(authorName)});
	auto previewElement = Glob2UI::mapPreview("preview", *preview, 160);
	std::vector<Element> parts;
	parts.push_back(adaptive(
		[details, previewElement](const LayoutContext &ctx, Size available)
		{
			if (available.w < ctx.presentation.pt(560))
				return column({center(previewElement), details});
			return row({previewElement, expanded(details)}, {-1, CrossAlign::Start});
		}));
	if (downloadPercent >= 0)
		parts.push_back(progress(downloadPercent, 100));
	return page(tr("[downloading map]"), scroll("download/scroll", column(std::move(parts))),
				actions({{"cancel", tr("[Cancel]"), [this] { cancel(); }, false, SDLK_ESCAPE}}, p), p, 720);
}

void YOGClientDownloadingMapScreen::onTimer(Uint32)
{
	client->update();
	downloader.update();
	if (!client->isConnected())
	{
		screens.push(std::make_unique<MessageScreen>(tr("[Map download failure: lost connection]"), std::vector<std::string>{tr("[ok]")}),
					 [this](GAGGUI::Screen &, int) { endExecute(CONNECTIONLOST); });
		return;
	}
	int percent = -1;
	if (downloader.getDownloadingState() == YOGClientMapDownloader::DownloadingMap)
	{
		percent = downloader.getPercentDownloaded();
		if (percent == 100)
			percent = 0;
	}
	else if (downloader.getDownloadingState() == YOGClientMapDownloader::Finished)
	{
		endExecute(FINISHED);
		return;
	}
	if (percent != downloadPercent)
	{
		downloadPercent = percent;
		invalidate();
	}
	if (!preview->isThumbnailLoaded())
	{
		MapThumbnail &thumbnail = client->getDownloadableMapList()->getMapThumbnail(info.getMapHeader().getMapName());
		if (thumbnail.isLoaded())
		{
			preview->setMapThumbnail(thumbnail);
			mapSize = GAGCore::FormattableString("%0 x %1").arg(preview->getLastWidth()).arg(preview->getLastHeight());
			invalidate();
		}
		else
		{
			auto state = client->getDownloadableMapList()->getThumbnailState(info.getMapHeader().getMapName());
			preview->setState(state == YOGClientDownloadableMapList::ThumbnailState::Failed ? MapPreview::State::Failed : MapPreview::State::Loading);
		}
	}
}
