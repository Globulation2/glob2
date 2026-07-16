// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Constants shared between the split GameGUI*.cpp translation units.
// This header is intentionally private to those files; do not include it elsewhere.

#pragma once

#include <string>

#include <GUIBase.h>
#include <GUITextInput.h>
#include <GraphicContext.h>

#include "GlobalContainer.h"

using namespace GAGCore;
using namespace GAGGUI;

#define TYPING_INPUT_BASE_INC 7
#define TYPING_INPUT_MAX_POS 46

// these values are manually layouted for cuteste perception
#define YPOS_BASE_DEFAULT 180
#define YPOS_BASE_CONSTRUCTION (YPOS_BASE_DEFAULT + 5)
#define YPOS_BASE_FLAG (YPOS_BASE_DEFAULT + 5)
#define YPOS_BASE_STAT YPOS_BASE_DEFAULT
#define YPOS_BASE_BUILDING (YPOS_BASE_DEFAULT + 10)
#define YPOS_BASE_UNIT (YPOS_BASE_DEFAULT + 10)
#define YPOS_BASE_RESSOURCE YPOS_BASE_DEFAULT

#define YOFFSET_NAME 28
#define YOFFSET_ICON 52
#define YOFFSET_CARYING 34
#define YOFFSET_BAR 32
#define YOFFSET_INFOS 12
#define YOFFSET_TOWER 22

#define YOFFSET_B_SEP 6

#define YOFFSET_TEXT_BAR 16
#define YOFFSET_TEXT_PARA 14
#define YOFFSET_TEXT_LINE 12

#define YOFFSET_PROGRESS_BAR 10

#define YOFFSET_BRUSH 56

// The flag view's zone-type strip (forbidden/guard/clearing buttons) sits at
// YPOS_BASE_FLAG+YOFFSET_BRUSH and is this tall; the brush tool panel starts
// directly below it. Shared by the draw path (GameGUIDrawMiscPanels.cpp) and
// the click path (GameGUIInputMenuClick.cpp) so they cannot drift apart.
constexpr int ZONE_STRIP_HEIGHT = 40;

// Per-row pitches inside the building info panel resource/swarm sections.
#define YOFFSET_RESSOURCE_LINE 11
#define YOFFSET_RESSOURCE_SECTION_PAD 5
#define YOFFSET_SWARM_PROGRESS_BAR 15
#define YOFFSET_SWARM_RATIO_LINE 20

// Dimensions of the production-timeout progress bar that sits above the swarm
// ratio scrollboxes. The width matches the right-menu content area (same
// literal appears in RIGHT_MENU_OFFSET below).
#define SWARM_PROGRESS_BAR_WIDTH 128
#define SWARM_PROGRESS_BAR_HEIGHT 7

// Y-offsets (measured from the bottom of the screen) of the repair/upgrade
// and destroy action buttons in the building info panel, and the button height
// used for hit-testing the upgrade-preview tooltip hover.
#define BOTTOM_BUTTON_PRIMARY_YOFFSET 48
#define BOTTOM_BUTTON_SECONDARY_YOFFSET 24
#define BOTTOM_BUTTON_HEIGHT 16

// The sidebar on the right
#define RIGHT_MENU_WIDTH 160
#define RIGHT_MENU_HALF_WIDTH (RIGHT_MENU_WIDTH / 2)
#define RIGHT_MENU_OFFSET ((RIGHT_MENU_WIDTH -128)/2)
#define RIGHT_MENU_RIGHT_OFFSET (RIGHT_MENU_WIDTH - RIGHT_MENU_OFFSET)

// The exploration flag reuses Building::minLevelToFlag as a two-option choice
// of which explorers may answer the flag (see Building::canUnitWorkHere):
//   ANY_EXPLORER (0)  — any explorer is accepted
//   GROUND_ATTACK (1) — only explorers that can cast ground attack
// The option list drawn in the building panel (and its click hit-test) has one
// row per option, in this order. War flags use minLevelToFlag literally as a
// minimum warrior level, so their list has NB_UNIT_LEVELS rows instead.
constexpr int EXPLORATION_FLAG_OPTION_ANY_EXPLORER = 0;
constexpr int EXPLORATION_FLAG_OPTION_GROUND_ATTACK = 1;
constexpr int EXPLORATION_FLAG_OPTION_COUNT = 2;

// Geometry of the three-zone scrollbox widget used for worker count, flag
// stay-range and swarm-ratio sliders. The visual strip is laid out as
//   [<-arrow][===proportional drag track===][->arrow]
// inside the right-menu's 128px content area. Click x-coordinates are
// interpreted relative to the strip's left edge (see
// interpretScrollBoxClick in GameGUIInputMenuClickBuilding.cpp).
constexpr int SCROLLBOX_BAR_WIDTH = 128;
constexpr int SCROLLBOX_ARROW_WIDTH = 18;

// Icons for main menu, alliance and objectives buttons.
#define IGM_ICON_HEIGHT 36
#define IGM_MAIN_MENU_ICON_Y 0
#define IGM_ALLIANCE_ICON_Y IGM_ICON_HEIGHT
#define IGM_OBJECTIVES_ICON_Y (IGM_ICON_HEIGHT * 2)

// Settings for the right sidebar in replays
#define REPLAY_PANEL_XOFFSET 25
#define REPLAY_PANEL_YOFFSET (YPOS_BASE_STAT+10)
#define REPLAY_PANEL_SPACE_BETWEEN_OPTIONS 22
#define REPLAY_PANEL_PLAYERLIST_YOFFSET (5*REPLAY_PANEL_SPACE_BETWEEN_OPTIONS+5)

// The actual progress bar (including buttons)
#define REPLAY_PROGRESS_BAR_X_OFFSET 4
#define REPLAY_PROGRESS_BAR_Y_OFFSET 3
#define REPLAY_PROGRESS_BAR_BUTTON_WIDTH 15
#define REPLAY_PROGRESS_BAR_CAP_WIDTH 10
#define REPLAY_PROGRESS_BAR_NUM_BUTTONS 3

// The panel around the actual progress bar
#define REPLAY_BAR_WIDTH (globalContainer->settings.screenWidth - RIGHT_MENU_WIDTH - 4)
#define REPLAY_BAR_HEIGHT (2*REPLAY_PROGRESS_BAR_Y_OFFSET + 20)
#define REPLAY_BAR_Y (globalContainer->settings.screenHeight - REPLAY_BAR_HEIGHT)
#define REPLAY_BAR_TIMER_X (REPLAY_PROGRESS_BAR_X_OFFSET + REPLAY_PROGRESS_BAR_CAP_WIDTH + 5)

// Sprites for the replay bar
#define REPLAY_BAR_LEFT_CAP_SPRITE 56
#define REPLAY_BAR_RIGHT_CAP_SPRITE 57
#define REPLAY_BAR_PLAY_BUTTON_SPRITE 51
#define REPLAY_BAR_PLAY_BUTTON_ACTIVE_SPRITE 50
#define REPLAY_BAR_PAUSE_BUTTON_SPRITE 53
#define REPLAY_BAR_PAUSE_BUTTON_ACTIVE_SPRITE 52
#define REPLAY_BAR_FAST_FORWARD_BUTTON_SPRITE 55
#define REPLAY_BAR_FAST_FORWARD_BUTTON_ACTIVE_SPRITE 54

enum GameGUIGfxId
{
	EXCHANGE_BUILDING_ICONS = 21
};

//! The screen that contains the text input while typing message in game
class InGameTextInput:public OverlayScreen
{
protected:
	//! the text input widget
	TextInput *textInput;

public:
	//! InGameTextInput constructor
	InGameTextInput(GraphicContext *parentCtx);
	//! InGameTextInput destructor
	virtual ~InGameTextInput() { }
	//! React on action from any widget (but there is only one anyway)
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	//! Return the text typed
	std::string getText(void) const { return textInput->getText(); }
	//! Set the text
	void setText(const std::string text) const { textInput->setText(text); }
};

