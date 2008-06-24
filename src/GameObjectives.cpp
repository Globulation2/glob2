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

#include "GameObjectives.h"
#include "Stream.h"

#include <iostream>


GameObjectives::GameObjectives()
{
	texts.push_back("[Defeat Your Oppenents]");
	hidden.push_back(false);
	completed.push_back(false);
	failed.push_back(false);
	types.push_back(Primary);
	scriptNumbers.push_back(1);
}



int GameObjectives::getNumberOfObjectives()
{
	return texts.size();
}



void GameObjectives::addNewObjective(const std::string& objective, bool ishidden, bool complete, bool nfailed, GameObjectiveType type, int scriptNumber)
{
	texts.push_back(objective);
	hidden.push_back(ishidden);
	completed.push_back(complete);
	failed.push_back(nfailed);
	types.push_back(type);
	scriptNumbers.push_back(scriptNumber);
}



void GameObjectives::removeObjective(int n)
{
	texts.erase(texts.begin() + n);
	hidden.erase(hidden.begin() + n);
	completed.erase(completed.begin() + n);
	failed.erase(failed.begin() + n);
	types.erase(types.begin() + n);
	scriptNumbers.erase(scriptNumbers.begin() + n);
}



void GameObjectives::setGameObjectiveText(int n, const std::string& objective)
{
	texts[n] = objective;
}



const std::string& GameObjectives::getGameObjectiveText(int n)
{
	return texts[n];
}



void GameObjectives::setObjectiveHidden(int n)
{
	hidden[n]=true;
}



void GameObjectives::setObjectiveVisible(int n)
{
	hidden[n]=false;
}



bool GameObjectives::isObjectiveVisible(int n)
{
	return !hidden[n];
}




void GameObjectives::setObjectiveComplete(int n)
{
	completed[n]=true;
	failed[n]=false;
}



void GameObjectives::setObjectiveIncomplete(int n)
{
	completed[n]=false;
	failed[n]=false;
}



void GameObjectives::setObjectiveFailed(int n)
{
	completed[n]=false;
	failed[n]=true;
}



bool GameObjectives::isObjectiveComplete(int n)
{
	return completed[n];
}



bool GameObjectives::isObjectiveFailed(int n)
{
	return failed[n];
}



void GameObjectives::setObjectiveType(int n, GameObjectiveType type)
{
	types[n] = type;
}



GameObjectives::GameObjectiveType GameObjectives::getObjectiveType(int n)
{
	return types[n];
}



void GameObjectives::setScriptNumber(int n, int scriptNumber)
{
	scriptNumbers[n] = scriptNumber;
}



int GameObjectives::getScriptNumber(int n)
{
	return scriptNumbers[n];
}



void GameObjectives::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("GameObjectives");
	stream->writeUint32(texts.size(), "size");
	for(int i=0; i<texts.size(); ++i)
	{
		stream->writeEnterSection(i);
		stream->writeText(texts[i], "text");
		stream->writeUint8(hidden[i], "hidden");
		stream->writeUint8(completed[i], "completed");
		stream->writeUint8(failed[i], "failed");
		stream->writeUint8(types[i], "type");
		stream->writeUint8(scriptNumbers[i], "scriptNumber");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}



void GameObjectives::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	texts.clear();
	hidden.clear();
	completed.clear();
	failed.clear();
	types.clear();
	scriptNumbers.clear();
	stream->readEnterSection("GameObjectives");
	Uint32 size = stream->readUint32("size");
	for(int i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		texts.push_back(stream->readText("text"));
		hidden.push_back(stream->readUint8("hidden"));
		completed.push_back(stream->readUint8("completed"));
		if(versionMinor>=76)
			failed.push_back(stream->readUint8("failed"));
		else
			failed.push_back(false);
		types.push_back(static_cast<GameObjectiveType>(stream->readUint8("type")));
		scriptNumbers.push_back(stream->readUint8("scriptNumber"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
}



