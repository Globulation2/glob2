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
	numberOfPlayer=0;
	numberOfTeam=0;
	gameTPF=40;
	gameLatency=5;
}

void SessionGame::save(SDL_RWops *stream)
{
	SDL_RWwrite(stream, "GLO2", 4, 1);
	SDL_WriteBE32(stream, numberOfPlayer);
	SDL_WriteBE32(stream, numberOfTeam);
	SDL_WriteBE32(stream, gameTPF);
	SDL_WriteBE32(stream, gameLatency);
	SDL_RWwrite(stream, "GLO2", 4, 1);
}

bool SessionGame::load(SDL_RWops *stream)
{
	char signature[4];
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;

	numberOfPlayer=SDL_ReadBE32(stream);
	numberOfTeam=SDL_ReadBE32(stream);
	gameTPF=SDL_ReadBE32(stream);
	gameLatency=SDL_ReadBE32(stream);

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
	gfx->drawFilledRect(20, 60, gfx->getW()-40, 200, 0, 0, 0);
	for (int i=0; i<numberOfPlayer; i++)
	{
		char s[32];
		players[i].printip(s);
		gfx->drawString(20, 60+i*20, globalContainer->standardFont, "%s : %s", players[i].name, s);
	}

}

char *SessionGame::getData()
{
	addSint32(data, numberOfPlayer, 0);
	addSint32(data, numberOfTeam, 4);
	addSint32(data, gameTPF, 8);
	addSint32(data, gameLatency, 12);
	
	return data;
}

bool SessionGame::setData(const char *data, int dataLength)
{
	if (dataLength!=SessionGame::getDataLength())
		return false;
	
	printf("s nop=%d\n", numberOfPlayer=getSint32(data, 0));
	printf("s not=%d\n", numberOfTeam=getSint32(data, 4));
	gameTPF=getSint32(data, 8);
	gameLatency=getSint32(data, 12);
	
	return true;
}


int SessionGame::getDataLength()
{
	return 16;
}

Sint32 SessionGame::checkSum()
{
	Sint32 cs=0;

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

void SessionInfo::save(SDL_RWops *stream)
{
	SessionGame::save(stream);
	
	map.save(stream);
	
	SDL_RWwrite(stream, "GLO2", 4, 1);
	for (int i=0; i<numberOfPlayer; i++)
		players[i].save(stream);
	for (int i2=0; i2<numberOfTeam; i2++)
		team[i2].save(stream);
		
	SDL_RWwrite(stream, "GLO2", 4, 1);
}

bool SessionInfo::load(SDL_RWops *stream)
{
	if (!SessionGame::load(stream))
		return false;
	
	if (!map.load(stream))
		return false;
	
	char signature[4];
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;

	{
		for (int i=0; i<numberOfPlayer; ++i)
			players[i].load(stream);
	}
	{
		for (int i=0; i<numberOfTeam; ++i)
			team[i].load(stream);
	}
	
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
	
	memcpy(l+data, map.getData(), map.getDataLength() );
	l+=map.getDataLength();

	{
		for (int i=0; i<32; ++i)
		{
			memcpy(l+data, players[i].getData(), players[i].getDataLength() );
			l+=players[i].getDataLength();
		}
	}

	{
		for (int i=0; i<32; ++i)
		{
			memcpy(l+data, team[i].getData(), team[i].getDataLength() );
			l+=team[i].getDataLength();
		}
	}
	
	memcpy(l+data, SessionGame::getData(), SessionGame::getDataLength() );
	l+=SessionGame::getDataLength();
	
	assert(l==1968);
	return data;
}

bool SessionInfo::setData(const char *data, int dataLength)
{
	if (dataLength!=SessionInfo::getDataLength())
		return false;
	
	int l=0;
	
	map.setData(l+data, map.getDataLength());
	l+=map.getDataLength();

	{
		for (int i=0; i<32; ++i)
		{
			players[i].setData(l+data, players[i].getDataLength());
			l+=players[i].getDataLength();
		}
	}

	{
		for (int i=0; i<32; ++i)
		{
			team[i].setData(l+data, team[i].getDataLength());
			team[i].race.create(Race::USE_DEFAULT); // TODO : pass the race trough the net.
			l+=team[i].getDataLength();
		}
	}
	
	
	SessionGame::setData(l+data, SessionGame::getDataLength() );
	l+=SessionGame::getDataLength();
	
	if(l!=getDataLength())
		return false;
	
	return true;
}

int SessionInfo::getDataLength()
{
	return (1968);
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
