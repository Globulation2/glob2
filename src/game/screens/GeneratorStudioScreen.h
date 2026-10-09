// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIBase.h>
#include "GUIMapPreview.h"
#include "MapHeader.h"
#include "GameHeader.h"
#include <atomic>
#include <memory>
#include <thread>

// Embedded generator authoring preview. Owns one frozen world for its whole life.
class GeneratorStudioScreen : public GAGGUI::Screen
{
  public:
	GeneratorStudioScreen(std::string packagePath = "/tmp/generator-studio.json",
						  std::string settingsPath = "/tmp/generator-studio-settings.json");
	~GeneratorStudioScreen() override;
	const char *recordingId() const override { return "generator_studio"; }
	bool usesResponsiveViewport() const override { return true; }
	bool supportsCompactViewport() const override { return true; }
	void updateExecution(Uint32 tick) override;
	void handleExecutionEvent(SDL_Event event) override;
	void paint() override;
	Uint32 executionDelay(Uint32, Uint32) override { return 16; }
	std::shared_ptr<std::string> snapshot;
	MapHeader map;
	GameHeader players;

  private:
	friend struct MapPreviewHarness;
	const std::string packagePath, settingsPath;
	MapPreview preview;
	std::thread worker;
	std::atomic<bool> complete{false};
	bool started = false, delivered = false, playable = false;
	std::string report, error;
	MapThumbnail terrain;
	std::vector<MapStart> starts;
	void generate();
};
