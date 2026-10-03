// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"

using namespace AISharedRuntime;
using namespace AISharedRuntime::Conditions;


EitherCondition::EitherCondition(Condition* condition1, Condition* condition2) : condition1(condition1), condition2(condition2)
{

}



EitherCondition::~EitherCondition()
{
	delete condition1;
	delete condition2;
}



tribool EitherCondition::passes(Runtime& runtime)
{
	tribool p1=condition1->passes(runtime);
	tribool p2=condition2->passes(runtime);
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



tribool Population::passes(Runtime& runtime)
{
	int amount=0;
	if(workers)
		amount+=runtime.player->team->stats.getLatestStat()->numberUnitPerType[WORKER];
	if(explorers)
		amount+=runtime.player->team->stats.getLatestStat()->numberUnitPerType[EXPLORER];
	if(warriors)
		amount+=runtime.player->team->stats.getLatestStat()->numberUnitPerType[WARRIOR];
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
	const Uint32 savedMethod=stream->readUint32("method");
	if (savedMethod > Lesser) return false;
	method=static_cast<PopulationMethod>(savedMethod);
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
