// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "GameObjectives.h"
#include "FileFormatVersions.h"
#include "Stream.h"
#include <cassert>


GameObjectives::GameObjectives() :
	invalidText("invalid")
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



bool GameObjectives::isValidObjectiveIndex(int n) const
{
	// The six vectors are parallel arrays over the same objectives; texts
	// is the reference for the current count.
	assert(hidden.size() == texts.size());
	assert(completed.size() == texts.size());
	assert(failed.size() == texts.size());
	assert(types.size() == texts.size());
	assert(scriptNumbers.size() == texts.size());
	return n >= 0 && n < (int)texts.size();
}



void GameObjectives::removeObjective(int n)
{
	if (!isValidObjectiveIndex(n))
		return;
	texts.erase(texts.begin() + n);
	hidden.erase(hidden.begin() + n);
	completed.erase(completed.begin() + n);
	failed.erase(failed.begin() + n);
	types.erase(types.begin() + n);
	scriptNumbers.erase(scriptNumbers.begin() + n);
}



void GameObjectives::setGameObjectiveText(int n, const std::string& objective)
{
	assert(n < (int)texts.size());
	texts[n] = objective;
}



const std::string& GameObjectives::getGameObjectiveText(int n)
{
	if (n >= 0 && n < (int)texts.size())
		return texts[n];
	else
		return invalidText;
}



void GameObjectives::setObjectiveHidden(int n)
{
	if (n >= 0 && n < (int)hidden.size())
		hidden[n]=true;
}



void GameObjectives::setObjectiveVisible(int n)
{
	if (n >= 0 && n < (int)hidden.size())
		hidden[n]=false;
}



bool GameObjectives::isObjectiveVisible(int n)
{
	if (n >= 0 && n < (int)hidden.size())
		return !hidden[n];
	else
		return false;
}




void GameObjectives::setObjectiveComplete(int n)
{
	assert(completed.size() == failed.size());
	if (n >= 0 && n < (int)completed.size())
	{
		completed[n]=true;
		failed[n]=false;
	}
}



void GameObjectives::setObjectiveIncomplete(int n)
{
	assert(completed.size() == failed.size());
	if (n >= 0 && n < (int)completed.size())
	{
		completed[n]=false;
		failed[n]=false;
	}
}



void GameObjectives::setObjectiveFailed(int n)
{
	assert(completed.size() == failed.size());
	if (n >= 0 && n < (int)completed.size())
	{
		completed[n]=false;
		failed[n]=true;
	}
}



bool GameObjectives::isObjectiveComplete(int n)
{
	if (n >= 0 && n < (int)completed.size())
		return completed[n];
	else
		return false;
}



bool GameObjectives::isObjectiveFailed(int n)
{
	if (n >= 0 && n < (int)failed.size())
		return failed[n];
	else
		return false;
}



void GameObjectives::setObjectiveType(int n, GameObjectiveType type)
{
	assert(n < (int)types.size());
	types[n] = type;
}



GameObjectives::GameObjectiveType GameObjectives::getObjectiveType(int n)
{
	if (n >= 0 && n < (int)types.size())
		return types[n];
	else
		return Invalid;
}



void GameObjectives::setScriptNumber(int n, int scriptNumber)
{
	assert(n < (int)scriptNumbers.size());
	scriptNumbers[n] = scriptNumber;
}



int GameObjectives::getScriptNumber(int n)
{
	assert(n < (int)scriptNumbers.size());
	return scriptNumbers[n];
}



void GameObjectives::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("GameObjectives");
	stream->writeUint32(texts.size(), "size");
	for(unsigned int i=0; i<texts.size(); ++i)
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
	for(unsigned int i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		texts.push_back(stream->readText("text"));
		hidden.push_back(stream->readUint8("hidden"));
		completed.push_back(stream->readUint8("completed"));
		if(versionMinor>=FILE_FORMAT_VERSION_BRIEFING_HINTS_OBJ_FAILED)
			failed.push_back(stream->readUint8("failed"));
		else
			failed.push_back(false);
		types.push_back(static_cast<GameObjectiveType>(stream->readUint8("type")));
		scriptNumbers.push_back(stream->readUint8("scriptNumber"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
}



