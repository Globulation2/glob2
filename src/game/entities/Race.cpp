// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <assert.h>

#include <Stream.h>

#include "Race.h"

UnitType Race::unitTypes[NB_UNIT_TYPE][NB_UNIT_LEVELS];
Sint32 Race::hungryness;

namespace
{
	// Compile-time defaults transcribed from the legacy data/units.txt. Index
	// is [unit_type][level] — outer dim is WORKER/EXPLORER/WARRIOR, inner is
	// upgrade level 0..3. Values are positional in the array fields:
	//   startImage[NB_MOVE=9] = { stopWalk, stopSwim, stopFly,
	//                             walk, swim, fly, build, harvest, attack }
	//   performance[NB_ABILITY=17] = { stopWalk, stopSwim, stopFly,
	//                                  walk, swim, fly, build, harvest,
	//                                  attackSpeed, attackForce,
	//                                  magicAttackAir, magicAttackGround,
	//                                  magicCreateWood, magicCreateCorn,
	//                                  magicCreateAlga, armor, hpMax }
	const UnitType kDefaultUnitTypes[NB_UNIT_TYPE][NB_UNIT_LEVELS] = {
		// WORKER (baseWorker)
		{
			// level 0
			{ .startImage = {64, 128, 0, 64, 128, 0, 192, 192, 0},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 16, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 200},
			  .harvestDamage = 10,
			  .armorReductionPerHappyness = 0,
			  .experiencePerLevel = 0,
			  .magicActionCooldown = 0 },
			// level 1
			{ .startImage = {64, 128, 0, 64, 128, 0, 192, 192, 0},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 21, 10, 0, 12, 9, 0, 0, 0, 0, 0, 0, 0, 0, 200},
			  .harvestDamage = 10,
			  .armorReductionPerHappyness = 0,
			  .experiencePerLevel = 0,
			  .magicActionCooldown = 0 },
			// level 2
			{ .startImage = {64, 128, 0, 64, 128, 0, 192, 192, 0},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 26, 20, 0, 16, 10, 0, 0, 0, 0, 0, 0, 0, 0, 200},
			  .harvestDamage = 10,
			  .armorReductionPerHappyness = 0,
			  .experiencePerLevel = 0,
			  .magicActionCooldown = 0 },
			// level 3
			{ .startImage = {64, 128, 0, 64, 128, 0, 192, 192, 0},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 30, 30, 0, 20, 11, 0, 0, 0, 0, 0, 0, 0, 0, 200},
			  .harvestDamage = 10,
			  .armorReductionPerHappyness = 0,
			  .experiencePerLevel = 0,
			  .magicActionCooldown = 0 },
		},
		// EXPLORER (baseExplorer)
		{
			// level 0
			{ .startImage = {0, 0, 0, 0, 0, 0, 0, 0, 0},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 0, 0, 28, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 38},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 1,
			  .experiencePerLevel = 50,
			  .magicActionCooldown = 3 },
			// level 1 (editor-only)
			{ .startImage = {0, 0, 0, 0, 0, 0, 0, 0, 0},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 0, 0, 28, 0, 0, 0, 0, 6, 0, 4, 4, 4, 0, 38},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 1,
			  .experiencePerLevel = 50,
			  .magicActionCooldown = 3 },
			// level 2 (editor-only)
			{ .startImage = {0, 0, 0, 0, 0, 0, 0, 0, 0},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 0, 0, 28, 0, 0, 0, 0, 6, 0, 3, 3, 3, 0, 38},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 1,
			  .experiencePerLevel = 50,
			  .magicActionCooldown = 3 },
			// level 3
			{ .startImage = {0, 0, 0, 0, 0, 0, 0, 0, 0},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 0, 0, 28, 0, 0, 0, 0, 6, 8, 2, 2, 2, 0, 38},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 1,
			  .experiencePerLevel = 50,
			  .magicActionCooldown = 3 },
		},
		// WARRIOR (baseWarrior)
		{
			// level 0
			{ .startImage = {256, 320, 0, 256, 320, 0, 0, 0, 384},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 16, 0, 0, 0, 0, 12, 13, 0, 0, 0, 0, 0, 10, 250},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 10,
			  .experiencePerLevel = 20,
			  .magicActionCooldown = 0 },
			// level 1
			{ .startImage = {256, 320, 0, 256, 320, 0, 0, 0, 384},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 21, 8, 0, 0, 0, 16, 14, 0, 0, 0, 0, 0, 10, 250},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 10,
			  .experiencePerLevel = 20,
			  .magicActionCooldown = 0 },
			// level 2
			{ .startImage = {256, 320, 0, 256, 320, 0, 0, 0, 384},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 26, 16, 0, 0, 0, 22, 15, 0, 0, 0, 0, 0, 10, 250},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 10,
			  .experiencePerLevel = 20,
			  .magicActionCooldown = 0 },
			// level 3
			{ .startImage = {256, 320, 0, 256, 320, 0, 0, 0, 384},
			  .hungryness = 350,
			  .performance = {8, 8, 0, 30, 24, 0, 0, 0, 28, 16, 0, 0, 0, 0, 0, 10, 250},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 10,
			  .experiencePerLevel = 20,
			  .magicActionCooldown = 0 },
		},
	};

	const Sint32 kDefaultRaceHungryness = 425;
}

Race::Race()
{
}

Race::~Race()
{
}

void Race::loadDefault()
{
	hungryness = kDefaultRaceHungryness;
	for (int t = 0; t < NB_UNIT_TYPE; ++t)
		for (int l = 0; l < NB_UNIT_LEVELS; ++l)
			unitTypes[t][l] = kDefaultUnitTypes[t][l];
}

void Race::load()
{
}

UnitType *Race::getUnitType(int type, int level)
{
	assert (level>=0);
	assert (level<NB_UNIT_LEVELS);
	assert (type>=0);
	assert (type<NB_UNIT_TYPE);
	return &(unitTypes[type][level]);
}

void Race::save(GAGCore::OutputStream *stream)
{
	for (int i=0; i<NB_UNIT_TYPE; i++)
		for(int j=0; j<NB_UNIT_LEVELS; j++)
			unitTypes[i][j].save(stream);

	stream->writeSint32(hungryness, "hungryness");
}

bool Race::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	for (int i=0; i<NB_UNIT_TYPE; i++)
		for(int j=0; j<NB_UNIT_LEVELS; j++)
			unitTypes[i][j].load(stream, versionMinor);

	hungryness = (Sint32)stream->readSint32("hungryness");

	return true;
}
