// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "GameHints.h"
#include "ScriptNumber.h"
#include "Stream.h"
#include <cassert>

GameHints::GameHints()
{
	
}



int GameHints::getNumberOfHints() const
{
	return static_cast<int>(texts.size());
}



void GameHints::addNewHint(const std::string& hint, bool nhidden, int scriptNumber)
{
	texts.push_back(hint);
	hidden.push_back(nhidden);
	scriptNumbers.push_back(ScriptNumber::clampToWireDomain(scriptNumber));
}



void GameHints::removeHint(int n)
{
	texts.erase(texts.begin() + n);
	hidden.erase(hidden.begin() + n);
	scriptNumbers.erase(scriptNumbers.begin() + n);
}



void GameHints::setGameHintText(int n, const std::string& hint)
{
	assert (n < (int)texts.size());
	texts[n]=hint;
}



const std::string& GameHints::getGameHintText(int n) const
{
	assert (n < (int)texts.size());
	return texts[n];
}



void GameHints::setHintHidden(int n)
{
	if (n >= 0 && n < (int)hidden.size())
		hidden[n]=true;
}



void GameHints::setHintVisible(int n)
{
	if (n >= 0 && n < (int)hidden.size())
		hidden[n]=false;
}



bool GameHints::isHintVisible(int n) const
{
	if (n >= 0 && n < (int)hidden.size())
		return !hidden[n];
	else
		return false;
}



void GameHints::setScriptNumber(int n, int scriptNumber)
{
	assert(n < (int)scriptNumbers.size());
	scriptNumbers[n]=ScriptNumber::clampToWireDomain(scriptNumber);
}



int GameHints::getScriptNumber(int n) const
{
	assert(n < (int)scriptNumbers.size());
	return scriptNumbers[n];
}



void GameHints::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("GameHints");
	stream->writeUint32(texts.size(), "size");
	for(unsigned int i=0; i<texts.size(); ++i)
	{
		stream->writeEnterSection(i);
		stream->writeText(texts[i], "text");
		stream->writeUint8(hidden[i], "hidden");
		// Fits by construction: every scriptNumbers entry point clamps to
		// the [0..MaxScriptNumber] Uint8 wire domain.
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
	for(unsigned int i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		texts.push_back(stream->readText("text"));
		hidden.push_back(stream->readUint8("hidden"));
		scriptNumbers.push_back(stream->readUint8("scriptNumber"));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
}




