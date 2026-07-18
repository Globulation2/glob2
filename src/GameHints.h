// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

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

	///Script numbers travel as a single byte on the wire (encodeData writes
	///Uint8), so the storable domain is [0..MaxScriptNumber]. addNewHint and
	///setScriptNumber clamp to this range so the in-memory value always
	///matches what a save/load round-trip yields. Stock content uses 1..8
	///(the script editor has eight hint slots). Mirrors
	///GameObjectives::MaxScriptNumber.
	static constexpr int MaxScriptNumber = 255;

	///Returns the number of hints. The count is returned as int (not size_t)
	///because every caller indexes hints with int (script numbers, UI loops)
	///and the serialized count is a Uint32; hint lists are tiny (campaign
	///hints, single digits), so the narrowing can never overflow in practice.
	int getNumberOfHints() const;
	///This adds a new hint. scriptNumber is clamped to [0..MaxScriptNumber]
	void addNewHint(const std::string& hint, bool hidden, int scriptNumber);
	///This removes the given hint
	void removeHint(int n);

	///This sets the text for the game hint at n
	void setGameHintText(int n, const std::string& hint);
	///This returns the text for the game hint at n
	const std::string& getGameHintText(int n) const;
	
	
	///This sets the given hint text as hidden
	void setHintHidden(int n);
	///This sets the given hint text as visible
	void setHintVisible(int n);
	///This returns true if the given hint text is visible
	bool isHintVisible(int n) const;
	
	///This sets the script number, which is how scripts will reference the given object.
	///The value is clamped to [0..MaxScriptNumber]
	void setScriptNumber(int n, int scriptNumber);
	///This returns the script number, which is how scripts will reference the given object
	int getScriptNumber(int n) const;

	///Encodes this GameObjectives into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;
	///Decodes this GameObjectives from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
private:
	std::vector<std::string> texts;
	std::vector<bool> hidden;
	std::vector<int> scriptNumbers;
};

