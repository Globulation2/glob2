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


#ifndef GameObjectives_h
#define GameObjectives_h

#include <string>
#include <vector>
#include "SDL_net.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This class stores the list of game objectives, which the map script may arbitrarily
///hide, reveal, set as complete, or set as incomplete
class GameObjectives
{
public:
	GameObjectives();
	
	enum GameObjectiveType
	{
		Primary,
		Secondary,
	};

	///This gets the number of objectives there are
	int getNumberOfObjectives();
	///This adds a new objective
	void addNewObjective(const std::string& objective, bool hidden, bool complete, bool failed, GameObjectiveType type, int scriptNumber);
	///This removes the given objective
	void removeObjective(int n);

	///This sets the text for the game objective at n
	void setGameObjectiveText(int n, const std::string& objective);
	///This returns the text for the game objective at n
	const std::string& getGameObjectiveText(int n);
	
	
	///This sets the given objective text as hidden
	void setObjectiveHidden(int n);
	///This sets the given objective text as visible
	void setObjectiveVisible(int n);
	///This returns true if the given objective text is visible
	bool isObjectiveVisible(int n);
	
	///This sets the given objective text as complete
	void setObjectiveComplete(int n);
	///This sets the given objective text as incomplete
	void setObjectiveIncomplete(int n);
	///This sets the given objective text as failed
	void setObjectiveFailed(int n);
	///This returns true if the given objective is complete
	bool isObjectiveComplete(int n);
	///This returns true if the given objective is failed
	bool isObjectiveFailed(int n);
	
	///This sets the given objective type
	void setObjectiveType(int n, GameObjectiveType type);
	///This returns the given objective type
	GameObjectiveType getObjectiveType(int n);
	
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
	std::vector<bool> completed;
	std::vector<bool> failed;
	std::vector<GameObjectiveType> types;
	std::vector<int> scriptNumbers;
};

#endif
