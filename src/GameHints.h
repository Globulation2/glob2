// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef GameHints_h
#define GameHints_h

#include <string>
#include <vector>
#include "SDL_net.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}
///This class is similar to the GameObjectives class, except that its meant for the game hints
class GameHints
{
public:
	GameHints();
	
	///This gets the number of hints there are
	int getNumberOfHints();
	///This adds a new hint
	void addNewHint(const std::string& hint, bool hidden, int scriptNumber);
	///This removes the given hint
	void removeHint(int n);

	///This sets the text for the game hint at n
	void setGameHintText(int n, const std::string& hint);
	///This returns the text for the game hint at n
	const std::string& getGameHintText(int n);
	
	
	///This sets the given hint text as hidden
	void setHintHidden(int n);
	///This sets the given hint text as visible
	void setHintVisible(int n);
	///This returns true if the given hint text is visible
	bool isHintVisible(int n);
	
	///This sets the script number, which is how scripts will reference the given object
	void setScriptNumber(int n, int scriptNumber);
	///This returns the script number, which is how scripts will reference the given object
	int getScriptNumber(int n);

	///Encodes this GameObjectives into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;
	///Decodes this GameObjectives from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
private:
	std::vector<std::string> texts;
	std::vector<bool> hidden;
	std::vector<int> scriptNumbers;
};

#endif
