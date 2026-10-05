#include "hive/HiveClient.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <algorithm>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_timer.h>

#include <FormatableString.h>
#include <GameplayRecording.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "GameGUIKeyActions.h"
#include "GameUtilities.h"
#include "EngineTiming.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include "VoiceRecorder.h"

using std::shared_ptr;
using std::static_pointer_cast;

void GameGUI::handleKeySwitchToAreaBrush(int figure)
{
	if (selectionMode != BRUSH_SELECTION)
		clearSelection();
	brush.setFigure(figure);
	if (brush.getType() == BrushTool::MODE_NONE)
	{
		brush.setType(BrushTool::MODE_ADD);
	}
	displayMode = FLAG_VIEW;
	setSelection(BRUSH_SELECTION);
	toolManager.activateZoneTool();
}

void GameGUI::handleKeySelectConstruct(const char *buildingName)
{
	clearSelection();
	if (isBuildingEnabled(std::string(buildingName)))
	{
		displayMode = CONSTRUCTION_VIEW;
		setSelection(TOOL_SELECTION, (void *)buildingName);
	}
}

void GameGUI::handleKeySelectPlaceFlag(const char *flagName)
{
	clearSelection();
	if (isFlagEnabled(std::string(flagName)))
	{
		displayMode = FLAG_VIEW;
		setSelection(TOOL_SELECTION, (void *)flagName);
	}
}

void GameGUI::handleKeySelectPlaceArea(GameGUIToolManager::ZoneType zone)
{
	if (selectionMode != BRUSH_SELECTION)
		clearSelection();
	if (brush.getType() == BrushTool::MODE_NONE)
	{
		brush.setType(BrushTool::MODE_ADD);
	}
	displayMode = FLAG_VIEW;
	setSelection(BRUSH_SELECTION);
	toolManager.activateZoneTool(zone);
}

void GameGUI::toggleTorusView()
{
    if (!torusView.available() || typingInputScreen || inGameMenu != IGM_NONE || scrollableText) return;
    if (torusPointerDown) toolManager.finishPointerGesture(localTeamNo);
    torusPointerDown=false;
    torusView.toggle();
    stopViewportMotion();
    selectionPushed = panPushed = mapPanPushed = miniMapPushed = false;
    viewportSpeedX = viewportSpeedY = 0;
}

bool GameGUI::canChangeGameSpeed() const
{
	return globalContainer->replaying || !game.gameHeader.hasNetworkPlayer();
}

static_assert(GameSpeedControl::presets.front() == Settings::GAME_SPEED_NORMAL &&
	GameSpeedControl::presets.back() == Settings::GAME_SPEED_MAXIMUM);

int GameGUI::litSpeedChevrons() const
{
	if (globalContainer->replaying && globalContainer->replayFastForward)
		return GameSpeedControl::CHEVRONS;
	return GameSpeedControl::lit(globalContainer->settings.gameSpeed);
}

void GameGUI::cycleGameSpeed(bool forwards)
{
	if(!canChangeGameSpeed())
		return;
	// The chevrons replace a replay's fast-forward instead of hiding behind it.
	if (globalContainer->replaying)
		globalContainer->replayFastForward = false;
	const int speed=globalContainer->settings.gameSpeed;
	setGameSpeed(forwards ? GameSpeedControl::faster(speed) : GameSpeedControl::slower(speed));
}

double GameGUI::targetTickRate() const
{
	int stepMs = GAME_TICK_MS;
	if (canChangeGameSpeed())
		stepMs = (globalContainer->replaying && globalContainer->replayFastForward)
			? REPLAY_FAST_FORWARD_MS : globalContainer->settings.getGameSpeedStepDuration();
	return stepMs > 0 ? 1000.0 / stepMs : 0;
}

int GameGUI::tickRateShortfall() const
{
	const auto rate = tickRate.rate();
	const double target = targetTickRate();
	if (!rate || target <= 0 || gamePaused || hardPause)
		return 0;
	return *rate < target * .75 ? 2 : *rate < target * .9 ? 1 : 0;
}

void GameGUI::changeGameSpeed(int amount)
{
	setGameSpeed(globalContainer->settings.gameSpeed+amount);
}

void GameGUI::setGameSpeed(int speed)
{
	if(!canChangeGameSpeed())
		return;
	const int oldSpeed=globalContainer->settings.gameSpeed;
	globalContainer->settings.changeGameSpeed(speed-oldSpeed);
	if(oldSpeed!=globalContainer->settings.gameSpeed)
		globalContainer->settings.save();
	addMessage(Color(230, 230, 230), FormattableString("%0: %1")
		.arg(Toolkit::getStringTable()->getString("[game speed]"))
		.arg(globalContainer->settings.getGameSpeedText()), false);
}

void GameGUI::handleKey(SDL_KeyboardEvent key, bool pressed, bool repeat)
{
	if (!typingInputScreen)
	{
		if(key.key == SDLK_SPACE && pressed && swallowSpaceKey)
		{
			setIsSpaceSet(true);
		}
		else
		{
			Uint32 action_t = keyboardManager.getAction(KeyPress(key, pressed));
			// Older personal shortcut files do not contain the new speed actions.
			// Provide a fallback when the configurable shortcut system did not
			// resolve an action; configured actions still take precedence.
			if(action_t==GameGUIKeyActions::DoNothing && pressed
				&& (key.mod&SDL_KMOD_CTRL))
			{
				if(key.key==SDLK_PLUS || key.key==SDLK_EQUALS
					|| key.key==SDLK_KP_PLUS)
					action_t=GameGUIKeyActions::IncreaseGameSpeed;
				else if(key.key==SDLK_MINUS || key.key==SDLK_KP_MINUS)
					action_t=GameGUIKeyActions::DecreaseGameSpeed;
			}

            if(globalContainer->liveSpectating) {
                switch(action_t) {
                case GameGUIKeyActions::DoNothing:
                case GameGUIKeyActions::ShowMainMenu:
                case GameGUIKeyActions::IterateSelection:
                case GameGUIKeyActions::GoToEvent:
                case GameGUIKeyActions::GoToHome:
                case GameGUIKeyActions::PauseGame:
                case GameGUIKeyActions::HardPause:
                case GameGUIKeyActions::IncreaseGameSpeed:
                case GameGUIKeyActions::DecreaseGameSpeed:
                case GameGUIKeyActions::ToggleTorusView:
                case GameGUIKeyActions::ToggleDrawUnitPaths:
                case GameGUIKeyActions::ToggleDrawInformation:
                case GameGUIKeyActions::ToggleDrawAccessibilityAids:
                case GameGUIKeyActions::ViewHistory: break;
                default: return;
                }
            }
			switch(action_t)
			{
				case GameGUIKeyActions::DoNothing:
				{
				}
				break;
				case GameGUIKeyActions::ToggleTorusView:
					if (!repeat) toggleTorusView();
					break;
				case GameGUIKeyActions::ShowMainMenu:
				{
					if (inGameMenu==IGM_NONE)
						openMainMenu();
				}
				break;
				case GameGUIKeyActions::UpgradeBuilding:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild = selectionBuilding();
						int unitWorking = defaultAssign.getDefaultAssignedUnits(selBuild->getConstructionOriginTypeNum());
						// Another team's building can be selected for viewing; its upgrade is not ours to cancel.
						if (selBuild->owner->teamNumber != localTeamNo)
							break;
						if (selBuild->constructionResultState == Building::UPGRADE)
							orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
						else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
							repairAndUpgradeBuilding(selBuild, false, true);
					}
				}
				break;
				case GameGUIKeyActions::IncreaseUnitsWorking:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild=selectionBuilding();
						const int current = displayedMaxUnitWorking(*selBuild);
						if ((selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->maxUnitWorking) && (current<MAX_UNIT_WORKING))
						{
							int nbReq=std::min(20, current+1);
							pendingFor(selBuild->gid).pendingMaxUnitWorking = nbReq;
							orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
							defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
						}
					}
				}
				break;
				case GameGUIKeyActions::DecreaseUnitsWorking:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild=selectionBuilding();
						const int current = displayedMaxUnitWorking(*selBuild);
						if ((selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->maxUnitWorking) && (current>0))
						{
							int nbReq=std::max(0, current-1);
							pendingFor(selBuild->gid).pendingMaxUnitWorking = nbReq;
							orderQueue.push_back(shared_ptr<Order>(new OrderModifyBuilding(selBuild->gid, nbReq)));
							defaultAssign.setDefaultAssignedUnits(selBuild->typeNum, nbReq);
						}
					}
				}
				break;
				case GameGUIKeyActions::OpenCommander:
                    openCommander(); break;
                case GameGUIKeyActions::StopCommander:
                    if(hive)hive->stop(); break;
                case GameGUIKeyActions::ToggleRecording:
                    // Single-key bindings are taken globally by Application; this
                    // handles multi-key sequences bound in the game layout.
                    if(GAGCore::Recording::available() || GAGCore::Recording::recorder().active())
                        GAGCore::Recording::toggle();
                    break;
                case GameGUIKeyActions::OpenChatBox:
				{
					openChat();
				}
				break;
				case GameGUIKeyActions::IterateSelection:
				{
					iterateSelection();
				}
				break;
				case GameGUIKeyActions::GoToEvent:
				{
					// The most recently reported notification first; further
					// presses step through the others.
					eventGoTypeIterator = eventGoType;
					if (const GameEventFeed::Row *row = eventFeed.nextJumpTarget(SDL_GetTicks()))
						centerViewportOn(row->x, row->y);
					else
						centerViewportOn(eventGoPosX, eventGoPosY);
				}
				break;
				case GameGUIKeyActions::GoToHome:
				{
					centerViewportOn(localTeam->startPosX, localTeam->startPosY);
				}
				break;
				case GameGUIKeyActions::PauseGame:
                    if(globalContainer->liveSpectating){hardPause=!hardPause;break;}
					requestPause(!gamePaused);
					break;
				case GameGUIKeyActions::HardPause:
					// Hard-pause freezes this client's entire order/checksum
					// exchange loop (EngineRun.cpp), so it must not fire in a
					// live networked game or peers desync. Single-player,
					// AI-only and replay playback have no live peer and are safe.
					if (globalContainer->replaying || !game.gameHeader.hasNetworkPlayer())
						hardPause=!hardPause;
					break;
				case GameGUIKeyActions::IncreaseGameSpeed:
					changeGameSpeed(1);
					break;
				case GameGUIKeyActions::DecreaseGameSpeed:
					changeGameSpeed(-1);
					break;
				case GameGUIKeyActions::ToggleDrawUnitPaths:
					drawPathLines=!drawPathLines;
					break;
				case GameGUIKeyActions::DestroyBuilding:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild=selectionBuilding();
						if (selBuild->owner->teamNumber==localTeamNo)
						{
							if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
							{
								orderQueue.push_back(shared_ptr<Order>(new OrderCancelDelete(selBuild->gid)));
							}
							else if (selBuild->buildingState==Building::ALIVE)
							{
								orderQueue.push_back(shared_ptr<Order>(new OrderDelete(selBuild->gid)));
							}
						}
					}
				}
				break;
				case GameGUIKeyActions::RepairBuilding:
				{
					if (selectionMode==BUILDING_SELECTION)
					{
						Building* selBuild = selectionBuilding();
						int unitWorking = defaultAssign.getDefaultAssignedUnits(selBuild->getConstructionOriginTypeNum());
						// Another team's building can be selected for viewing; its repair is not ours to cancel.
						if (selBuild->owner->teamNumber != localTeamNo)
							break;
						if (selBuild->constructionResultState == Building::REPAIR)
							orderQueue.push_back(shared_ptr<Order>(new OrderCancelConstruction(selBuild->gid, unitWorking)));
						else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
							repairAndUpgradeBuilding(selBuild, true, false);
					}
				}
				break;
				case GameGUIKeyActions::ToggleDrawInformation:
					drawHealthFoodBar=!drawHealthFoodBar;
					break;
				case GameGUIKeyActions::ToggleDrawAccessibilityAids:
					drawAccessibilityAids = !drawAccessibilityAids;
					break;
				case GameGUIKeyActions::MarkMap:
					putMark=true;
					globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_MARK);
					break;
				case GameGUIKeyActions::ToggleRecordingVoice:
					if (globalContainer->voiceRecorder->recordingNow)
						globalContainer->voiceRecorder->stopRecording();
					else
						globalContainer->voiceRecorder->startRecording();
					break;
				case GameGUIKeyActions::ViewHistory:
				{
					toggleHistory();
				}
				break;
				case GameGUIKeyActions::SelectConstructInn:
					handleKeySelectConstruct("inn");
					break;
				case GameGUIKeyActions::SelectConstructSwarm:
					handleKeySelectConstruct("swarm");
					break;
				case GameGUIKeyActions::SelectConstructHospital:
					handleKeySelectConstruct("hospital");
					break;
				case GameGUIKeyActions::SelectConstructRacetrack:
					handleKeySelectConstruct("racetrack");
					break;
				case GameGUIKeyActions::SelectConstructSwimmingPool:
					handleKeySelectConstruct("swimmingpool");
					break;
				case GameGUIKeyActions::SelectConstructBarracks:
					handleKeySelectConstruct("barracks");
					break;
				case GameGUIKeyActions::SelectConstructSchool:
					handleKeySelectConstruct("school");
					break;
				case GameGUIKeyActions::SelectConstructDefenceTower:
					handleKeySelectConstruct("defencetower");
					break;
				case GameGUIKeyActions::SelectConstructStoneWall:
					handleKeySelectConstruct("stonewall");
					break;
				case GameGUIKeyActions::SelectConstructMarket:
					handleKeySelectConstruct("market");
					break;
				case GameGUIKeyActions::SelectPlaceExplorationFlag:
					handleKeySelectPlaceFlag("explorationflag");
					break;
				case GameGUIKeyActions::SelectPlaceWarFlag:
					handleKeySelectPlaceFlag("warflag");
					break;
				case GameGUIKeyActions::SelectPlaceClearingFlag:
					handleKeySelectPlaceFlag("clearingflag");
					break;
				case GameGUIKeyActions::SelectPlaceForbiddenArea:
					handleKeySelectPlaceArea(GameGUIToolManager::Forbidden);
					break;
				case GameGUIKeyActions::SelectPlaceGuardArea:
					handleKeySelectPlaceArea(GameGUIToolManager::Guard);
					break;
				case GameGUIKeyActions::SelectPlaceClearingArea:
					handleKeySelectPlaceArea(GameGUIToolManager::Clearing);
					break;
				case GameGUIKeyActions::SelectPlaceFarmArea:
					if (toolManager.farmAreasAvailable())
						handleKeySelectPlaceArea(GameGUIToolManager::Farm);
					break;
				case GameGUIKeyActions::SwitchToAddingAreas:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setType(BrushTool::MODE_ADD);
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToRemovingAreas:
				{
					if(selectionMode != BRUSH_SELECTION)
						clearSelection();
					brush.setType(BrushTool::MODE_DEL);
					displayMode = FLAG_VIEW;
					setSelection(BRUSH_SELECTION);
					toolManager.activateZoneTool();
				}
				break;
				case GameGUIKeyActions::SwitchToAreaBrush1:
					handleKeySwitchToAreaBrush(0);
					break;
				case GameGUIKeyActions::SwitchToAreaBrush2:
					handleKeySwitchToAreaBrush(1);
					break;
				case GameGUIKeyActions::SwitchToAreaBrush3:
					handleKeySwitchToAreaBrush(2);
					break;
				case GameGUIKeyActions::SwitchToAreaBrush4:
					handleKeySwitchToAreaBrush(3);
					break;
				case GameGUIKeyActions::SwitchToAreaBrush5:
					handleKeySwitchToAreaBrush(4);
					break;
				case GameGUIKeyActions::SwitchToAreaBrush6:
					handleKeySwitchToAreaBrush(5);
					break;
				case GameGUIKeyActions::SwitchToAreaBrush7:
					handleKeySwitchToAreaBrush(6);
					break;
				case GameGUIKeyActions::SwitchToAreaBrush8:
					handleKeySwitchToAreaBrush(7);
					break;
			}
		}
	}
}

void GameGUI::handleKeyAlways(void)
{
	if (!inputState.hasFocus()) return;
	const Uint8 *keystate = inputState.keyboard();
	if (notmenu == false)
	{
		SDL_Keymod modState = inputState.modifiers();
		updateCamera();
		double xMotion = 1/camera.zoom;
		double yMotion = 1/camera.zoom;
		/* We check that only Control is held to avoid accidentally
			matching window manager bindings for switching windows
			and/or desktops. */
		if (!(modState & (SDL_KMOD_ALT|SDL_KMOD_SHIFT)))
		{
			/* It violates good abstraction principles that I
				have to do the calculations in the next two
				lines.  There should be methods that abstract
				these computations. */
			if ((modState & SDL_KMOD_CTRL))
			{
				/* We move by half screens if Control is held while
					the arrow keys are held.  So we shift by 6
					instead of 5.  (If we shifted by 5, it would be
					good to subtract 1 so that there would be a small
					overlap between what is viewable both before and
					after the motion.) */
				xMotion = int(camera.visibleW()/64);
				yMotion = int(camera.visibleH()/64);
			}
			else
			{
				/* We move the screen by one square at a time if CTRL key
					is not being help */
				xMotion = 1/camera.zoom;
				yMotion = 1/camera.zoom;
			}
		}
		else if (modState)
		{
			/* Probably some keys held down as part of window
				manager operations. */
			xMotion = 0;
			yMotion = 0;
		}

		if (keystate[SDL_SCANCODE_UP])
			camera.originY -= yMotion*32;
		if (keystate[SDL_SCANCODE_KP_8])
			camera.originY -= yMotion*32;
		if (keystate[SDL_SCANCODE_DOWN])
			camera.originY += yMotion*32;
		if (keystate[SDL_SCANCODE_KP_2])
			camera.originY += yMotion*32;
		if ((keystate[SDL_SCANCODE_LEFT]) && (!typingInputScreen)) // we have a test in handleKeyAlways, that's not very clean, but as every key check based on key states and not key events are here, it is much simpler and thus easier to understand and thus cleaner ;-)
			camera.originX -= xMotion*32;
		if (keystate[SDL_SCANCODE_KP_4])
			camera.originX -= xMotion*32;
		if ((keystate[SDL_SCANCODE_RIGHT]) && (!typingInputScreen)) // we have a test in handleKeyAlways, that's not very clean, but as every key check based on key states and not key events are here, it is much simpler and thus easier to understand and thus cleaner ;-)
			camera.originX += xMotion*32;
		if (keystate[SDL_SCANCODE_KP_6])
			camera.originX += xMotion*32;
		if (keystate[SDL_SCANCODE_KP_7])
		{
			camera.originX -= xMotion*32;
			camera.originY -= yMotion*32;
		}
		if (keystate[SDL_SCANCODE_KP_9])
		{
			camera.originX += xMotion*32;
			camera.originY -= yMotion*32;
		}
		if (keystate[SDL_SCANCODE_KP_1])
		{
			camera.originX -= xMotion*32;
			camera.originY += yMotion*32;
		}
		if (keystate[SDL_SCANCODE_KP_3])
		{
			camera.originX += xMotion*32;
			camera.originY += yMotion*32;
		}
		camera.normalize();viewportX=camera.tileX();viewportY=camera.tileY();
	}
}
