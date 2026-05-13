// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <memory>
#include "SDL_net.h"
#include <list>


class Game;

namespace GAGCore
{
	class OutputStream;
	class InputStream;
};

///These are the type of winning conditions there are
enum WinningConditionType
{
	WCUnknown,
	WCDeath,
	WCAllies,
	WCPrestige,
	WCScript,
	WCOpponentsDefeated,
};

///This represents a generic winning condition. Each condition may specify which teams have won,
///which have lost, or possibly, both
class WinningCondition
{
public:
	virtual ~WinningCondition() = default;

	///Returns true if the particular player has won according to this winning condition
	virtual bool hasTeamWon(int team, const Game* game) const = 0;
	///Returns true if the particular player has lost according to this winning condition
	virtual bool hasTeamLost(int team, const Game* game) const = 0;
	///Returns the winning condition type
	virtual WinningConditionType getType() const=0;
	///This will encode the data in this winning condition to a stream. All derived class must start by saving a Uint8 from getType()
	virtual void encodeData(GAGCore::OutputStream* stream) const = 0;
	///This will decode data. It is important that, unlike encodeData, this must ignore the initial Uint8
	virtual void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)=0;

	///This will reconstruct a winning condition from serialized data
	static std::shared_ptr<WinningCondition> getWinningCondition(GAGCore::InputStream* stream, Uint32 versionMinor);
	///This will set the given list to the default set of winning conditions, in their default order
	static std::list<std::shared_ptr<WinningCondition> > getDefaultWinningConditions();

};

///A team has lost if its dead.
class WinningConditionDeath : public WinningCondition
{
public:
	bool hasTeamWon(int team, const Game* game) const override;
	bool hasTeamLost(int team, const Game* game) const override;
	WinningConditionType getType() const override;
	void encodeData(GAGCore::OutputStream* stream) const override;
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor) override;
};

///A team has won if one of its allies has won
class WinningConditionAllies : public WinningCondition
{
public:
	bool hasTeamWon(int team, const Game* game) const override;
	bool hasTeamLost(int team, const Game* game) const override;
	WinningConditionType getType() const override;
	void encodeData(GAGCore::OutputStream* stream) const override;
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor) override;
};

///A team has won if the prestige limit is reached and its above the prestige amount
///and a team has lost if the prestige limit is reached and its below the prestige amount
class WinningConditionPrestige : public WinningCondition
{
public:
	bool hasTeamWon(int team, const Game* game) const override;
	bool hasTeamLost(int team, const Game* game) const override;
	WinningConditionType getType() const override;
	void encodeData(GAGCore::OutputStream* stream) const override;
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor) override;
};

///A team has won if the script says it has won, and lost if the script says it has lost.
///In server builds, hasTeamWon/hasTeamLost are stubbed to false: SGSL.cpp is not in the
///server link, and Team::checkWinConditions (the sole caller) is client-only.
class WinningConditionScript : public WinningCondition
{
public:
	bool hasTeamWon(int team, const Game* game) const override;
	bool hasTeamLost(int team, const Game* game) const override;
	WinningConditionType getType() const override;
	void encodeData(GAGCore::OutputStream* stream) const override;
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor) override;
};

///A team has won if all enemies have lost
class WinningConditionOpponentsDefeated : public WinningCondition
{
public:
	bool hasTeamWon(int team, const Game* game) const override;
	bool hasTeamLost(int team, const Game* game) const override;
	WinningConditionType getType() const override;
	void encodeData(GAGCore::OutputStream* stream) const override;
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor) override;
};



