// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "MapHeader.h"
#include <memory>
#include <optional>
class YOGClient;
namespace GAGGUI
{
class ScreenStack;
}

// Owns an asynchronous LAN handshake and its lobby. The client is already
// connecting and may own an attached in-process server for hosting.
class LANSessionScreen : public Glob2UI::Screen
{
  public:
    const char* recordingId() const override { return "lansession"; }
	LANSessionScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<YOGClient> client,
					 std::string username, std::optional<MapHeader> hostedMap = {});
	~LANSessionScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;

  protected:
	void onEscape() override { endExecute(0); }

  private:
	enum class Stage
	{
		Greeting,
		Login,
		GameList,
		Lobby,
		Failed
	};
	void fail(const char *message);
	void enterLobby();
	GAGGUI::ScreenStack &screens;
	std::shared_ptr<YOGClient> client;
	std::string username;
	std::optional<MapHeader> hostedMap;
	Stage stage = Stage::Greeting;
	std::optional<Uint32> stageStarted;
};
