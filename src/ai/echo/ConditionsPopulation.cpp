// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"

using namespace AIEcho;
using namespace AIEcho::Conditions;
using namespace boost::logic;


EitherCondition::EitherCondition(Condition* condition1, Condition* condition2) : condition1(condition1), condition2(condition2)
{

}



EitherCondition::~EitherCondition()
{
	delete condition1;
	delete condition2;
}



boost::logic::tribool EitherCondition::passes(Echo& echo)
{
	tribool p1=condition1->passes(echo);
	tribool p2=condition2->passes(echo);
	if(p1 || p2)
		return true;
	else if(!p1 || !p2)
		return false;
	else
		return indeterminate;
}



ConditionType EitherCondition::get_type()
{
	return CEitherCondition;
}



bool EitherCondition::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("EitherCondition");
	condition1=Condition::load_condition(stream, player, versionMinor);
	condition2=Condition::load_condition(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}



void EitherCondition::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("EitherCondition");
	Condition::save_condition(condition1, stream);
	Condition::save_condition(condition2, stream);
	stream->writeLeaveSection();
}



Population::Population(bool workers, bool explorers, bool warriors, int num, PopulationMethod method) : workers(workers), explorers(explorers), warriors(warriors), num(num), method(method)
{

}



boost::logic::tribool Population::passes(Echo& echo)
{
	int amount=0;
	if(workers)
		amount+=echo.player->team->stats.getLatestStat()->numberUnitPerType[WORKER];
	if(explorers)
		amount+=echo.player->team->stats.getLatestStat()->numberUnitPerType[EXPLORER];
	if(warriors)
		amount+=echo.player->team->stats.getLatestStat()->numberUnitPerType[WARRIOR];
	if(method==Greater)
	{
		return (amount >= num);
	}
	else if(method==Lesser)
	{
		return (amount <= num);
	}
	return false;
}



ConditionType Population::get_type()
{
	return CPopulation;
}



bool Population::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Population");
	workers=stream->readUint8("workers");
	explorers=stream->readUint8("explorers");
	warriors=stream->readUint8("warriors");
	num=stream->readSint32("num");
	method=static_cast<PopulationMethod>(stream->readUint32("method"));
	stream->readLeaveSection();
	return true;
}



void Population::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Population");
	stream->writeUint8(workers, "workers");
	stream->writeUint8(explorers, "explorers");
	stream->writeUint8(warriors, "warriors");
	stream->writeSint32(num, "num");
	stream->writeUint32(static_cast<Uint32>(method), "method");
	stream->writeLeaveSection();
}
