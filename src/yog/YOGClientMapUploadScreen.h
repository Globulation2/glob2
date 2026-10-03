// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#pragma once
#include "YOGClientMapUploader.h"
#include "ui/FrontendUI.h"
#include <memory>
#include <string>

class YOGClient;
class MapPreview;
namespace GAGGUI
{
class ScreenStack;
}

/// Uploads a map to YOG with a name and author.
class YOGClientMapUploadScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "yogclient_map_upload"; }
	YOGClientMapUploadScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client, const std::string mapFile);
	~YOGClientMapUploadScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;

	enum
	{
		CANCEL,
		UPLOAD,
		UPLOADFAILED,
		UPLOADFINISHED,
		CONNECTIONLOST,
	};

  protected:
	void onEscape() override { cancel(); }

  private:
	void showError(const char *key, int result);
	void cancel();
	void upload();
	GAGGUI::ScreenStack &screens;
	std::unique_ptr<MapPreview> preview;
	std::shared_ptr<YOGClient> client;
	YOGClientMapUploader uploader;
	std::string mapFile, mapName, authorName;
	std::string mapInfo, mapVersion, mapSize, uploadStatusText;
	int uploadPercent = -1;
	bool isUploading = false;
};
