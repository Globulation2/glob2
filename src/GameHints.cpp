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

#include "GameHints.h"
#include "Stream.h"

GameHints::GameHints()
{
	
}



int GameHints::getNumberOfHints()
{
	return texts.size();
}



void GameHints::addNewHint(const std::string& hint, bool nhidden, int scriptNumber)
{
	texts.push_back(hint);
	hidden.push_back(nhidden);
	scriptNumbers.push_back(scriptNumber);
}



void GameHints::removeHint(int n)
{
	texts.erase(texts.begin() + n);
	hidden.erase(hidden.begin() + n);
	scriptNumbers.erase(scriptNumbers.begin() + n);
}



void GameHints::setGameHintText(int n, const std::string& hint)
{
	texts[n]=hint;
}



const std::string& GameHints::getGameHintText(int n)
{
	return texts[n];
}



void GameHints::setHintHidden(int n)
{
	hidden[n]=true;
}



void GameHints::setHintVisible(int n)
{
	hidden[n]=false;
}



bool GameHints::isHintVisible(int n)
{
	return !hidden[n];
}



void GameHints::setScriptNumber(int n, int scriptNumber)
{
	scriptNumbers[n]=scriptNumber;
}



int GameHints::getScriptNumber(int n)
{
	return scriptNumbers[n];
}



void GameHints::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("GameHints");
	stream->writeUint32(texts.size(), "size");
	for(int i=0; i<texts.size(); ++i)
	{
		stream->writeEnterSection(i);
		stream->writeText(texts[i], "text");
		stream->writeUint8(hidden[i], "hidden");
		stream->writeUint8(scriptNumbers[i], "scriptNumber");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}



void GameHints::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	texts.clear();
	hidden.clear();
	scriptNumbers.clear();
	stream->readEnterSection("GameHints");
	Uint32 size = stream->readUint32("size");
	for(int i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		texts.push_back(stream->readText("text"));
		hidden.push_back(stream->readUint8("hidden"));
		scriptNumbers.push_back(stream->readUint8("scriptNumber"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
}




