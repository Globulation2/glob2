// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <iostream>
#include <optional>

#include <SDL_keyboard.h>
#include <SDL_keycode.h>

#include <FileManager.h>
#include <Stream.h>
#include <TextStream.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "ScrollWheelTarget.h"
#include "Unit.h"

using std::shared_ptr;
using std::static_pointer_cast;

namespace {

struct SlashCommand
{
	std::string name;
	std::string body;
};

// Parse a chat-input string into a slash-command name and message body.
//   "/cmd body words..." → name="cmd", body="body words..."
//   "/cmd"               → name="cmd", body=""
//   ""  or non-'/' first char → std::nullopt
// The body is the substring after the first space; if the message is just
// "/<name>" with no space, body is empty and the caller's empty-message guard
// suppresses sending an order. Pivots on a single find(' ') — no past-end
// reads even when the user types "/a" and hits Enter.
std::optional<SlashCommand> parseSlashCommand(const std::string& message)
{
	if (message.empty() || message[0] != '/')
		return std::nullopt;
	const std::string::size_type sp = message.find(' ');
	if (sp == std::string::npos)
		return SlashCommand{message.substr(1), std::string()};
	return SlashCommand{message.substr(1, sp - 1), message.substr(sp + 1)};
}

} // namespace

bool GameGUI::processScrollableWidget(SDL_Event *event)
{
	scrollableText->translateAndProcessEvent(event);
	return true;
}

// Forward events to the in-game chat input while it is open. Returns true if
// the event completed the chat entry and was fully consumed, in which case no
// further processing should happen for this event.
bool GameGUI::processTypingInput(SDL_Event *event)
{
	if (!typingInputScreen)
		return false;

	if ((event->type==SDL_KEYDOWN) && (event->key.keysym.sym == SDLK_ESCAPE))
	{
		typingInputScreenInc=-TYPING_INPUT_BASE_INC;
		typingInputScreen->endValue=1;
	}

	typingInputScreen->translateAndProcessEvent(event);

	if (typingInputScreen->endValue==0)
	{
		//Interpret message
		std::string message = typingInputScreen->getText();
		Uint32 nchatMask = chatMask;
		if (auto cmd = parseSlashCommand(message))
		{
			message = cmd->body;
			if (cmd->name == "a")
			{
				nchatMask = localTeam->allies;
			}
			else
			{
				for (int i = 0; i < game.gameHeader.getNumberOfPlayers(); ++i)
				{
					if (cmd->name == game.gameHeader.getBasePlayer(i).name)
					{
						nchatMask = game.gameHeader.getBasePlayer(i).teamNumberMask | localTeam->me;
						break;
					}
				}
			}
		}

		if (!message.empty())
		{
			orderQueue.push_back(shared_ptr<Order>(new MessageOrder(nchatMask, MessageOrder::NORMAL_MESSAGE_TYPE, message.c_str())));
			typingInputScreen->setText("");
		}
		typingInputScreenInc=-TYPING_INPUT_BASE_INC;
		typingInputScreen->endValue=1;
		return true;
	}
	return false;
}

void GameGUI::processEvent(SDL_Event *event)
{
	// handle typing
	if (processTypingInput(event))
		return;

	// the dump (debug) keys are always handled
	if (event->type == SDL_KEYDOWN)
		handleKeyDump(event->key);


	if (event->type==SDL_MOUSEBUTTONUP)
	{
		int button=event->button.button;
		if (button==SDL_BUTTON_MIDDLE)
		{
			panPushed=false;
		}
	}


	if (event->type == SDL_MOUSEBUTTONDOWN)
		handleMenuIconClick(event->button);


	// if there is a menu he get events first
	if (inGameMenu)
	{
		notmenu=true;
		processGameMenu(event);
	}
	else
	{
		notmenu=false;
		if (scrollableText)
		{
			processScrollableWidget(event);
		}
		if (event->type==SDL_KEYDOWN)
		{
			handleKey(event->key.keysym, true);
		}
		else if (event->type==SDL_KEYUP)
		{
			handleKey(event->key.keysym, false);
		}
		else if (event->type==SDL_MOUSEBUTTONDOWN)
		{
			handleMouseButtonDown(event->button);
		}
		else if (event->type==SDL_MOUSEBUTTONUP)
		{
			handleMouseButtonUp(event->button);
		}
		else if (event->type==SDL_MOUSEWHEEL)
		{
			int factor = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
			accumulateScrollWheelDelta(event->wheel.y * factor);
		}
	}

	if (event->type==SDL_MOUSEMOTION)
	{
		handleMouseMotion(event->motion.x, event->motion.y, event->motion.state);
	}
	else if (event->type==SDL_WINDOWEVENT)
	{
		handleActivation(event->window.data1, event->window.data2);
	}
	else if (event->type==SDL_QUIT)
	{
		exitGlobCompletely=true;
		orderQueue.push_back(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
		flushOutgoingAndExit=true;
	}
	else if (event->type==SDL_WINDOWEVENT_RESIZED)
	{
		// FIXME: window resize is broken
		/*int newW=event->window.data1;
		int newH=event->window.data2;
		newW&=(~(0x1F));
		newH&=(~(0x1F));
		if (newW<640)
			newW=640;
		if (newH<480)
			newH=480;
		printf("New size : %dx%d\n", newW, newH);
		globalContainer->gfx->setRes(newW, newH);*/
	}
}

// Route one scroll-wheel delta into the pending accumulators. The modifier
// keys are sampled here, at event time, so deltas scrolled while SHIFT was held
// are committed to the stay-range accumulator even if SHIFT is released before
// the frame's flushScrollWheelOrders() runs. The building-type gate is deferred
// to flush time.
void GameGUI::accumulateScrollWheelDelta(int delta)
{
	SDL_Keymod mod = SDL_GetModState();
	switch (scrollWheelTarget(mod & KMOD_SHIFT, mod & KMOD_CTRL,
	                          globalContainer->settings.scrollWheelEnabled))
	{
	case ScrollWheelTarget::MaxUnitWorking:
		scrollWheelWorkingChanges += delta;
		break;
	case ScrollWheelTarget::UnitStayRange:
		scrollWheelStayRangeChanges += delta;
		break;
	case ScrollWheelTarget::None:
		break;
	}
}

// Hit-test the in-game menu icons on the right panel edge; open, switch or
// close the corresponding in-game menu screen on a left-click hit.
void GameGUI::handleMenuIconClick(SDL_MouseButtonEvent mouseEvent)
{
	int butx = mouseEvent.x;
	int buty = mouseEvent.y;

	int leftEdge  = globalContainer->gfx->getW() - RIGHT_MENU_WIDTH - IGM_ICON_HEIGHT/2;
	int rightEdge = globalContainer->gfx->getW() - RIGHT_MENU_WIDTH + IGM_ICON_HEIGHT/2;
	int menu = -1;

	if (mouseEvent.button == SDL_BUTTON_LEFT
		&& (butx > leftEdge)
		&& (butx < rightEdge))
	{
		if (buty < IGM_MAIN_MENU_ICON_Y + IGM_ICON_HEIGHT)
		{
			menu = IGM_MAIN;
		}
		if (!(hiddenGUIElements & HIDABLE_ALLIANCE)
			&& (buty > IGM_ALLIANCE_ICON_Y)
			&& (buty < IGM_ALLIANCE_ICON_Y + IGM_ICON_HEIGHT))
		{
			menu = IGM_ALLIANCE;
		}
		if ((buty > IGM_OBJECTIVES_ICON_Y)
			&& (buty < IGM_OBJECTIVES_ICON_Y + IGM_ICON_HEIGHT))
		{
			menu = IGM_OBJECTIVES;
		}

		if (menu != -1)
		{
			if (inGameMenu != IGM_NONE)
			{
				gameMenuScreen.reset();
			}
			if (inGameMenu == menu)
				inGameMenu = IGM_NONE;
			else
				inGameMenu = static_cast<InGameMenu>(menu);

			switch (menu)
			{
			case IGM_MAIN:
				gameMenuScreen.reset(new InGameMainScreen(globalContainer->replaying));
				break;
			case IGM_ALLIANCE:
				gameMenuScreen.reset(new InGameAllianceScreen(this));
				break;
			case IGM_OBJECTIVES:
				gameMenuScreen.reset(new InGameObjectivesScreen(this, false));
				break;
			default:
				assert(false);
			}
		}
	}
}

// Mouse-button press on the game view (only reached when no in-game menu is
// open): right-click view cycling, left-click dispatch to menu panel / replay
// bar / map, middle-click panning, and legacy wheel buttons 4/5.
void GameGUI::handleMouseButtonDown(SDL_MouseButtonEvent mouseEvent)
{
	int button=mouseEvent.button;
	//int state=mouseEvent.state;

	if (button==SDL_BUTTON_RIGHT)
	{
		handleRightClick();
	}
	else if (button==SDL_BUTTON_LEFT)
	{
		if (mouseEvent.x>globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)
			handleMenuClick(mouseEvent.x-globalContainer->gfx->getW()+RIGHT_MENU_WIDTH, mouseEvent.y, mouseEvent.button);
		else if (globalContainer->replaying && mouseEvent.y >= REPLAY_BAR_Y)
			handleReplayProgressBarClick(mouseEvent.x, mouseEvent.y, mouseEvent.button);
		else
			handleMapClick(mouseEvent.x, mouseEvent.y, mouseEvent.button);
	}
	else if (button==SDL_BUTTON_MIDDLE)
	{
		if ((selectionMode==BUILDING_SELECTION) && (globalContainer->gfx->getW()-mouseEvent.x<RIGHT_MENU_WIDTH))
		{
			Building* selBuild=selectionBuilding();
			assert (selBuild);
//			selBuild->verbose=(selBuild->verbose+1)%5;
//			printf("building gid=(%d)\n", selBuild->gid);
//			if (selBuild->verbose==0)
//				printf(" verbose off\n");
//			else if (selBuild->verbose==1 || selBuild->verbose==2)
//				printf(" verbose global [%d]\n", selBuild->verbose&1);
//			else if (selBuild->verbose==3 || selBuild->verbose==4)
//				printf(" verbose local [%d]\n", selBuild->verbose&1);
//			else
//				assert(false);
//			printf(" pos=(%d, %d)\n", selBuild->posX, selBuild->posY);
//			printf(" dirtyLocalGradient=[%d, %d]\n", selBuild->dirtyLocalGradient[0], selBuild->dirtyLocalGradient[1]);
//			printf(" globalGradient=[%p, %p]\n", selBuild->globalGradient[0], selBuild->globalGradient[1]);
//			printf(" locked=[%d, %d]\n", selBuild->locked[0], selBuild->locked[1]);

		}
		else
		{
			// Enable panning
			panPushed=true;
			panMouseX=mouseEvent.x;
			panMouseY=mouseEvent.y;
			panViewX=viewportX;
			panViewY=viewportY;
		}
	}
	else if (button==4)
	{
		accumulateScrollWheelDelta(1);
	}
	else if (button==5)
	{
		accumulateScrollWheelDelta(-1);
	}
}

// Mouse-button release on the game view (only reached when no in-game menu is
// open): finalize flag moves or brush/tool strokes, then clear pushed states.
void GameGUI::handleMouseButtonUp(SDL_MouseButtonEvent mouseEvent)
{
	int button=mouseEvent.button;
	if ((button==SDL_BUTTON_LEFT) && (mouseEvent.x < globalContainer->gfx->getW()-RIGHT_MENU_WIDTH))
	{
		if ((selectionMode==BUILDING_SELECTION) && selectionPushed && selectionBuilding()->type->isVirtual)
		{
			// update flag
			moveFlag(mouseEvent.x, mouseEvent.y, true);
		}
		// We send the order
		else if (selectionMode==BRUSH_SELECTION || selectionMode==TOOL_SELECTION)
		{
			toolManager.handleMouseUp(mouseEvent.x, mouseEvent.y, localTeamNo, viewportX, viewportY);
		}
	}
	miniMapPushed=false;
	selectionPushed=false;
	panPushed=false;
	// showUnitWorkingToBuilding=false;
}

void GameGUI::handleKeyDump(SDL_KeyboardEvent key)
{
	if (key.keysym.sym == SDLK_PRINTSCREEN)
	{
		if ((key.keysym.mod & KMOD_SHIFT) != 0)
		{
			OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("glob2.dump.txt"));
			if (stream->isEndOfStream())
			{
				std::cerr << "Can't dump full game memory to file glob2.dump.txt" << std::endl;
			}
			else
			{
				std::cerr << "Dump full game memory" << std::endl;
				save(stream, "glob2.dump.txt");
			}
			delete stream;
		}
		else
		{
			globalContainer->gfx->printScreen("screenshot.bmp");
		}
	}
}

void GameGUI::handleActivation(Uint8 state, Uint8 gain)
{
	if (gain==0)
	{
		viewportSpeedX=viewportSpeedY=0;
	}
}

void GameGUI::handleRightClick(void)
{
	// We cycle between views:
	if (selectionMode==NO_SELECTION)
	{
		nextDisplayMode();
	}
	// We deselect all, we want no tools activated:
	else
	{
		clearSelection();
	}
}

void GameGUI::nextDisplayMode(void)
{
	if (globalContainer->replaying)
	{
		replayDisplayMode=ReplayDisplayMode((replayDisplayMode + 1) % RDM_NB_VIEWS);
		return;
	}

	int t=0;
	do
	{
		displayMode=DisplayMode((displayMode + 1) % NB_VIEWS);
		if ((t++)==4)
		{
			displayMode=NB_VIEWS;
			break;
		}
	} while ((1<<((int)displayMode)) & hiddenGUIElements);
}

void GameGUI::repairAndUpgradeBuilding(Building *building, bool repair, bool upgrade)
{
	BuildingType *buildingType = building->type;

	// building site can't be repaired nor upgraded
	if (buildingType->isBuildingSite)
		return;
	// we can upgrade or repair only building from our team
	if (building->owner->teamNumber != localTeamNo)
		return;
	int typeNum = building->typeNum + 1; //determines type of updated building
	int unitWorking = defaultAssign.getDefaultAssignedUnits(typeNum);
	int repairUnitWorking = defaultAssign.getDefaultAssignedUnits(building->typeNum - 1);
	int unitWorkingFuture = defaultAssign.getDefaultAssignedUnits(typeNum+1);
	if ((building->hp < buildingType->hpMax) && repair)
	{
		// repair
		if ((building->type->regenerationSpeed == 0) &&
			(building->isHardSpaceForBuildingSite(Building::REPAIR)) &&
			(localTeam->maxBuildLevel() >= buildingType->level))
			orderQueue.push_back(shared_ptr<Order>(new OrderConstruction(building->gid, repairUnitWorking, displayedMaxUnitWorking(*building))));
	}
	else if (upgrade)
	{
		// upgrade
		if ((buildingType->nextLevel != -1) &&
			(building->isHardSpaceForBuildingSite(Building::UPGRADE)) &&
			(localTeam->maxBuildLevel() > buildingType->level))
			orderQueue.push_back(shared_ptr<Order>(new OrderConstruction(building->gid, unitWorking, unitWorkingFuture)));
	}
}
