/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

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

#include "Session.h"
#include "Order.h"
#include "GlobalContainer.h"

SessionGame::SessionGame()
{
	versionMajor=0;
	versionMinor=5;
	sessionInfoOffset=0;
	gameOffset=0;
	teamsOffset=0;
	playersOffset=0;
	mapOffset=0;

	numberOfPlayer=0;
	numberOfTeam=0;
	gameTPF=40;
	gameLatency=5;
	
	fileIsAMap=(Sint32)true;
}

SessionGame::SessionGame(const SessionGame &sessionGame)
{
	*this=sessionGame;
}

void SessionGame::save(SDL_RWops *stream)
{
	versionMajor=0;
	versionMinor=5;
	SDL_RWwrite(stream, "GLO2", 4, 1);
	SDL_WriteBE32(stream, versionMajor);
	SDL_WriteBE32(stream, versionMinor);
	// save 0, will be rewritten after
	SDL_WriteBE32(stream, 0);
	SDL_WriteBE32(stream, 0);
	SDL_WriteBE32(stream, 0);
	SDL_WriteBE32(stream, 0);
	SDL_WriteBE32(stream, 0);
	SDL_WriteBE32(stream, numberOfPlayer);
	SDL_WriteBE32(stream, numberOfTeam);
	SDL_WriteBE32(stream, gameTPF);
	SDL_WriteBE32(stream, gameLatency);
	SDL_WriteBE32(stream, fileIsAMap);
	SDL_RWwrite(stream, "GLO2", 4, 1);
}

bool SessionGame::load(SDL_RWops *stream)
{
	char signature[4];
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;

	versionMajor=SDL_ReadBE32(stream);
	versionMinor=SDL_ReadBE32(stream);
	if (versionMinor>1)
	{
		sessionInfoOffset=SDL_ReadBE32(stream);
		gameOffset=SDL_ReadBE32(stream);
		teamsOffset=SDL_ReadBE32(stream);
		playersOffset=SDL_ReadBE32(stream);
		mapOffset=SDL_ReadBE32(stream);
	}
	numberOfPlayer=SDL_ReadBE32(stream);
	numberOfTeam=SDL_ReadBE32(stream);
	gameTPF=SDL_ReadBE32(stream);
	gameLatency=SDL_ReadBE32(stream);

	if (versionMinor>4)
		fileIsAMap=SDL_ReadBE32(stream);
	else
		fileIsAMap=(Sint32)true;
	
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;

	return true;
}

Uint8 SessionGame::getOrderType()
{
	return DATA_SESSION_GAME;
}

void SessionInfo::draw(DrawableSurface *gfx)
{
	char playerInfo[128];
	gfx->drawFilledRect(20, 60, gfx->getW()-40, 20*numberOfPlayer, 0, 0, 0);
	for (int i=0; i<numberOfPlayer; i++)
	{
		int teamNumber;
		getPlayerInfo(i, &teamNumber, playerInfo, NULL, sizeof(playerInfo));
		BaseTeam &te=team[teamNumber];
		gfx->drawFilledRect(22, 62+i*20, 16, 16, te.colorR, te.colorG, te.colorB);
		gfx->drawString(40, 60+i*20, globalContainer->standardFont, playerInfo);
	}
}

void SessionInfo::getPlayerInfo(int playerNumber, int *teamNumber, char *infoString, SessionInfo *savedSessionInfo, int stringLen)
{
	assert(playerNumber>=0);
	assert(playerNumber<numberOfPlayer);
	*teamNumber=players[playerNumber].teamNumber;
	if (players[playerNumber].type==BasePlayer::P_IP)
	{
		char s[32];
		players[playerNumber].printip(s);
		char t[32];
		players[playerNumber].printNetState(t);
		
		if (savedSessionInfo)
		{
			if ((savedSessionInfo->players[playerNumber].type==BasePlayer::P_IP)
				||(savedSessionInfo->players[playerNumber].type==BasePlayer::P_LOCAL))
				snprintf(infoString, stringLen, "%s (%s %s) %s %s", players[playerNumber].name, globalContainer->texts.getString("[was]"), savedSessionInfo->players[playerNumber].name, s, t);
			else if (savedSessionInfo->players[playerNumber].type==BasePlayer::P_AI)
				snprintf(infoString, stringLen, "%s (%s %s) %s %s", players[playerNumber].name, globalContainer->texts.getString("[was AI]"), savedSessionInfo->players[playerNumber].name, s, t);
			else
				assert(false);
		}
		else
			snprintf(infoString, stringLen, "%s : %s (%s)", players[playerNumber].name, s, t);
	}
	else if (players[playerNumber].type==BasePlayer::P_AI)
	{
		if (savedSessionInfo)
		{
			if ((savedSessionInfo->players[playerNumber].type==BasePlayer::P_IP)
				||(savedSessionInfo->players[playerNumber].type==BasePlayer::P_LOCAL))
				snprintf(infoString, stringLen, "%s (%s %s) %s", players[playerNumber].name, globalContainer->texts.getString("[was]"), savedSessionInfo->players[playerNumber].name, globalContainer->texts.getString("[AI]"));
			else if (savedSessionInfo->players[playerNumber].type==BasePlayer::P_AI)
				snprintf(infoString, stringLen, "%s (%s %s) %s", players[playerNumber].name, globalContainer->texts.getString("[was AI]"), savedSessionInfo->players[playerNumber].name, globalContainer->texts.getString("[AI]"));
			else
				assert(false);
		}
		else
			snprintf(infoString, stringLen, "%s : (%s)", players[playerNumber].name, globalContainer->texts.getString("[AI]"));
	}
	else
		assert(false);
}

char *SessionGame::getData()
{
	addSint32(data, versionMajor, 0);
	addSint32(data, versionMinor, 4);
	addSint32(data, numberOfPlayer, 8);
	addSint32(data, numberOfTeam, 12);
	addSint32(data, gameTPF, 16);
	addSint32(data, gameLatency, 20);
	addSint32(data, fileIsAMap, 24);
	
	return data;
}

bool SessionGame::setData(const char *data, int dataLength)
{
	if (dataLength!=SessionGame::getDataLength())
		return false;

	versionMajor=getSint32(data, 0);
	versionMinor=getSint32(data, 4);
	printf("s nop=%d\n", numberOfPlayer=getSint32(data, 8));
	printf("s not=%d\n", numberOfTeam=getSint32(data, 12));
	gameTPF=getSint32(data, 16);
	gameLatency=getSint32(data, 20);
	fileIsAMap=getSint32(data, 24);
	
	return true;
}


int SessionGame::getDataLength()
{
	return S_GAME_DATA_SIZE;
}

Sint32 SessionGame::checkSum()
{
	Sint32 cs=0;

	cs^=versionMajor;
	cs^=versionMinor;
	cs^=numberOfPlayer;
	cs^=numberOfTeam;
	cs=(cs<<31)|(cs>>1);
	cs^=gameTPF;
	cs^=gameLatency;
	cs=(cs<<31)|(cs>>1);
	
	return cs;
}

SessionInfo::SessionInfo()
:SessionGame()
{

}

SessionInfo::SessionInfo(const SessionGame &sessionGame)
:SessionGame(sessionGame)
{

}

void SessionInfo::save(SDL_RWops *stream)
{
	int i;
	SessionGame::save(stream);

	// update to this version
	SAVE_OFFSET(stream, 12);

	map.save(stream);
	
	SDL_RWwrite(stream, "GLO2", 4, 1);
	for (i=0; i<numberOfPlayer; i++)
		players[i].save(stream);
	for (i=0; i<numberOfTeam; i++)
		team[i].save(stream);
		
	SDL_RWwrite(stream, "GLO2", 4, 1);
}

bool SessionInfo::load(SDL_RWops *stream)
{
	int i;
	if (!SessionGame::load(stream))
		return false;

	if (versionMinor>1)
		SDL_RWseek(stream, sessionInfoOffset, SEEK_SET);

	if (!map.load(stream))
		return false;

	char signature[4];
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;

	
	for (i=0; i<numberOfPlayer; ++i)
		if(!players[i].load(stream, versionMinor))
			return false;

	for (i=0; i<numberOfTeam; ++i)
		if(!team[i].load(stream))
			return false;

	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;
	
	return true;
}

Uint8 SessionInfo::getOrderType()
{
	return DATA_SESSION_INFO;
}

char *SessionInfo::getData()
{
	int l=0;
	int i;
	
	memcpy(l+data, map.getData(), map.getDataLength() );
	l+=map.getDataLength();

	for (i=0; i<32; ++i)
	{
		assert(players[i].getDataLength()==60);
		memcpy(l+data, players[i].getData(), players[i].getDataLength() );
		l+=players[i].getDataLength();
	}

	for (i=0; i<32; ++i)
	{
		assert(team[i].getDataLength()==16);
		memcpy(l+data, team[i].getData(), team[i].getDataLength() );
		l+=team[i].getDataLength();
	}

	memcpy(l+data, SessionGame::getData(), SessionGame::getDataLength() );
	l+=SessionGame::getDataLength();
	
	assert(l==S_INFO_DATA_SIZE);
	return data;
}

bool SessionInfo::setData(const char *data, int dataLength)
{
	if (dataLength!=SessionInfo::getDataLength())
		return false;

	int l=0;
	int i;

	map.setData(l+data, map.getDataLength());
	l+=map.getDataLength();

	for (i=0; i<32; ++i)
	{
		players[i].setData(l+data, players[i].getDataLength());
		l+=players[i].getDataLength();
	}

	for (i=0; i<32; ++i)
	{
		team[i].setData(l+data, team[i].getDataLength());
		team[i].race.create(Race::USE_DEFAULT); // TODO : pass the race trough the net.
		l+=team[i].getDataLength();
	}

	SessionGame::setData(l+data, SessionGame::getDataLength() );
	l+=SessionGame::getDataLength();
	
	if(l!=getDataLength())
		return false;
	
	return true;
}

int SessionInfo::getDataLength()
{
	return S_INFO_DATA_SIZE;
}

Sint32 SessionInfo::checkSum()
{
	Sint32 cs=0;
	
	cs^=map.checkSum();

	{
		for (int i=0; i<32; ++i)
			cs^=players[i].checkSum();
	}

	{
		for (int i=0; i<32; ++i)
			cs^=team[i].checkSum();
	}
	
	cs^=SessionGame::checkSum();
	
	return cs;
}

bool SessionInfo::setLocal(int p)
{
	if (p>=numberOfPlayer)
		return false;
	players[p].type=BasePlayer::P_LOCAL;
	return true;
}
