// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

/*
What's in AI ?
AI represents the behaviour of an artificial intelligence player.
The main method is std::shared_ptr<Order> getOrder() which return the order to be used by the AI's team.
*/

#include "BuildingType.h"
#include <memory>
#include <string>
#include <vector>

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}
class Player;
class Order;

/*
Howto make a new AI ?
If you want to build a new way AI behave, you have to:
Add a new Strategy to the AI::ImplementationID enum.
Add a new case in the AI::load method.
Create a subclass of AIImplementation.
Fill AIImplementation's methods correctly for that subclass.

Warning:
You have to understand how the Order class is used.
Never use rand(), always syncRand().
(because the AI need to behave exactly the same on every computer.)
Be sure to return at least a *NullOrder, not NULL.

Idea:
You can access useful data this way:
player
player->team
player->team->game
player->team->game->map
The current AIs store pointers to all these for convenient access.

Fairness:
AI don't have restricted access to hidden part of the map.
You have to check it yourself, please do it.
Please don't use too much CPU either.
Test games with a lot of AI for that.

Gameplay:
Player and AI may play together, in the same team.
Think if your AI is able to play with a human player?
*/

struct AIDiagnosticRow
{
	AIDiagnosticRow(const std::string& label, const std::string& value)
		: label(label), value(value) {}
	std::string label;
	std::string value;
};

struct AIDiagnosticSection
{
	explicit AIDiagnosticSection(const std::string& title=std::string())
		: title(title) {}
	std::string title;
	std::vector<AIDiagnosticRow> rows;
};

enum AITopologyMovementMode
{
	AITopologyLand=0,
	AITopologyAmphibious=1
};

enum AITopologyCandidateState
{
	AITopologyCandidateUnselected=0,
	AITopologyCandidateSelected=1,
	AITopologyCandidateRejectedOverlap=2,
	AITopologyCandidateRejectedCap=3
};

struct AITopologyDiagnosticTeam
{
	AITopologyDiagnosticTeam() : team(-1), shortestDistance(-1) {}
	int team;
	int shortestDistance;
	std::vector<int> enemyDistance;
	std::vector<unsigned char> corridor;
	std::vector<int> corridorWidth;
	std::vector<int> terrainWidth;
};

struct AITopologyDiagnosticMode
{
	AITopologyDiagnosticMode()
		: mode(AITopologyLand), enabled(false), candidateCount(0),
		  selectedCount(0) {}
	AITopologyMovementMode mode;
	bool enabled;
	int candidateCount;
	int selectedCount;
	std::vector<unsigned char> walkable;
	std::vector<int> homeDistance;
	std::vector<AITopologyDiagnosticTeam> teams;
	std::vector<int> memberships;
	std::vector<int> minimumCrossSection;
	std::vector<int> minimumTerrainCrossSection;
	std::vector<unsigned char> qualified;
};

struct AITopologyDiagnosticCandidate
{
	AITopologyDiagnosticCandidate()
		: mode(AITopologyLand), index(-1), memberships(0), crossSection(-1),
		  terrainCrossSection(-1), homeDistance(-1),
		  state(AITopologyCandidateUnselected) {}
	AITopologyMovementMode mode;
	int index;
	int memberships;
	int crossSection;
	int terrainCrossSection;
	int homeDistance;
	AITopologyCandidateState state;
};

struct AITopologyDiagnosticSnapshot
{
	AITopologyDiagnosticSnapshot()
		: width(0), height(0), tick(-1), active(false), trainedWarriors(0),
		  swimmingWarriors(0), effectiveZoneCap(0), selectedCount(0),
		  innerDistance(0), bandMaximum(0), pathSlack(0),
		  maximumCrossSection(0) {}
	int width;
	int height;
	int tick;
	bool active;
	int trainedWarriors;
	int swimmingWarriors;
	int effectiveZoneCap;
	int selectedCount;
	int innerDistance;
	int bandMaximum;
	int pathSlack;
	int maximumCrossSection;
	std::vector<AITopologyDiagnosticMode> modes;
	std::vector<AITopologyDiagnosticCandidate> candidates;
	std::vector<unsigned char> desired;
};

class AIImplementation
{
public:
	AIImplementation(){}
	virtual ~AIImplementation(){}
	
	virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
	virtual void save(GAGCore::OutputStream *stream)=0;
	
	virtual std::shared_ptr<Order> getOrder(void)=0;

    virtual void getDiagnosticSections(std::vector<AIDiagnosticSection>& sections) const { sections.clear(); }
    virtual const AITopologyDiagnosticSnapshot* getTopologyDiagnosticSnapshot() const { return nullptr; }
};


 

