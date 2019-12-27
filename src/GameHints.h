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
