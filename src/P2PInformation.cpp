/*
  Copyright (C) 2008 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "P2PInformation.h"

P2PInformation::P2PInformation()
{

}



void P2PInformation::addP2PPlayer(P2PPlayerInformation& information)
{
	players.push_back(information);
}



P2PPlayerInformation& P2PInformation::getPlayerInformation(size_t n)
{
	return players[n];
}



const P2PPlayerInformation& P2PInformation::getPlayerInformation(size_t n) const
{
	return players[n];
}



void P2PInformation::removeP2PPlayer(size_t n)
{
	players.erase(players.begin() + n);
}



size_t P2PInformation::getNumberOfP2PPlayer() const
{
	return players.size();
}



void P2PInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("P2PInformation");
	stream->writeEnterSection("players");
	stream->writeUint8(players.size(), "size");
	for(int i=0; i<players.size(); ++i)
	{
		stream->writeEnterSection(i);
		players[i].encodeData(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



void P2PInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("P2PInformation");
	stream->readEnterSection("players");
	Uint8 size = stream->readUint8("size");
	for(int i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		players.push_back(P2PPlayerInformation());
		players[i].decodeData(stream);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
}



bool P2PInformation::operator==(const P2PInformation& rhs) const
{
	if(players.size() != rhs.players.size())
		return false;

	for(int i=0; i<players.size(); ++i)
	{
		if(players[i] != rhs.players[i])
			return false;
	}
	
	return true;
}



bool P2PInformation::operator!=(const P2PInformation& rhs) const
{
	if(players.size() != rhs.players.size())
		return true;
		
	for(int i=0; i<players.size(); ++i)
	{
		if(players[i] != rhs.players[i])
			return true;
	}
	
	return false;
}




