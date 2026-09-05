// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

// This is the version of map and savegame format, and all of the recorded datas on the server
#define VERSION_MAJOR 0
#define MINIMUM_VERSION_MINOR 58
#define VERSION_MINOR 88
// version 10 adds script saved in game
// version 11 the gamesfiles do saves which building has been seen under fog of war.
// version 12 saves map name into SessionGame instead of BaseMap.
// version 13 adds construction state into buildings
// version 14 adds the save of the end of game stats in Team.
// version 15 and 16 remove old compatibility because of major core engine changes.
// version 17 change the ressource system, move sore hardcoded ressource stuff to external file.
// version 18 adds fruit mask for unit happyness.
// version 19 removed optimisation parameters
// version 20 removed useless variable in Unit, used by old pathfinding. And added new Units-states.
// version 21 added real support for ClearingFlags with the gradient system.
// version 22 added MOV_RANDOM_FLY to support exiting of forbidden flags.
// version 23 added support for the exchange building.
// version 24 added Building::bullets
// version 25 added Multiple AI support
// version 26 added saved type of player (human/ai) in Team
// version 27 adding clearingRessources[] to allow flags to clear specific ressources
// version 28 changed eternal ressources way to count the amount.
// version 29 added Team::startPosSet for easy map editing.
// version 30 *added version for AI implementations*
// version 31 changed saved files signatures in Game.
// version 32 add more stats to end of game stats (total HP, attack Power, defense Power)
// version 33 added Building::minLevelToFlag to allow to request a minimum level of warriors in war flags.
// version 34 added Race::hungryness to have it cleaner.
// version 35 saving AINumbi endian safely.
// version 36 save number of unit lost/gained because of conversion and add guard areas
// version 37 added campaign in game
// version 38 added the clear area
// version 39 added UnitType::harvestDamage and UnitType::armorReductionPerHappyness.
// version 40 added experience and experienceLevel to Unit
// version 41 added magic abilities
// version 42 added AIWarrush
// version 43 added AINicowar
// version 44 added Bullet:revealX/Y/W/H into the saved file
// version 45 added teamRessources to Team for shared resources among markets
// version 46 added Unit::validTarget
// version 47 added new map generation system
// version 48 added script state load/save
// version 49 added units skinnning and per-unit hungryness
// version 50 changed TeamStat code to use std::vector
// version 51 added script areas to Map
// version 52 changed the Echo API
// version 53 added no-growth areas, to limit ressources growth
// version 54 removed campaign map linking for the new campaign system
// version 55 froze current Nicowar to OldNicowar in preperation of new nicowar system
// version 56 rewrote the unit allocation system, and added "hidden" forbidden zone to Map
// version 57 added custom prestige settings
// version 58 made signifigant, irreversible changes to how the map headers work in the game
//            all old versions have been rendered unreadable
// version 59 added complete saving/loading to Nicowar
// version 60 updated the saving for Nicowar for the new changes
// version 61 added an under attack countdown to buildings and units
// version 62 removed hidden forbidden
// version 63 added fertility to map
// version 64 removed syncRandSeedA/B/C and replaced with Boost.Random, seed stored in GameHeader and automatically seeded
// version 65 added maxUnitWorkingPrevious to Building to recover previous assigned unit count when cancelling construction or delete
// version 66 added new defense and explorer attack code to nicowar
// version 67 added checksum to map header
// version 68 changed checksum in map header to md5
// version 69 started saving GameGUIDefaultAssignManager
// version 70 added maxUnitWorkingFuture to be saved in Building
// version 71 added pre-game alliances in the form of ally-team numbers in GameHeader, and added system for winning conditions
// version 72 added pre-game option for map-discovery
// version 73 moved the race variable from BaseTeam down to team to shrink the size of MapHeader
// version 74 added unitsFailingRequirements to Building
// version 75 added description to Campaign, GameObjectives to Game, and removed campaignText from Game
// version 76 added mission complete/incomplete to Campaigns, added mission brieifing, added mission hints, added ability to fail game objective
// version 77 removed unitsFailingRequrements, expanding it into more requirements
// version 78 added defaultFlagRadius to the Order class
// version 79 added priority to the Building class
//TODO: verion conflicts in beta4 and default??
//beta4:
// version 80 added unitsHarvesting list to the Building class
// version 81 added canNotConvertUnitTimer to the Building class
//default:
//beta5:
// version 82 integrated new map script system
// version 83 added a description to campaigns
// version 84 dropped per-unit skinName (skin is now derived from typeNum)
// version 85 added Bullet::ticksInitial to the saved file
// version 86 widened BasePlayer::playerID from Uint16 to Uint32 in the saved file
// version 87 widened the replay inter-order step counter from Uint16 to Uint32
// version 88 added the per-team Map::exploredArea to saved games

//This must be updated when there are changes to YOG, MapHeader, GameHeader, BasePlayer, BaseTeam,
//NetMessage, and the likes, in parrallel to change of the VERSION_MINOR above
#define NET_PROTOCOL_VERSION 28
//Clients with older versions than this will be rejected
#define YOG_MIN_CLIENT_NET_PROTOCOL_VERSION 27
// version 21 changed OrderModifyWarFlag to more generic OrderModifyMinLevelToFlag
// version 22 added ConfigCheckSum to check if all use has the same file config.
// version 23 updated to allow custom prestige settings
// version 24 rewrote YOG
// version 25 changed YOGGameInfo to include game state information so that running games aren't shown
// version 26 changed heavy updates to YOG in general
// version 27 reordered the NetMessages so that reverse compatibility with future game versions can be done, added random seed in GameHeader
// version 28 Nicowar's behavior was changed

