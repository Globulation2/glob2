// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "GameGUI.h"
#include "Glob2Screen.h"
#include "FrontendTheme.h"
#include "gui/PhoneGraphic.h"
#include <GUIDropdown.h>
#include <TouchInput.h>

class LoadSaveScreen;

namespace GAGGUI
{
class Text;
class TextButton;
class OnOffButton;
} // namespace GAGGUI

//! Widget to display stats at end of game
class EndGameStat : public RectangularWidget, public PhoneGraphic
{
  public:
	//! Constructor, takes position and initial map name
	EndGameStat(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, Game *game);
	//! Destructor
	virtual ~EndGameStat();
	void paintPhone(int width, int height) override;
	void inspectPhone(int x, int y) override
	{
		mouse_x = x;
		mouse_y = y;
	}
	// Shared results view sends viewport coordinates, unlike PhoneGraphic's
	// historical local-coordinate adapter.
	void inspectScreenPoint(int x, int y)
	{
		int left, top, width, height;
		getScreenPos(&left, &top, &width, &height);
		mouse_x = x >= left && x < left + width ? x - left : -1;
		mouse_y = y >= top && y < top + height ? y - top : -1;
	}
	//! Set the type of stats (units, buildings, prestige) to draw
	void setStatType(int type);
	void paintMeasurements();
	//! Enables / disables a particular team
	void setEnabledState(int teamNum, bool isEnabled);
	//! paint routine
	virtual void paint(void);

  protected:
	//! Returns the value at the given point, by interpolating
	double getValue(double position, int team, int type);

	//! Returns the text for a particular time from seconds
	std::string getTimeText(int seconds);

	//! Returns the text for the right-scale
	std::string getRightScaleText(int value, int digits);

	/// Get the label of the end game stat
	std::string getStatLabel();

	//! the type of the stat beeing drawn
	int type;
	//! Pointer to game, used for drawing
	Game *game;
	//! List of true/false values for each team's enabled status
	bool *isTeamEnabled;
	//! This moves the circle indicating the score at the current mouse position.
	virtual void onSDLMouseMotion(SDL_Event *event);
	int mouse_x;
	int mouse_y;
};

struct TeamEntry
{
	int teamNum;
	Uint64 endVal[36]{};
	GAGCore::Color color;
	std::string name;
	bool enabled = true;
};

class EndGameScreen : public Glob2Screen
{
	friend class GameGUITouchHarness;

  public:
	//! Return values passed by the screen's buttons to onAction
	enum ButtonId
	{
		//! stat selector buttons use their int as id
		STAT_BUTTON_FIRST = 0,
		//! per-team toggle buttons use TEAM_TOGGLE_FIRST + row index
		TEAM_TOGGLE_FIRST = EndOfGameStat::TYPE_NB_STATS,
		QUIT = 38,
		SAVE_REPLAY = 39
	};

  protected:
	std::vector<TeamEntry> teams;
	EndGameStat *statWidget;
	int selectedMetric = 0;
	GAGGUI::Dropdown metricPicker;
	GAGCore::TouchInput resultGesture;
	struct ResultControl
	{
		GAGCore::ViewRect rect;
		int action;
	};
	std::vector<ResultControl> resultControls;
	bool expandedChart = false;
	bool teamFiltersOpen = false;
	double teamFilterScroll = 0;
	void drawResults();
	void activateResultControl(int action);

  protected:
	//! resort players
	void sortAndSet(int type);

	//! Translated short name of a stat type, used for its selector button and the graph label
	static std::string statTypeName(int type);

	//! pointer to the game, necessary for correctly saving replays
	Game *game;

  public:
	EndGameScreen(GameGUI *gui);
	void selectMetric(int metric);
	~EndGameScreen() override;
	bool usesResponsiveViewport() const override { return true; }
	void updateExecution(Uint32 tick) override;
	void handleExecutionEvent(SDL_Event event) override;
	void drawExecution() override;
	void viewportResized(int oldWidth, int oldHeight, int width, int height) override;
	virtual void onAction(Widget *source, Action action, int par1, int par2);

  private:
	std::unique_ptr<LoadSaveScreen> replaySave;
	std::unique_ptr<PhoneForm> replayForm;
	void saveReplay(const char *dir, const char *ext);
};
