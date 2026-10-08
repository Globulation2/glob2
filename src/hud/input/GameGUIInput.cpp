#include <GestureScroll.h>
#include "hive/HiveDialog.h"
#include "MapZoomControls.h"
#include "ConnectionOverlay.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <iostream>
#include <optional>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>

#include <FileManager.h>
#include <Stream.h>
#include <TextStream.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "render/scene/BuildingCatalogView.h"
#include "GameGUITouch.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "GameGUIKeyActions.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
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
	if (!scrollableText)
		return false;
	const bool consumed = scrollableText->eventLogical(*event);
	if (scrollableText->finished())
		scrollableText.reset();
	return consumed;
}

// Forward events to the in-game chat input while it is open. Returns true if
// the event completed the chat entry and was fully consumed, in which case no
// further processing should happen for this event.
bool GameGUI::processTypingInput(SDL_Event *event)
{
	if (!typingInputScreen)
		return false;

	const bool consumed = typingInputScreen->eventLogical(*event);
	if (!typingInputScreen->finished())
		return consumed;

	if(typingCommander)
    {
        if(hive) {
            hive->commandDraft=typingInputScreen->getText();
            if(typingInputScreen->result()==0)hive->command(hive->commandDraft, globalContainer->settings.hiveMindSupervision);
        }
        closeChat();
        return true;
    }

	if (typingInputScreen->result()==0)
	{
		//Interpret message
		std::string message = typingInputScreen->getText();
		Uint32 nchatMask = chatMask;
		if (auto cmd = parseSlashCommand(message))
		{
			message = cmd->body;
			if (cmd->name == "a")
			{
				nchatMask = drawnScene().panels.local.state().allies;
			}
			else
			{
                if (const auto& session = drawnScene().world.session)
                    for (const auto& player : session->players)
                        if (cmd->name == player.name) {
                            nchatMask = Team::teamNumberToMask(player.teamNumber) | Team::teamNumberToMask(localTeamNo);
                            break;
                        }
			}
		}

		if (!message.empty())
			enqueueOrder(shared_ptr<Order>(new MessageOrder(nchatMask, MessageOrder::NORMAL_MESSAGE_TYPE, message.c_str())));
	}
	closeChat();
	return true;
}

void GameGUI::processEvent(SDL_Event *event)
{
    // Live diagnostic dumps and dialog construction are exceptional owner work.
    const bool diagnostic=(event->type==SDL_EVENT_MOUSE_BUTTON_DOWN || event->type==SDL_EVENT_MOUSE_BUTTON_UP) && (inputState.modifiers() & SDL_KMOD_SHIFT);
    const bool ownerDialog = gameMenuScreen && inGameMenu != IGM_TELEMETRY
        && inGameMenu != IGM_ALLIANCE && inGameMenu != IGM_OBJECTIVES;
    if ((diagnostic || ownerDialog || hive) && parkForClient([&]{processEvent(event);})) return;
    if (GAGCore::scrollGesture(*event) && !inputState.hasFocus()) return;
    inputState.observe(*event);
    if(hiveCards && !gameMenuScreen && !scrollableText && !inGameMenu && globalContainer->settings.hiveMindEnabled && hiveCards->handle(*event))return;
    if (connectionOverlay && !activeDialog() && !inGameMenu && connectionOverlay->handle(*event)) return;
    if (touch && !activeDialog() && touch->process(*event)) return;
    if ((event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_MIDDLE) ||
        ((event->type >= SDL_EVENT_WINDOW_FIRST && event->type <= SDL_EVENT_WINDOW_LAST) && event->type == SDL_EVENT_WINDOW_FOCUS_LOST))
        panPushed = false;
    if ((event->type >= SDL_EVENT_WINDOW_FIRST && event->type <= SDL_EVENT_WINDOW_LAST) && event->type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        lastMouseButtonState = 0;
        viewportSpeedX = viewportSpeedY = 0;
        selectionPushed = false;
        mapPanPushed = false;
        if (touch) touch->cancel();
        if (auto *dialog = activeDialog()) dialog->cancelInput();
        torusView.stopMoving();
        toolManager.cancelDrag(localTeamNo);
        torusPointerDown = false;
        torusView.setPointerHeld(false);
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT)
        torusView.setPointerHeld(false);
    if (!inputState.hasFocus() && (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP ||
        event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_WHEEL)) return;

    if (!typingInputScreen && inGameMenu == IGM_NONE && !scrollableText) {
        if (event->type == SDL_EVENT_MOUSE_MOTION) { mouseX=event->motion.x; mouseY=event->motion.y; }
        if (torusView.active() && handleTorusPointer(*event)) return;
    }

    // Commander cancellation must win over the composer's Return submission,
    // including customized stop bindings. Leave the draft open for editing.
    if (typingInputScreen && hive && (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) &&
        keyboardManager.getAction(KeyPress(event->key, event->type == SDL_EVENT_KEY_DOWN)) == GameGUIKeyActions::StopCommander)
    {
        if (hive) hive->stop();
        return;
    }

	// handle typing
	if (processTypingInput(event))
		return;

	// the dump (debug) keys are always handled
	if (event->type == SDL_EVENT_KEY_DOWN)
		handleKeyDump(event->key);


	// An open dialog gets every event; only the panel icons can still switch menus.
	if (inGameMenu)
	{
		notmenu=true;
		if (!processGameMenu(event) && event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
			handleMenuIconClick(event->button);
	}
	else
	{
		notmenu=false;
		if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
			handleMenuIconClick(event->button);
		if (inGameMenu)
			return;
		if (processScrollableWidget(event))
			return;
		if (event->type==SDL_EVENT_KEY_DOWN)
		{
			handleKey(event->key, true, event->key.repeat != 0);
		}
		else if (event->type==SDL_EVENT_KEY_UP)
		{
			handleKey(event->key, false);
		}
		else if (event->type==SDL_EVENT_MOUSE_BUTTON_DOWN)
		{
			handleMouseButtonDown(event->button);
		}
		else if (event->type==SDL_EVENT_MOUSE_BUTTON_UP)
		{
			handleMouseButtonUp(event->button);
		}
		else if (auto sample = GAGCore::scrollGesture(*event))
		{
			const auto wheel = GAGCore::gestureWheelFallback(*sample);
			const double delta=wheel.wheel.y * (wheel.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
			if (!scrollBuildingChoices(delta)) zoomMap(delta,mouseX,mouseY);
		}
		else if (event->type==SDL_EVENT_MOUSE_WHEEL)
		{
			int factor = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
			const double delta=event->wheel.y;
			if (!scrollBuildingChoices(delta*factor)) zoomMap(delta*factor,mouseX,mouseY);
		}
	}

	if (inGameMenu != IGM_NONE || scrollableText)
		mapPanPushed = false;
	if (event->type==SDL_EVENT_MOUSE_MOTION)
	{
		handleMouseMotion(event->motion.x, event->motion.y, event->motion.state);
	}
	else if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST)
	{
		handleActivation(0, 0);
	}
	else if (event->type==SDL_EVENT_QUIT)
	{
		exitGlobCompletely=true;
		enqueueOrder(shared_ptr<Order>(new PlayerQuitsGameOrder(localPlayer)));
		flushOutgoingAndExit=true;
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
            if (menu == IGM_MAIN && parkForClient([&]{handleMenuIconClick(mouseEvent);})) return;
			if (inGameMenu == menu)
			{
				closeDialog();
				return;
			}
			switch (menu)
			{
			case IGM_MAIN:
				openMainMenu();
				break;
			case IGM_ALLIANCE:
				openDialog(IGM_ALLIANCE, std::make_unique<InGameAllianceScreen>(this));
				break;
			case IGM_OBJECTIVES:
				openDialog(IGM_OBJECTIVES, std::make_unique<InGameObjectivesScreen>(this, false));
				break;
			default:
				assert(false);
			}
		}
	}
}

void GameGUI::centerViewportOn(int x, int y)
{
	const int oldViewportX = viewportX;
	const int oldViewportY = viewportY;
	stopViewportMotion();
	viewportX = x-int(camera.visibleW()/64);
	viewportY = y-int(camera.visibleH()/64);
	viewportChanged(oldViewportX, viewportX, oldViewportY, viewportY);
}

bool GameGUI::handleEventFeedClick(int mx, int my)
{
	for (const EventFeedHit &hit : eventFeedHits)
	{
		if (mx >= hit.x && mx < hit.x + hit.w && my >= hit.y && my < hit.y + hit.h)
		{
			centerViewportOn(hit.mapX, hit.mapY);
			return true;
		}
	}
	return false;
}

// Mouse-button press on the game view (only reached when no in-game menu is
// open): right-click view cycling, left-click dispatch to menu panel / replay
// bar / map, middle-click panning, and legacy wheel buttons 4/5.
void GameGUI::handleMouseButtonDown(SDL_MouseButtonEvent mouseEvent)
{
	updateCamera();
    // On the ring the controls zoom about its focus, as the wheel does there.
    const bool ring=torusView.enabled();
    if(mouseEvent.button==SDL_BUTTON_LEFT && (ring || !torusView.active()) &&
        clickMapZoomControls(camera,mouseEvent.x,mouseEvent.y,true,true,
            ring ? (globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)/2.0 : -1, ring ? (globalContainer->gfx->getH()+16)/2.0 : -1))
    {viewportX=camera.tileX();viewportY=camera.tileY();torusView.rebaseViewport(viewportX,viewportY);zoomControlPushed=true;return;}
	int button=mouseEvent.button;

	// The top bar's speed chevrons: left click speeds up, right click slows down.
	if ((button==SDL_BUTTON_LEFT || button==SDL_BUTTON_RIGHT) && !touch->usesHUD() && canChangeGameSpeed()
		&& mouseEvent.y<16 && mouseEvent.x>=topBarSpeedX()-4 && mouseEvent.x<topBarSpeedX()+TOP_BAR_SPEED_WIDTH-4)
	{
		cycleGameSpeed(button==SDL_BUTTON_LEFT);
		return;
	}

	// A game-event notification row jumps the view to where it happened.
	if (button==SDL_BUTTON_LEFT && handleEventFeedClick(mouseEvent.x, mouseEvent.y))
		return;

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
		{
			const bool mapGesture = selectionMode != TOOL_SELECTION &&
				selectionMode != BRUSH_SELECTION && !putMark;
			handleMapClick(mouseEvent.x, mouseEvent.y, mouseEvent.button);
			if (mapGesture && camera.contains(mouseEvent.x, mouseEvent.y) &&
				mouseEvent.y >= 16 && !torusView.active())
			{
				// Direct flag grabs still move the flag; other map objects
				// can start a camera drag.
				const auto* ref=std::get_if<BuildingRef>(&selection);
                const auto* building=ref ? inputBuilding(*ref) : nullptr;
                const bool movingFlag = selectionMode == BUILDING_SELECTION && selectionPushed &&
                    building && drawnScene().entities.type(*building)->semantics.relocatable;
				mapPanPushed = !movingFlag;
				panMouseX=mouseEvent.x;
				panMouseY=mouseEvent.y;
			}
		}
	}
	else if (button==SDL_BUTTON_MIDDLE)
	{
		// Enable panning
		panPushed=true;
		panMouseX=mouseEvent.x;
		panMouseY=mouseEvent.y;
	}
	else if (button==4 || button==5)
		zoomMap(button==4 ? 1 : -1, mouseEvent.x, mouseEvent.y);
}

// Mouse-button release on the game view (only reached when no in-game menu is
// open): finalize flag moves or brush/tool strokes, then clear pushed states.
void GameGUI::handleMouseButtonUp(SDL_MouseButtonEvent mouseEvent)
{
	updateCamera();
    if(mouseEvent.button==SDL_BUTTON_LEFT && zoomControlPushed)
    {zoomControlPushed=false;miniMapPushed=selectionPushed=mapPanPushed=false;return;}
	int button=mouseEvent.button;
	if ((button==SDL_BUTTON_LEFT) && camera.contains(mouseEvent.x,mouseEvent.y) && mouseEvent.y>=16)
	{
        const auto* ref=std::get_if<BuildingRef>(&selection);
        const auto* building=ref ? inputBuilding(*ref) : nullptr;
		if (!mapPanPushed && (selectionMode==BUILDING_SELECTION) &&
			selectionPushed && building && drawnScene().entities.type(*building)->semantics.relocatable)
		{
			// update flag
			moveFlag(mapMouseX(mouseEvent.x), mapMouseY(mouseEvent.y), true);
		}
		// We send the order
		else if (selectionMode==BRUSH_SELECTION || selectionMode==TOOL_SELECTION)
		{
			toolManager.handleMouseUp(mapMouseX(mouseEvent.x), mapMouseY(mouseEvent.y), localTeamNo, viewportX, viewportY, inputState.modifiers());
		}
	}
	if (button==SDL_BUTTON_LEFT)
	{
		miniMapPushed=false;
		selectionPushed=false;
		mapPanPushed=false;
	}
	else if (button==SDL_BUTTON_MIDDLE)
		panPushed=false;
	// showUnitWorkingToBuilding=false;
}

void GameGUI::handleKeyDump(SDL_KeyboardEvent key)
{
	if (key.key == SDLK_PRINTSCREEN)
	{
		if ((key.mod & SDL_KMOD_SHIFT) != 0)
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
	if (globalContainer->isViewingGame())
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

void GameGUI::repairAndUpgradeBuilding(const SceneBuildingPanel* building, bool repair, bool upgrade)
{
    if (!building || building->owner().number != localTeamNo || building->type->isBuildingSite) return;
    const auto& type = *building->type;
    if (repair && building->state().hp < building->state().maxHp)
    {
        if (building->hardSpaceForRepair)
            enqueueOrder(std::make_shared<OrderConstruction>(building->state().gid,
                defaultAssign.getDefaultAssignedUnits(drawnScene(), type.prevLevel),
                std::clamp(displayedMaxUnitWorking(*building), 0, type.semantics.assignmentLimit)));
    }
    else if (upgrade && building->hardSpaceForUpgrade)
        enqueueOrder(std::make_shared<OrderConstruction>(building->state().gid,
            defaultAssign.getDefaultAssignedUnits(drawnScene(), type.nextLevel),
            defaultAssign.getDefaultAssignedUnits(drawnScene(), BuildingCatalogView(*drawnScene().buildingTypes).getFinishedTypeNum(
                BuildingCatalogView(*drawnScene().buildingTypes).get(type.nextLevel)->key))));
}
