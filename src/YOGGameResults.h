// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGGameResults_h
#define YOGGameResults_h

#include "YOGConsts.h"
#include <map>

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This stores the win/lose/disconnected results for a single game
class YOGGameResults
{
public:
	///Constructs a default yog game results
	YOGGameResults();

	///Sets the game result state for a particular player
	void setGameResultState(const std::string& player, YOGGameResult result);

	///Gets the game result state for a particular player
	YOGGameResult getGameResultState(const std::string& player);

	///Encodes this YOGGameResults into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGGameResults from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 netDataVersion);
	
	///Test for equality between two YOGGameResults
	bool operator==(const YOGGameResults& rhs) const;
	bool operator!=(const YOGGameResults& rhs) const;
private:
	std::map<std::string, YOGGameResult> results;
};

#endif
