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

//#define YOG_SERVER_IP "goldeneye.sked.ch"
#define YOG_SERVER_IP "192.168.1.5"
#define YOG_SERVER_PORT 7007

// 1s-3.5s-7s-14s-40s
#define SECOND_TIMEOUT 20
#define SHORT_NETWORK_TIMEOUT 70
#define DEFAULT_NETWORK_TIMEOUT 140
#define LONG_NETWORK_TIMEOUT 280
#define MAX_NETWORK_TIMEOUT 800

#define DEFAULT_NETWORK_TOTL 3

//We allways have:
// 256 char max messages size,
// 128 char max games name size,
// 32 char max userName size.

//Max size is defined by games lists. ((128+32+6+4)*16)=2720
#define YOG_MAX_PACKET_SIZE ((128+32+6+4)*16)

enum YOGMessageType
{
	YMT_BAD=0,
	
	YMT_CONNECTING=1,
	YMT_DECONNECTING=2,
	
	YMT_SEND_MESSAGE=3,
	YMT_MESSAGE=4,
	
	YMT_SHARING_GAME=6,
	YMT_STOP_SHARING_GAME=7,
	
	YMT_GAME_SOCKET=8,
	
	YMT_REQUEST_SHARED_GAMES_LIST=9,
	
	YMT_CONNECTION_PRESENCE=10,
	
	YMT_FLUSH_FILES=126,
	YMT_CLOSE_YOG=127
};

#endif 
