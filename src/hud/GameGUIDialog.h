// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "ui/FrontendUI.h"
#include "Team.h"
#include <GraphicContext.h>
#include <array>
#include <optional>
#include <string>
#include <vector>

class GameGUI;
class GameHeader;

// The in-match dialogs. Each is a declarative panel hosted by GameGUI; it ends
// with finish(code) and GameGUI reads result() to act, so the commands emitted
// for a choice are unchanged from the classic dialogs.

class InGameMainScreen : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "in_game_main"; }
	enum
	{
		LOAD_GAME = 0,
		SAVE_GAME = 1,
		OPTIONS = 2,
		RETURN_GAME = 5,
		QUIT_GAME = 6,
		PAUSE_GAME = 7,
		HIVE_MIND = 8,
		AI_TELEMETRY = 9
	};
	explicit InGameMainScreen(bool isReplay = false, bool canSave = true, bool paused = false);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	/// A networked (turn-protocol) game: no Load or Save, and "Leave match" instead
	/// of "Quit the game".
	void setNetworked(bool value) { networked = value; }
	/// Network matches: what the Pause item says and whether it can be used (a
	/// queue match's pause limit, TurnLockstepSession). Pause shows on both looks there.
	void setPauseOffer(std::string label, bool enabled)
	{
		pauseLabel = std::move(label);
		pauseEnabled = enabled;
	}
	void setHiveMind(bool value) { hiveMind = value; }

  protected:
	void onEscape() override { finish(RETURN_GAME); }
	double maxWidth() const override { return -1; }

  private:
	bool replay, canSave, paused;
	bool networked = false, hiveMind = false;
	std::string pauseLabel;
	bool pauseEnabled = true;
};

/// A yes/no question over the game ("Leave match?"), Cancel on Escape.
class InGameConfirmScreen : public Glob2UI::InGameDialog
{
  public:
	enum
	{
		CANCEL = 0,
		CONFIRM = 1
	};
	InGameConfirmScreen(std::string title, std::string body, std::string confirmLabel, std::string cancelLabel);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

  protected:
	void onEscape() override { finish(CANCEL); }
	double maxWidth() const override { return -1; }

  private:
	std::string title, body, confirmLabel, cancelLabel;
};

class InGameEndOfGameScreen : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "in_game_end_of_game"; }
	enum
	{
		QUIT = 0,
		CONTINUE = 1,
		WATCH_AGAIN = 2
	};
	const std::string title;
	const bool canContinue;
	InGameEndOfGameScreen(std::string title, bool canContinue, std::optional<GAGCore::Color> teamColor = {},
						  bool won = false);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

  protected:
	void onEscape() override { finish(canContinue ? CONTINUE : QUIT); }
	double maxWidth() const override { return -1; }

  private:
	std::optional<GAGCore::Color> teamColor;
	bool won;
};

class InGameAllianceScreen : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "in_game_alliance"; }
	enum
	{
		OK = 0
	};
	// One row per player; the local team's own players are not listed.
	struct Entry
	{
		int player = 0, team = 0;
		std::string name;
		GAGCore::Color color;
		bool alliance = false, normalVision = false, foodVision = false, marketVision = false, chat = false;
		// Alliance and vision are locked in fixed-team matches.
		bool diplomacy = true;
	};
	explicit InGameAllianceScreen(GameGUI *gameGUI);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	static int countNumberPlayersForLocalTeam(GameHeader &gameHeader, int localteam);
	Uint32 getAlliedMask() const;
	Uint32 getEnemyMask() const;
	Uint32 getExchangeVisionMask() const;
	Uint32 getFoodVisionMask() const;
	Uint32 getOtherVisionMask() const;
	Uint32 getChatMask() const;
	const std::vector<Entry> &entries() const { return rows; }
	// Harness entry point: flips one of the five settings of a listed player.
	enum Setting
	{
		Alliance,
		NormalVision,
		FoodVision,
		MarketVision,
		Chat
	};
	void set(int player, Setting setting, bool value);

  protected:
	void onEscape() override { finish(OK); }
	double maxWidth() const override { return 640; }
	GAGGUI::ui::Rect available(const Glob2UI::Presentation &p, const GAGGUI::ui::Metrics &m) override
	{ return insetAvailable(p, m); }

  private:
	GameGUI *gameGUI;
	std::vector<Entry> rows;
	bool editable = true;
	int players = 0;
	// Settings of every player of the local team, kept for the masks.
	std::array<bool, Team::MAX_COUNT> ownAlliance{}, ownNormal{}, ownFood{}, ownMarket{}, ownChat{};
	std::array<int, Team::MAX_COUNT> teamOf{};
	bool &field(Entry &entry, Setting setting) const;
	// Players of one team share alliance and vision.
	void mirror(int player, Setting setting);
};

class InGameOptionScreen : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "in_game_option"; }
	enum
	{
		OK = 0,
		MUTE = 1,
	};
	explicit InGameOptionScreen(GameGUI *gameGUI);
	~InGameOptionScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	std::string gameSpeedText() const;
	bool adjustableGameSpeed;
	// Harness entry points mirroring the controls.
	void setMute(bool value);
	void setGameSpeed(int speed);
	bool setMusicSet(const std::string &name);

  protected:
	void onEscape() override { finish(OK); }
	double maxWidth() const override { return -1; }

  private:
	GameGUI *gameGUI;
	void applyVolume();
	std::string musicSetStatus;
};

//! The chat composer shown while typing a message in game: Return sends
//! (result 0), Escape closes (result 1). Recipients come from the Teams dialog.
class InGameTextInput : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "in_game_text_input"; }
	explicit InGameTextInput(bool commander = false);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	std::string getText() const { return text; }
	void setText(const std::string &value)
	{
		text = value;
		invalidate();
	}

  protected:
	bool scrim() const override { return false; }
	void onEscape() override { finish(1); }
	double maxWidth() const override { return 560; }
	GAGGUI::ui::Rect available(const GAGGUI::ui::Presentation &presentation, const GAGGUI::ui::Metrics &metrics) override;
	GAGGUI::ui::Rect place(GAGGUI::ui::Size measured, GAGGUI::ui::Rect area) override;

  private:
	std::string text;
	bool commander = false;
};

///This screen shows the current objectives of the mission, a mission briefing, and
///hints as the mission goes along
class InGameObjectivesScreen : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "in_game_objectives"; }
	enum
	{
		OBJECTIVES = 1,
		BRIEFING = 2,
		HINTS = 3,
		OK = 4,
	};
	//If show briefing is enabled, then the briefing tab will be shown rather than the objectives tab
	InGameObjectivesScreen(GameGUI *gui, bool showBriefing);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void showTab(int tab);
	int tab() const { return page; }

  protected:
	void onEscape() override { finish(OK); }
	double maxWidth() const override { return 560; }
	GAGGUI::ui::Rect available(const Glob2UI::Presentation &p, const GAGGUI::ui::Metrics &m) override
	{ return insetAvailable(p, m); }

  private:
	struct Line
	{
		std::string text;
		int state = -1; // -1 plain, 0 open, 1 complete, 2 failed
	};
	int page;
	std::string briefing;
	std::vector<Line> primary, secondary, hints;
	bool hasSecondary = false;
};

// Reads only access-filtered immutable Scene data.
class InGameAITelemetryScreen : public Glob2UI::InGameDialog
{
  public:
	explicit InGameAITelemetryScreen(GameGUI *gui) : gui(gui) {}
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;

  protected:
	void onEscape() override { finish(0); }
	void onUpdate(Uint32) override;
	double maxWidth() const override { return 720; }
	bool fillHeight() const override { return true; }

  private:
	GameGUI *gui;
	int player = -1;
	Uint32 sample = ~0u, accessiblePlayers = 0;
	std::string search, selectedField;
	void resetFieldSelection();
};
