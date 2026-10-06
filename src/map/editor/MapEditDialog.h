// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006-2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Team.h"
#include "map/TerrainRegistry.h"
#include "ui/FrontendUI.h"
#include <string>

class Game;

// Editor actions presented through the shared responsive dialog framework.
class MapEditMenuScreen : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "map_edit_menu"; }
	MapEditMenuScreen();
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

	enum
	{
		LOAD_MAP,
		SAVE_MAP,
		REROLL_TERRAIN_LOOK,
		OPEN_SCRIPT_EDITOR,
		OPEN_TEAMS_EDITOR,
		RETURN_EDITOR,
		QUIT_EDITOR,
		SHARE_MAP,
		IMPORT_TERRAIN,
		TERRAIN_PALETTE
	};

  protected:
	void onEscape() override { finish(RETURN_EDITOR); }
	double maxWidth() const override { return -1; }
};

///This is a text info box. It is used primarily for entering the names of the script areas,
///however it is generic enough to be recycled for other purposes.
class AskForTextInput : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "ask_for_text_input"; }
	enum
	{
		OK,
		CANCEL
	};
	AskForTextInput(const std::string &label, const std::string &current);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	// The confirmed text after OK; the original text after Cancel.
	std::string getText() const;
	// The draft as typed so far.
	const std::string &draft() const { return currentText; }
	void setText(const std::string &value);
	void confirm();

  protected:
	void onEscape() override { finish(CANCEL); }
	double maxWidth() const override { return -1; }

  private:
	std::string labelText;
	std::string originalText, currentText;
};

///This is the teams editor screen. This is the editor that allows the map creator to choose alliances and arrange teams
///in the map. This is primarily for campaign missions since these settings are overridden for custom games
class TeamsEditor : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "teams_editor"; }
	explicit TeamsEditor(Game *game);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	///Rebuilds the game's GameHeader from scratch out of the slot state
	///(it does not mutate the existing header). Active slots are packed
	///into consecutive player numbers; each slot's ally-team index
	///(0-based) is written back as ally team number index + 1, the
	///inverse of allyTeamNumberToWidgetIndex().
	void generateGameHeader();

	enum
	{
		OK,
		CANCEL
	};
	struct Slot
	{
		bool active = false;
		int color = 0, ai = 0, ally = 0;
	};
	const Slot &slot(int index) const { return slots[index]; }
	void setActive(int index, bool active);
	void setColor(int index, int color);
	void setAI(int index, int ai);
	void setAlly(int index, int ally);
	void confirm();

  protected:
	void onEscape() override { finish(CANCEL); }
	bool fillHeight() const override { return true; }
	double maxWidth() const override { return 760; }

  private:
	Game *game;
	Slot slots[Team::MAX_COUNT];
	Glob2UI::Element slotRow(int index, const Glob2UI::Presentation &p, bool compact);
};

// Shared desktop/touch palette. Definitions are fixed for the dialog lifetime.
class TerrainPaletteDialog : public Glob2UI::InGameDialog
{
	std::shared_ptr<const TerrainRegistry> registry;

  public:
	explicit TerrainPaletteDialog(std::shared_ptr<const TerrainRegistry> value)
		: InGameDialog(Glob2UI::Surface::Editor), registry(std::move(value))
	{
	}
	const char *recordingId() const override { return "terrain_palette"; }
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;

  protected:
	void onEscape() override { finish(-1); }
	bool fillHeight() const override { return true; }
	double maxWidth() const override { return 760; }
};
