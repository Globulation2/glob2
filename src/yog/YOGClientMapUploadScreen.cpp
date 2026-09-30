// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#include "YOGClientMapUploadScreen.h"
#include "Engine.h"
#include "FormatableString.h"
#include "GUIMapPreview.h"
#include "GlobalContainer.h"
#include "MapHeader.h"
#include "MessageScreen.h"
#include "YOGClient.h"
#include <ScreenStack.h>

using namespace Glob2UI;

YOGClientMapUploadScreen::YOGClientMapUploadScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client, const std::string mapFile)
	: screens(screens), client(client), uploader(client), mapFile(mapFile), authorName(globalContainer->settings.getUsername())
{
	preview = std::make_unique<MapPreview>();
	preview->setMapThumbnail(mapFile.c_str());
	Engine engine;
	MapHeader mapHeader = engine.loadMapHeader(mapFile);
	mapName = mapHeader.getMapName();
	mapInfo = GAGCore::FormattableString("%0%1").arg(mapHeader.getNumberOfTeams()).arg(tr("[teams]"));
	mapVersion = GAGCore::FormattableString("%0 %1.%2").arg(tr("[Version]")).arg(mapHeader.getVersionMajor()).arg(mapHeader.getVersionMinor());
	mapSize = GAGCore::FormattableString("%0 x %1").arg(preview->getLastWidth()).arg(preview->getLastHeight());
}

YOGClientMapUploadScreen::~YOGClientMapUploadScreen() { uploader.cancelUpload(); }

Element YOGClientMapUploadScreen::build(const Presentation &p)
{
	auto details = column({field(tr("[Upload Map]"), textField("name", mapName, [this](const std::string &v) { mapName = v; }, {false, 255, "", {}, false, !isUploading})),
						   paragraph(mapInfo), paragraph(mapVersion), paragraph(mapSize),
						   field(tr("[Map Upload: Author Name]"), textField("author", authorName, [this](const std::string &v) { authorName = v; }, {false, 255, "", {}, false, !isUploading}))});
	auto previewElement = Glob2UI::mapPreview("preview", *preview, 160);
	std::vector<Element> parts;
	parts.push_back(adaptive(
		[details, previewElement](const LayoutContext &ctx, Size available)
		{
			if (available.w < ctx.presentation.pt(560))
				return column({center(previewElement), details});
			return row({previewElement, expanded(details)}, {-1, CrossAlign::Start});
		}));
	if (uploadPercent >= 0)
		parts.push_back(progress(uploadPercent, 100));
	if (!uploadStatusText.empty())
		parts.push_back(paragraph(uploadStatusText, {FontRole::Body, false, TextAlign::Center}));
	return page(tr("[Upload Map]"), scroll("upload/scroll", column(std::move(parts))),
				actions({{"upload", tr("[Upload Map]"), [this] { upload(); }, true, SDLK_RETURN, !isUploading},
						 {"cancel", tr("[Cancel]"), [this] { cancel(); }, false, SDLK_ESCAPE}},
						p),
				p, 720);
}

void YOGClientMapUploadScreen::cancel()
{
	client->update();
	uploader.cancelUpload();
	endExecute(CANCEL);
}

void YOGClientMapUploadScreen::upload()
{
	if (isUploading)
		return;
	uploader.startUploading(mapFile.c_str(), mapName, authorName, preview->getLastWidth(), preview->getLastHeight());
	isUploading = true;
	invalidate();
}

void YOGClientMapUploadScreen::showError(const char *key, int result)
{
	screens.push(std::make_unique<MessageScreen>(tr(key), std::vector<std::string>{tr("[ok]")}),
				 [this, result](GAGGUI::Screen &, int) { endExecute(result); });
}

void YOGClientMapUploadScreen::onTimer(Uint32)
{
	client->update();
	uploader.update();
	if (!client->isConnected())
	{
		showError("[Map upload failure: connection lost]", CONNECTIONLOST);
		return;
	}
	int percent = -1;
	std::string statusText;
	if (isUploading)
	{
		const auto state = uploader.getUploadingState();
		if (state == YOGClientMapUploader::Nothing)
		{
			showError(uploader.getRefusalReason() == YOGMapUploadReasonMapNameAlreadyExists
						  ? "[Map upload failure: map name in use]"
						  : "[Map upload failure: unknown reason]",
					  UPLOADFAILED);
			return;
		}
		if (state == YOGClientMapUploader::WaitingForUploadReply)
			statusText = tr("[Map Upload: Waiting for reply]");
		else if (state == YOGClientMapUploader::Finished)
		{
			endExecute(UPLOADFINISHED);
			return;
		}
		else
			percent = uploader.getPercentUploaded();
	}
	if (percent != uploadPercent || statusText != uploadStatusText)
	{
		uploadPercent = percent;
		uploadStatusText = statusText;
		invalidate();
	}
}
