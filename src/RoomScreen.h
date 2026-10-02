// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#pragma once
#include "RoomBackend.h"
#include "ui/FrontendUI.h"
#include <ScreenStack.h>
#include <deque>
#include <memory>
#include <optional>
#include <string>

class MapPreview;

// The Room (multiplayer mock-up 2): one screen for online rooms (PlatformRoom) and
// LAN rooms (LanRoom) over RoomBackend. The left side is the custom-game lobby's
// Map / Players & Teams / Game Rules tabs, with seats that hold people; the right
// side is what a room adds: the invite (or, on a LAN, how to join), chat and the
// ready state. Phones get a Seats / Map / Rules / Chat bar at the thumb and the
// primary action (Start for the host, Ready for guests) in the thumb corner,
// mirrored by Settings > thumb side.
//
// The host edits the map and rules with the custom-game screen itself
// (CustomGameScreen in room mode), so the generator and rule components are the
// same ones single player uses.
class RoomScreen : public Glob2UI::Screen
{
  public:
	RoomScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<RoomBackend> room);
	~RoomScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;

	// Semantic entry points shared with harnesses.
	enum Tab
	{
		MapTab,
		PlayersTab,
		RulesTab,
		ChatTab // phones only
	};
	void selectTab(int tab);
	int tab() const { return currentTab; }
	void openSeat(int seat);
	void sendChat(const std::string &text);
	void editSetup(int customGameTab);
	RoomBackend &backend() { return *room; }

  protected:
	void onEscape() override;

  private:
	struct ChatLine
	{
		std::string author, text;
		bool system = false;
	};
	GAGGUI::ScreenStack &screens;
	std::shared_ptr<RoomBackend> room;
	// A catalog map from "Use in a room" (OnlineMapsScreen), applied once the room
	// is ready and only by its host: {hash, mapId}.
	std::optional<std::pair<std::string, std::string>> pendingCatalogMap;
	std::deque<ChatLine> chat;
	std::string chatDraft, notice;
	int currentTab = PlayersTab;
	int unread = 0;
	int selectedSeat = -1;
	Uint32 lastChatAt = 0, now = 0;
	bool launched = false, closing = false;
	std::unique_ptr<MapPreview> preview;
	std::string previewFile;

	void handle(const RoomBackend::Event &event);
	void launch();
	void finish(int code, const std::string &message);
	void leave();
	Glob2UI::Element header(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element tabs(const Glob2UI::Presentation &p);
	Glob2UI::Element seats(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element seatControls(const RoomBackend::Slot &slot, const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element mapPanel(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element rulesPanel(const Glob2UI::Presentation &p);
	Glob2UI::Element invite(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element chatPanel(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element primaryActions(const Glob2UI::Presentation &p, bool phone);
	std::string chatText() const;
};
