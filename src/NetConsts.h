/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __NET_CONSTS_H
#define __NET_CONSTS_H

#define GAME_SERVER_PORT 7008
#define GAME_JOINER_PORT_1 7010
#define GAME_JOINER_PORT_2 7011
#define GAME_JOINER_PORT_3 7012
#define GAME_JOINER_FIND_LOCAL_PORT_BASE 7014
#define ANY_PORT 0

// 1s-3s-7s-14s-22s
#ifndef SECOND_TIMEOUT

#define SECOND_TIMEOUT 25
#define SHORT_NETWORK_TIMEOUT 75
#define DEFAULT_NETWORK_TIMEOUT 175
#define LONG_NETWORK_TIMEOUT 350
#define MAX_NETWORK_TIMEOUT 550

#define DEFAULT_NETWORK_TOTL 3
#endif

// Max packet size for reception in game setup
#define MAX_PACKET_SIZE 4096

#define NET_PROTOCOL_VERSION 23
// version 21 changed OrderModifyWarFlag to more generic OrderModifyMinLevelToFlag
// version 22 added ConfigCheckSum to check if all use has the same file config.
// version 23 updated to allow custom prestige settings

enum OrderTypes
{
	BAD_ORDER=0,
	
	ORDER_CREATE=20,
	ORDER_MODIFY_BUILDING=22,
	ORDER_MODIFY_EXCHANGE=23,
	ORDER_MODIFY_SWARM=24,
	ORDER_MODIFY_FLAG=30,
	ORDER_MODIFY_CLEARING_FLAG=31,
	ORDER_MODIFY_MIN_LEVEL_TO_FLAG=32,
	ORDER_MOVE_FLAG=35,
	ORDER_ALTERATE_FORBIDDEN=37,
	ORDER_ALTERATE_GUARD_AREA=38,
	ORDER_ALTERATE_CLEAR_AREA=39,
	ORDER_DELETE=40,
	ORDER_CANCEL_DELETE=41,
	ORDER_CONSTRUCTION=42,
	ORDER_CANCEL_CONSTRUCTION=43,
	
	ORDER_NULL=51,
	ORDER_PAUSE_GAME=59,
	ORDER_PLAYER_QUIT_GAME=67,

	ORDER_TEXT_MESSAGE=71,
	ORDER_VOICE_DATA=72,
	ORDER_SET_ALLIANCE=73,

	ORDER_MAP_MARK=74,
};

#endif 
