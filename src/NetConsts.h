/*
� � Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�re
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

� � This program is free software; you can redistribute it and/or modify
� � it under the terms of the GNU General Public License as published by
� � the Free Software Foundation; either version 2 of the License, or
� � (at your option) any later version.

� � This program is distributed in the hope that it will be useful,
� � but WITHOUT ANY WARRANTY; without even the implied warranty of
� � MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. �See the
� � GNU General Public License for more details.

� � You should have received a copy of the GNU General Public License
� � along with this program; if not, write to the Free Software
� � Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA �02111-1307 �USA
*/

#ifndef __NET_CONSTS_H
#define __NET_CONSTS_H

#define SERVER_PORT 7007
#define ANY_PORT 0

// 1s-3.5s-7s-14s
#define SECOND_TIMEOUT 20
#define SHORT_NETWORK_TIMEOUT 70
#define DEFAULT_NETWORK_TIMEOUT 140
#define LONG_NETWORK_TIMEOUT 280

#define DEFAULT_NETWORK_TOTL 3

enum OrderTypes
{
	BAD_ORDER=0,
	
	BROADCAST_REQUEST=1,
	BROADCAST_RESPONSE_LAN=2,
	BROADCAST_RESPONSE_YOG=3,
	
	SERVER_WATER=10,
	SERVER_PRESENCE=11,
	DATA_SESSION_GAME=12,
	DATA_SESSION_INFO=13,
	
	FULL_FILE_REQUEST=14,
	
	DATA_BASE_PLAYER=15,
	DATA_BASE_TEAM=16,
	DATA_BASE_MAP=17,
	
	ORDER_CREATE=20,
	ORDER_MODIFY_BUILDING=22,
	ORDER_MODIFY_SWARM=23,
	ORDER_MODIFY_FLAG=24,
	ORDER_MOVE_FLAG=25,
	ORDER_DELETE=30,
	ORDER_CANCEL_DELETE=31,
	ORDER_UPGRADE=32,
	ORDER_CANCEL_UPGRADE=33,
	
	ORDER_NULL=51,
	ORDER_QUITED=52,
	ORDER_WAITING_FOR_PLAYER=55,
	ORDER_DROPPING_PLAYER=57,
	ORDER_REQUESTING_AWAY=58,
	ORDER_NO_MORE_ORDER_AVIABLES=59,
	ORDER_PLAYER_QUIT_GAME=60,
	
	ORDER_MODIFY_UNIT=61,
	
	ORDER_TEXT_MESSAGE=71,
	ORDER_SET_ALLIANCE=72,
	ORDER_SUBMIT_CHECK_SUM=73,
	
	NEW_PLAYER_WANTS_PRESENCE=80,
	NEW_PLAYER_WANTS_SESSION_INFO=81,
	NEW_PLAYER_WANTS_FILE=82,
	NEW_PLAYER_SEND_CHECKSUM_CONFIRMATION=83,
	PLAYER_EXPLAINS_HOST_IP=84,
	
	SERVER_SEND_CHECKSUM_RECEPTION=85,
	
	CLIENT_QUIT_NEW_GAME=90,
	SERVER_QUIT_NEW_GAME=91,
	
	PLAYERS_CAN_START_CROSS_CONNECTIONS=101,
	PLAYERS_CONFIRM_START_CROSS_CONNECTIONS=102,
	PLAYERS_STILL_CROSS_CONNECTING=103,
	SERVER_CONFIRM_CLIENT_STILL_CROSS_CONNECTING=104,
	
	PLAYER_CROSS_CONNECTION_FIRST_MESSAGE=107,
	PLAYER_CROSS_CONNECTION_SECOND_MESSAGE=108,
	
	PLAYERS_CROSS_CONNECTIONS_ACHIEVED=110,
	SERVER_HEARD_CROSS_CONNECTION_CONFIRMATION=111,
	
	SERVER_ASK_FOR_GAME_BEGINNING=117,
	PLAYER_CONFIRM_GAME_BEGINNING=118,
	
	SERVER_KICKED_YOU=125,
	
	ORDER_MAP_GENERATION_DEFINITION=126
	
};

#endif 
