/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __PREPARATIONGUI_H
#define __PREPARATIONGUI_H

#include "GAG.h"
#include "Race.h"
#include "Session.h"
#include "GUIMapPreview.h"

class TextInput;

class SessionConnection
{
protected:
	enum {MAX_PACKET_SIZE=65536};
public:
	SessionConnection();
	virtual ~SessionConnection();
	bool validSessionInfo;
	//Font *font;
	int crossPacketRecieved[32];
	int startGameTimeCounter;
protected:
	enum {hostiphost=0};
	enum {hostipport=0};
public:

	SessionInfo sessionInfo;
	Sint32 myPlayerNumber;
	UDPsocket socket;
	bool destroyNet;
	int channel;

};

void raceMenu(Race *race);


#endif
