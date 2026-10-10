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
	//                                  magicCreateWood, magicCreateWheat,
	//                                  magicCreateAlga, armor, hpMax }
	constexpr UnitType kDefaultUnitTypes[NB_UNIT_TYPE][NB_UNIT_LEVELS] = {
		// WORKER (baseWorker)
		{
			// level 0
			{ .startImage = {64, 128, 0, 64, 128, 0, 192, 192, 0},
			  .hungriness = 350,
			  .performance = {8, 8, 0, 16, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 200},
			  .harvestDamage = 10,
			  .armorReductionPerHappyness = 0,
			  .experiencePerLevel = 0,
			  .magicActionCooldown = 0 },
			// level 1
			{ .startImage = {64, 128, 0, 64, 128, 0, 192, 192, 0},
			  .hungriness = 350,
			  .performance = {8, 8, 0, 21, 10, 0, 12, 9, 0, 0, 0, 0, 0, 0, 0, 0, 200},
			  .harvestDamage = 10,
			  .armorReductionPerHappyness = 0,
			  .experiencePerLevel = 0,
			  .magicActionCooldown = 0 },
			// level 2
			{ .startImage = {64, 128, 0, 64, 128, 0, 192, 192, 0},
			  .hungriness = 350,
			  .performance = {8, 8, 0, 26, 20, 0, 16, 10, 0, 0, 0, 0, 0, 0, 0, 0, 200},
			  .harvestDamage = 10,
			  .armorReductionPerHappyness = 0,
			  .experiencePerLevel = 0,
			  .magicActionCooldown = 0 },
			// level 3
			{ .startImage = {64, 128, 0, 64, 128, 0, 192, 192, 0},
			  .hungriness = 350,
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
			  .hungriness = 350,
			  .performance = {8, 8, 0, 0, 0, 28, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 38},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 1,
			  .experiencePerLevel = 50,
			  .magicActionCooldown = 3 },
			// level 1 (editor-only)
			{ .startImage = {0, 0, 0, 0, 0, 0, 0, 0, 0},
			  .hungriness = 350,
			  .performance = {8, 8, 0, 0, 0, 28, 0, 0, 0, 0, 6, 0, 4, 4, 4, 0, 38},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 1,
			  .experiencePerLevel = 50,
			  .magicActionCooldown = 3 },
			// level 2 (editor-only)
			{ .startImage = {0, 0, 0, 0, 0, 0, 0, 0, 0},
			  .hungriness = 350,
			  .performance = {8, 8, 0, 0, 0, 28, 0, 0, 0, 0, 6, 0, 3, 3, 3, 0, 38},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 1,
			  .experiencePerLevel = 50,
			  .magicActionCooldown = 3 },
			// level 3
			{ .startImage = {0, 0, 0, 0, 0, 0, 0, 0, 0},
			  .hungriness = 350,
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
			  .hungriness = 350,
			  .performance = {8, 8, 0, 16, 0, 0, 0, 0, 12, 13, 0, 0, 0, 0, 0, 10, 250},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 10,
			  .experiencePerLevel = 20,
			  .magicActionCooldown = 0 },
			// level 1
			{ .startImage = {256, 320, 0, 256, 320, 0, 0, 0, 384},
			  .hungriness = 350,
			  .performance = {8, 8, 0, 21, 8, 0, 0, 0, 16, 14, 0, 0, 0, 0, 0, 10, 250},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 10,
			  .experiencePerLevel = 20,
			  .magicActionCooldown = 0 },
			// level 2
			{ .startImage = {256, 320, 0, 256, 320, 0, 0, 0, 384},
			  .hungriness = 350,
			  .performance = {8, 8, 0, 26, 16, 0, 0, 0, 22, 15, 0, 0, 0, 0, 0, 10, 250},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 10,
			  .experiencePerLevel = 20,
			  .magicActionCooldown = 0 },
			// level 3
			{ .startImage = {256, 320, 0, 256, 320, 0, 0, 0, 384},
			  .hungriness = 350,
			  .performance = {8, 8, 0, 30, 24, 0, 0, 0, 28, 16, 0, 0, 0, 0, 0, 10, 250},
			  .harvestDamage = 0,
			  .armorReductionPerHappyness = 10,
			  .experiencePerLevel = 20,
			  .magicActionCooldown = 0 },
		},
	};

    static_assert([] {
        for (const auto& race : kDefaultUnitTypes) for (const auto& unit : race)
            for (const auto& terrain : TERRAIN_PROPERTIES)
                for (int action : {int(WALK),int(SWIM),int(FLY)})
                {
                    const unsigned factor=action==FLY?terrain.airSpeedQ8:terrain.groundSpeedQ8;
                    if (static_cast<unsigned>(unit.performance[action])*factor/256 > UNIT_DELTA_MAX)
                        return false;
                }
        return true;
    }(), "Default unit terrain movement must not hit the one-cell-per-tick speed cap");

	const Sint32 kDefaultRaceHungriness = 425;
}
