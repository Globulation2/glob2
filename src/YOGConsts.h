/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __YOG_CONSTS_H
#define __YOG_CONSTS_H

#define YOG_SERVER_IP "goldeneye.sked.ch"
//#define YOG_SERVER_IP "192.168.1.5"
#define YOG_SERVER_PORT 7007

// 1s-4s-7.5s-14.5s-40s
#ifndef SECOND_TIMEOUT

#define SECOND_TIMEOUT 20
#define SHORT_NETWORK_TIMEOUT 80
#define DEFAULT_NETWORK_TIMEOUT 150
#define LONG_NETWORK_TIMEOUT 290
#define MAX_NETWORK_TIMEOUT 800

#define DEFAULT_NETWORK_TOTL 3
#endif


//We allways have:
// 256 char max messages size,
// 128 char max games name size,
// 32 char max userName size.

//Max size is defined by games lists. ((128+32+6+4)*16)=2720
//or unshare 256*4+4=1028
#define YOG_MAX_PACKET_SIZE ((128+32+6+4)*16)

enum YOGMessageType
{
	YMT_BAD=0,
	
	YMT_LOOP_BACK_PACKET_TYPE=1,
	YMT_BROADCAST_REQUEST=2,
	YMT_BROADCAST_RESPONSE_LAN=3,
	YMT_BROADCAST_RESPONSE_YOG=4,
	
	YMT_GAME_INFO_FROM_HOST=5,
	
	YMT_CONNECTING=6,
	YMT_DECONNECTING=7,
	YMT_CONNECTION_REFUSED=8,
	
	YMT_SEND_MESSAGE=10,
	YMT_MESSAGE=12,
	YMT_PRIVATE_MESSAGE=14,
	YMT_PRIVATE_RECEIPT=16,
	YMT_ADMIN_MESSAGE=18,
	
	YMT_SHARING_GAME=20,
	YMT_STOP_SHARING_GAME=22,
	
	YMT_HOST_GAME_SOCKET=30,
	YMT_JOIN_GAME_SOCKET=32,
	
	YMT_GAMES_LIST=40,
	YMT_UNSHARED_LIST=42,
	
	YMT_CLIENTS_LIST=50,
	YMT_LEFT_CLIENTS_LIST=52,
	
	YMT_CONNECTION_PRESENCE=80,
	
	YMT_PLAYERS_WANTS_TO_JOIN=100,
	
	YMT_FLUSH_FILES=126,
	YMT_CLOSE_YOG=127
};

#endif 
