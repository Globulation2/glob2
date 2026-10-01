// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptValue.h"
#include <array>
#include <memory>
class Game;
class Unit;
class Building;
namespace GAGCore {class InputStream;class OutputStream;}
namespace Script
{
class Observations
{
 struct RememberedTile {unsigned tick=0;unsigned short terrain=0,fertility=0;unsigned char type=255,variety=0,amount=0;bool known=false;};
 Game& game;
 int team;
 // Lazily allocated indexed chunks avoid tree lookups without allocating an
 // entire map per controller before it has explored any terrain.
 using Chunk=std::array<RememberedTile,256>;
 std::vector<std::unique_ptr<Chunk>> remembered;
 unsigned knownTiles=0;
 const RememberedTile* lookup(unsigned index) const;
 RememberedTile& remember(unsigned index);
 unsigned lastTick=0xffffffffu;
 Value ref(const Unit* unit) const;
 Value ref(const Building* building) const;
 Value unit(const Unit& unit) const;
 Value building(const Building& building) const;
 Value tile(int x,int y) const;
public:
 Observations(Game& game,int team):game(game),team(team){}
 void observe();
 Value query(const std::string& name,const std::vector<Value>& args,const QueryBudget& budget={}) const;
 void save(GAGCore::OutputStream* stream) const;
 void load(GAGCore::InputStream* stream);
};
}
