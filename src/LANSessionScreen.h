// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include <memory>
#include <optional>
namespace Lan
{
class LanRoom;
}
namespace GAGGUI
{
class ScreenStack;
}

// Owns a LAN room: shows progress while a guest connects to the host, then the room
// (MultiplayerGameScreen on the LanRoom backend). A hosted room is ready at once.
// When the room ends with an explanation (the host left, a refusal, a lost
// connection), the screen shows it before returning.
class LANSessionScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "lansession"; }
	LANSessionScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<Lan::LanRoom> room);
	~LANSessionScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;
	Lan::LanRoom &room() { return *lanRoom; }

  protected:
	void onEscape() override { endExecute(0); }

  private:
	enum class Stage
	{
		Connecting,
		Lobby,
		Failed
	};
	void fail(const std::string &message);
	void enterLobby();
	GAGGUI::ScreenStack &screens;
	std::shared_ptr<Lan::LanRoom> lanRoom;
	Stage stage = Stage::Connecting;
	std::optional<Uint32> stageStarted;
};
