// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <string>
#include <vector>
#include "SDL_net.h"
#include "ScriptNumber.h"

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
		Primary = 0,
		Secondary,
		Invalid
	};

	///Returned by getScriptNumber for an out-of-range index. Real script
	///numbers are 1-based (the editor assigns 1..16), so 0 never matches
	///an objective a script can reference.
	static constexpr int InvalidScriptNumber = 0;

	///Upper bound of the storable script number domain. Stock content uses 1..16.
	static constexpr int MaxScriptNumber = ScriptNumber::Max;

	///This gets the number of objectives there are
	int getNumberOfObjectives();
	///This adds a new objective. scriptNumber is clamped to [0..MaxScriptNumber]
	void addNewObjective(const std::string& objective, bool hidden, bool complete, bool failed, GameObjectiveType type, int scriptNumber);
	///This removes the given objective
	void removeObjective(int n);

	///This sets the text for the game objective at n; ignored if n is out of range
	void setGameObjectiveText(int n, const std::string& objective);
	///This returns the text for the game objective at n, or "invalid" if n is out of range
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
	
	///This sets the given objective type; ignored if n is out of range
	void setObjectiveType(int n, GameObjectiveType type);
	///This returns the given objective type, or Invalid if n is out of range
	GameObjectiveType getObjectiveType(int n);

	///This sets the script number, which is how scripts will reference the given object;
	///ignored if n is out of range. The value is clamped to [0..MaxScriptNumber]
	void setScriptNumber(int n, int scriptNumber);
	///This returns the script number, which is how scripts will reference the given object,
	///or InvalidScriptNumber if n is out of range
	int getScriptNumber(int n);

	///Encodes this GameObjectives into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;
	///Decodes this GameObjectives from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
	
private:
	///True if n indexes an existing objective. All accessors share this
	///guard: out-of-range indices are silently ignored by setters and give
	///the documented defaults from getters.
	bool isValidObjectiveIndex(int n) const;

	std::vector<std::string> texts;
	std::vector<bool> hidden;
	std::vector<bool> completed;
	std::vector<bool> failed;
	std::vector<GameObjectiveType> types;
	std::vector<int> scriptNumbers;
	std::string invalidText;
};

