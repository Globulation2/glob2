// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AreaEffects.h"
#include "BuildingType.h"
#include "ResourceGrowth.h"
#include "Sector.h"
#include "Bullet.h"
#include "sim/snapshot/SnapshotStorage.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <fstream>

namespace
{
using Json = nlohmann::json;
using namespace BuildingAreaEffects;
void install(Game &game, const Json &aura, const Json &strong = Json{})
{
	auto catalog = Json::parse(game.buildingsTypes.snapshotJson());
	const int id = game.buildingsTypes.getTypeNum("stonewall", 0, false);
	catalog["variants"][id]["semantics"]["areaEffects"] = aura;
	if (!strong.is_null())
	{
		const int other = game.buildingsTypes.getTypeNum("inn", 0, false);
		catalog["variants"][other]["semantics"]["areaEffects"] = strong;
	}
	game.buildingsTypes.loadSnapshotJson(catalog.dump());
	game.configureBuildingCatalog();
}
void refresh(glob2test::HeadlessGame &world, Uint32 tick)
{
	world.game.stepCounter = tick;
	world.game.areaEffects.beginTick(world.game);
}
Uint16 value(Game &game, Channel c, int x, int y, int team = 0)
{
	return game.areaEffects.at(c, team, game.map.coordToIndex(x, y));
}
std::vector<Uint32> components(Game &game)
{
	std::vector<Uint32> state, buildings, units;
	game.checkSum(&state, &buildings, &units, true);
	state.erase(state.begin());
	state.insert(state.end(), buildings.begin(), buildings.end());
	state.insert(state.end(), units.begin(), units.end());
	return state;
}
} // namespace
TEST_SUITE("BuildingAreaEffects")
{
	TEST_CASE("catalog bounds identity and canonical absence")
	{
		glob2test::HeadlessGlobals globals;
		BuildingsTypes stock;
		stock.initLegacy();
		const auto original = stock.snapshotJson();
		const auto hash = stock.fingerprint();
		auto json = Json::parse(original);
		const int id = stock.getTypeNum("stonewall", 0, false);
		json["variants"][id]["semantics"]["areaEffects"] = Json::object();
		stock.loadSnapshotJson(json.dump());
		CHECK(stock.snapshotJson() == original);
		CHECK(stock.fingerprint() == hash);
		for (const auto &[key, bad] :
			 std::vector<std::pair<std::string, Json>>{{"radius", -1},
													   {"radius", 65536},
													   {"healingQ8", 65536},
													   {"damageQ8", -1},
													   {"feedingQ8", 1.5},
													   {"attackBuffBps", 30001},
													   {"armorWeaknessBps", 10001},
													   {"fertilityBuffBps", -1},
													   {"unknown", 1},
													   {"cost", {{"food", -1}}}})
		{
			CAPTURE(key);
			auto invalid = json;
			invalid["variants"][id]["semantics"]["areaEffects"][key] = bad;
			CHECK_THROWS(stock.loadSnapshotJson(invalid.dump()));
			CHECK(stock.fingerprint() == hash);
		}
		json["variants"][id]["semantics"]["areaEffects"] = {
			{"radius", 4}, {"healingQ8", 128}, {"cost", {{"food", 1}}}};
		stock.loadSnapshotJson(json.dump());
		CHECK(stock.fingerprint() != hash);
		BuildingsTypes copy;
		copy.loadSnapshotJson(stock.snapshotJson());
		CHECK(copy.fingerprint() == stock.fingerprint());
		CHECK(sizeof(BuildingRuntimeTraits) == 64);
	}
	TEST_CASE("strongest fields match reference across wrapped oversized and changed coverage")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world(
			{.wDec = 5, .hDec = 5, .teams = 3, .loadDefaultRace = true, .header = true});
		const Json weak = {{"radius", 3},
						   {"healingQ8", 128},
						   {"damageQ8", 256},
						   {"feedingQ8", 512},
						   {"attackBuffBps", 2000},
						   {"armorBuffBps", 3000},
						   {"attackWeaknessBps", 1000},
						   {"armorWeaknessBps", 2000},
						   {"fertilityBuffBps", 5000},
						   {"fertilityWeaknessBps", 1000}};
		auto strong = weak;
		strong["radius"] = 65535;
		strong["healingQ8"] = 512;
		strong["attackBuffBps"] = 7000;
		strong["attackWeaknessBps"] = 4000;
		strong["armorWeaknessBps"] = 6000;
		strong["fertilityBuffBps"] = 12000;
		strong["fertilityWeaknessBps"] = 4000;
		install(world.game, weak, strong);
		auto *a = world.addBuilding("stonewall", 31, 31, 0, 0);
		auto *b = world.addBuilding("inn", 8, 8, 0, 1);
		world.game.teams[0]->allies = 3;
		world.game.teams[0]->enemies = 4;
		world.game.teams[1]->allies = 2;
		world.game.teams[1]->enemies = 5;
		refresh(world, 0);
		const auto compare = [&]
		{
			for (int t = 0; t < 3; ++t)
				for (int y = 0; y < 32; ++y)
					for (int x = 0; x < 32; ++x)
					{
						int heal = 0, damage = 0, feed = 0, attack = 0, attackWeak = 0, armor = 0,
							armorWeak = 0, fertilityBuff = 0, fertilityWeak = 0;
						for (auto *emitter : {a, b})
							if (emitter && emitter->areaFunded &&
								emitter->buildingState != Building::DEAD)
							{
								const auto &s = emitter->type->semantics.areaEffects;
								// Independent wrapped distance from each individual footprint tile.
								int distance = 100000;
								for (int dy = 0; dy < emitter->type->height; ++dy)
									for (int dx = 0; dx < emitter->type->width; ++dx)
									{
										int vx = std::abs(x - ((emitter->posX + dx) & 31)),
											vy = std::abs(y - ((emitter->posY + dy) & 31));
										distance =
											std::min(distance, std::max(std::min(vx, 32 - vx),
																		std::min(vy, 32 - vy)));
									}
								if (distance > s.radius)
									continue;
								fertilityBuff = std::max(fertilityBuff, s.fertilityBuffBps);
								fertilityWeak = std::max(fertilityWeak, s.fertilityWeaknessBps);
								if ((emitter->owner->allies | (1u << emitter->owner->teamNumber)) &
									(1u << t))
								{
									heal = std::max(heal, s.healingQ8);
									feed = std::max(feed, s.feedingQ8);
									attack = std::max(attack, s.attackBuffBps);
									armor = std::max(armor, s.armorBuffBps);
								}
								if (emitter->owner->attackableTeams() & (1u << t))
								{
									damage = std::max(damage, s.damageQ8);
									attackWeak = std::max(attackWeak, s.attackWeaknessBps);
									armorWeak = std::max(armorWeak, s.armorWeaknessBps);
								}
							}
						CHECK(value(world.game, Healing, x, y, t) == heal);
						CHECK(value(world.game, Damage, x, y, t) == damage);
						CHECK(value(world.game, Feeding, x, y, t) == feed);
						CHECK(value(world.game, UnitAttack, x, y, t) ==
							  10000 + attack - attackWeak);
						CHECK(value(world.game, UnitArmor, x, y, t) == 10000 + armor - armorWeak);
						CHECK(value(world.game, BuildingAttack, x, y, t) == 10000 + attack);
						CHECK(value(world.game, BuildingArmor, x, y, t) == 10000 + armor);
						CHECK(world.game.areaEffects
								  .fertilityValues()[world.game.map.coordToIndex(x, y)] ==
							  10000 + fertilityBuff - fertilityWeak);
					}
		};
		compare();
		const auto visits = world.game.areaEffects.metrics.emitterVisits;
		refresh(world, 1);
		CHECK(world.game.areaEffects.metrics.emitterVisits == visits);
		CHECK(value(world.game, UnitAttack, 31, 31, 2) == 6000);
		CHECK(value(world.game, UnitArmor, 31, 31, 2) == 4000);
		CHECK(world.game.areaEffects.fertilityValues()[world.game.map.coordToIndex(31, 31)] ==
			  18000);
		b->kill();
		refresh(world, 2);
		compare();
		// Removing the strongest weakness restores the remaining weaker source.
		CHECK(value(world.game, UnitAttack, 31, 31, 2) == 9000);
		CHECK(value(world.game, UnitArmor, 31, 31, 2) == 8000);
		CHECK(world.game.areaEffects.fertilityValues()[world.game.map.coordToIndex(31, 31)] ==
			  14000);
		a->posX = 15;
		a->posY = 18;
		world.game.areaEffects.changed(a->gid);
		refresh(world, 3);
		compare();
		world.game.teams[0]->allies = 5;
		world.game.teams[0]->enemies = 2;
		refresh(world, 4);
		compare();
		world.game.gameHeader.setPeacefulModeEnabled(true);
		refresh(world, 5);
		compare();
	}
	TEST_CASE("upkeep is atomic pulses accumulate fractions and cap services")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
		install(world.game, {{"radius", 5},
							 {"healingQ8", 128},
							 {"damageQ8", 128},
							 {"feedingQ8", 128},
							 {"cost", {{"food", 1}, {"wood", 1}}}});
		auto *b = world.addBuilding("stonewall", 4, 4);
		auto *overlapping = world.addBuilding("stonewall", 6, 4);
		overlapping->materials[WHEAT] = 4;
		overlapping->materials[WOOD] = 4;
		auto *u = world.addUnit(WORKER, 7, 7);
		u->hp -= 5;
		u->hungry -= 5;
		auto *enemy = world.addUnit(WORKER, 7, 8, 1);
		const int enemyHp = enemy->hp;
		b->materials[WHEAT] = 4;
		b->materials[WOOD] = 0;
		refresh(world, 0);
		CHECK_FALSE(b->areaFunded);
		CHECK(b->materials[WHEAT] == 4);
		CHECK(overlapping->areaFunded);
		CHECK(overlapping->materials[WHEAT] == 3);
		CHECK(overlapping->materials[WOOD] == 3);
		b->materials[WOOD] = 4;
		refresh(world, 1);
		CHECK_FALSE(b->areaFunded);
		refresh(world, 16);
		REQUIRE(b->areaFunded);
		CHECK(b->materials[WOOD] == 3);
		CHECK(b->materials[WHEAT] == 3);
		CHECK(overlapping->materials[WHEAT] == 2);
		CHECK(overlapping->materials[WOOD] == 2);
		const int hp = u->hp, hunger = u->hungry;
		u->applyAreaServices();
		CHECK(u->hp == hp);
		CHECK(u->hungry == hunger);
		enemy->applyAreaServices();
		CHECK(enemy->hp == enemyHp);
		CHECK(enemy->areaServiceRemainders[Damage] == 128);
		refresh(world, 17);
		u->applyAreaServices();
		CHECK(u->areaServiceRemainders[Healing] == 128);
		refresh(world, 32);
		u->applyAreaServices();
		CHECK(u->hp == hp + 1);
		CHECK(u->hungry == hunger + 1);
		enemy->applyAreaServices();
		CHECK(enemy->hp == enemyHp - 1);
		CHECK(enemy->areaServiceRemainders[Damage] == 0);
		u->applyAreaServices();
		CHECK(u->areaServiceRemainders[Healing] == 0);
		const int food = b->materials[WHEAT];
		refresh(world, 32);
		CHECK(b->materials[WHEAT] == food);
		u->hp = u->performance[HP];
		u->hungry = Unit::HUNGRY_MAX;
		refresh(world, 48);
		u->applyAreaServices();
		CHECK(u->areaServiceRemainders[Healing] == 0);
		CHECK(u->areaServiceRemainders[Feeding] == 0);
		u->hp -= 2;
		u->displacement = Unit::DIS_INSIDE;
		refresh(world, 64);
		u->applyAreaServices();
		CHECK(u->areaServiceRemainders[Healing] == 0);
		b->materials[WOOD] = 0;
		refresh(world, 80);
		CHECK_FALSE(b->areaFunded);
		CHECK(value(world.game, Healing, 7, 7) == 0);
	}
	TEST_CASE("combat uses live coverage and magic preserves armor bypass")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
		install(world.game, {{"radius", 4},
							 {"attackBuffBps", 5000},
							 {"armorBuffBps", 10000},
							 {"attackWeaknessBps", 2000},
							 {"armorWeaknessBps", 5000}});
		auto *b = world.addBuilding("stonewall", 4, 4);
		auto *u = world.addUnit(WARRIOR, 6, 6, 0, 1);
		auto *enemy = world.addUnit(WARRIOR, 7, 7, 1, 1);
		const int attack = u->getRealAttackStrength(), armor = u->getRealArmor(false),
				  eattack = enemy->getRealAttackStrength(), earmor = enemy->getRealArmor(false);
		refresh(world, 0);
		CHECK(u->getRealAttackStrength() == attack * 15000 / 10000);
		CHECK(u->getRealArmor(false) == armor * 2);
		CHECK(enemy->getRealAttackStrength() == eattack * 8000 / 10000);
		CHECK(enemy->getRealArmor(false) == earmor / 2);
		CHECK(enemy->getRealArmor(true) == 0);
		CHECK(b->getEffectiveArmor() == b->type->armor * 2);
		u->posX = 20;
		u->posY = 20;
		CHECK(u->getRealAttackStrength() == attack);
		u->posX = 6;
		u->posY = 6;
		u->displacement = Unit::DIS_ENTERING_BUILDING;
		CHECK(u->getRealAttackStrength() == attack);
	}
	TEST_CASE("even footprints receive combat modifiers at the northwestern center")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		install(world.game, {{"radius", 1}, {"attackBuffBps", 10000}, {"armorBuffBps", 10000}});
		auto catalog = Json::parse(world.game.buildingsTypes.snapshotJson());
		auto &inn = catalog["variants"][world.game.buildingsTypes.getTypeNum("inn", 0, false)];
		inn["properties"]["width"] = 2;
		inn["properties"]["height"] = 2;
		inn["properties"]["armor"] = 3;
		world.game.buildingsTypes.loadSnapshotJson(catalog.dump());
		world.game.configureBuildingCatalog();
		world.addBuilding("stonewall", 4, 5);
		auto *recipient = world.addBuilding("inn", 5, 5);
		refresh(world, 0);
		CHECK(value(world.game, BuildingAttack, 5, 5) == 20000);
		CHECK(value(world.game, BuildingAttack, 6, 6) == Neutral);
		CHECK(recipient->applyAreaAttack(7) == 14);
		CHECK(recipient->getEffectiveArmor() == 6);
	}
	TEST_CASE("ordinary building services discard positive aura fractions at their caps")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		install(world.game, {{"healingQ8", 128}, {"feedingQ8", 128}});
		auto catalog = Json::parse(world.game.buildingsTypes.snapshotJson());
		auto &inn = catalog["variants"][world.game.buildingsTypes.getTypeNum("inn", 0, false)];
		inn["semantics"]["healing"] = {
			{"enabled", true}, {"unitMask", 1}, {"duration", 4}, {"cost", Json::object()}};
		world.game.buildingsTypes.loadSnapshotJson(catalog.dump());
		world.game.configureBuildingCatalog();
		auto *b = world.addBuilding("inn", 12, 12);
		b->materials[WHEAT] = 10;
		for (int purpose : {int(FEED), int(HEAL)})
		{
			auto *u = world.addUnit(WORKER, 6 + purpose % 2, 6);
			u->hp -= 2;
			u->hungry -= 2;
			u->areaServiceRemainders[Healing] = 128;
			u->areaServiceRemainders[Feeding] = 128;
			u->destinationPurpose = purpose;
			REQUIRE(b->canOfferService(u, purpose));
			b->subscribeUnitForInside(u);
			world.game.map.setGroundUnit(u->posX, u->posY, NOGUID);
			u->posX = b->getMidX();
			u->posY = b->getMidY();
			u->displacement = Unit::DIS_INSIDE;
			u->insideTimeout = 0;
			u->delta = 255;
			u->speed = 1;
			u->syncStep();
			if (purpose == FEED)
			{
				CHECK(u->hungry == Unit::HUNGRY_MAX);
				CHECK(u->areaServiceRemainders[Feeding] == 0);
			}
			else
			{
				CHECK(u->hp == u->performance[HP]);
				CHECK(u->areaServiceRemainders[Healing] == 0);
			}
		}
	}
	TEST_CASE("damage resolves death before healing and respects no permadeath")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
		install(world.game, {{"radius", 5}, {"healingQ8", 4096}, {"damageQ8", 4096}});
		world.addBuilding("stonewall", 4, 4, 0, 0);
		world.addBuilding("stonewall", 9, 4, 0, 1);
		auto *u = world.addUnit(WORKER, 7, 7);
		u->hp = 1;
		refresh(world, 0);
		u->applyAreaServices();
		CHECK(u->isDead);
		auto *survivor = world.addUnit(WORKER, 7, 8);
		survivor->hp = 1;
		world.game.gameHeader.setPermadeathDisabled(true);
		refresh(world, 16);
		survivor->applyAreaServices();
		CHECK_FALSE(survivor->isDead);
	}
	TEST_CASE("turret snapshots attack at firing and reads target armor at impact")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
		install(world.game, {{"radius", 5}, {"attackBuffBps", 10000}, {"armorBuffBps", 10000}});
		auto catalog = Json::parse(world.game.buildingsTypes.snapshotJson());
		auto &gun = catalog["variants"][world.game.buildingsTypes.getTypeNum("inn", 0, false)];
		gun["properties"]["width"] = 3;
		gun["properties"]["height"] = 1;
		gun["properties"]["shootingRange"] = 5;
		gun["properties"]["shootSpeed"] = 1024;
		gun["properties"]["shootRhythm"] = 65535;
		gun["properties"]["maxBullets"] = 10;
		gun["properties"]["multiplierStoneToBullets"] = 2;
		gun["semantics"]["projectileDamage"] = {0, 9, 17};
		gun["semantics"]["projectileBuildingDamage"] = 0;
		world.game.buildingsTypes.loadSnapshotJson(catalog.dump());
		world.game.configureBuildingCatalog();
		auto *source = world.addBuilding("stonewall", 6, 8);
		auto *defense = world.addBuilding("stonewall", 16, 8, 0, 1);
		auto *turret = world.addBuilding("inn", 8, 8);
		turret->bullets = 10;
		auto *target = world.addUnit(EXPLORER, 14, 9, 1);
		target->speed = 1;
		target->delta = 0;
		refresh(world, 0);
		turret->turretStep(0);
		turret->turretStep(1);
		turret->turretStep(2);
		auto *sector = world.game.map.getSector(turret->getMidX(), turret->getMidY());
		REQUIRE(!sector->bullets.empty());
		auto *shot = sector->bullets.front();
		CHECK(shot->unitDamage[EXPLORER] == 18);
		source->kill();
		defense->kill();
		refresh(world, 1);
		const int before = target->hp, damage = std::max(1, 18 - target->getRealArmor(false));
		shot->ticksLeft = 0;
		sector->step();
		CHECK(target->hp == before - damage);
	}
	TEST_CASE("magic and flying services use coverage without fruit bonuses")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
		install(world.game,
				{{"radius", 8}, {"attackBuffBps", 10000}, {"healingQ8", 256}, {"feedingQ8", 256}});
		world.addBuilding("stonewall", 4, 4);
		auto *caster = world.addUnit(EXPLORER, 8, 8, 0, 1);
		REQUIRE(caster->performance[FLY] > 0);
		auto *target = world.addUnit(WARRIOR, 9, 8, 1);
		caster->performance[MAGIC_ATTACK_GROUND] = 3;
		caster->performance[MAGIC_ATTACK_AIR] = 0;
		caster->magicActionTimeout = 0;
		caster->hp -= 2;
		caster->hungry -= 2;
		refresh(world, 0);
		const int hp = caster->hp, hunger = caster->hungry, fruits = caster->fruitCount;
		caster->applyAreaServices();
		CHECK(caster->hp == hp + 1);
		CHECK(caster->hungry == hunger + 1);
		CHECK(caster->fruitCount == fruits);
		const int before = target->hp;
		caster->hp = caster->performance[HP];
		caster->hungry = Unit::HUNGRY_MAX;
		caster->hungriness = 0;
		caster->medical = Unit::MED_FREE;
		caster->needToRecheckMedical = false;
		caster->delta = 255;
		caster->speed = 1;
		caster->syncStep();
		CHECK(target->hp == before - 6 + target->getRealArmor(true));
		auto *pendingDeath = world.addUnit(WORKER, 7, 8);
		pendingDeath->hp = UNIT_HP_DEATH_THRESHOLD - 1;
		pendingDeath->applyAreaServices();
		CHECK(pendingDeath->isDead);
	}
	TEST_CASE("fertility snapshots retain their captured modifiers and only land changes")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		auto &map = world.game.map;
		for (int y = 0; y < 32; ++y)
		{
			map.setVertexTerrain(0, y, WATER);
			map.setVertexTerrain(1, y, WATER);
		}
		const auto tile = map.coordToIndex(3, 8);
		const auto rate = map.resourceGrowthRateAt(tile, WHEAT);
		const auto aquatic = map.resourceGrowthRateAt(map.coordToIndex(0, 8), ALGA);
		REQUIRE(rate > 0);
		install(world.game, {{"radius", 65535}, {"fertilityBuffBps", 10000}});
		auto *b = world.addBuilding("stonewall", 4, 8);
		refresh(world, 0);
		CHECK(map.resourceGrowthRateAt(tile, WHEAT) ==
			  std::min(rate * 2, 4u * Fertility::kRateScale));
		CHECK(map.resourceGrowthRateAt(map.coordToIndex(16, 8), WHEAT) == 0);
		CHECK(map.resourceGrowthRateAt(map.coordToIndex(0, 8), ALGA) == aquatic);
		for (int y = 0; y < 32; ++y)
			map.setResourceByIndex(3, y, WHEAT, 3);
		SimulationSnapshot::Storage storage;
		auto snapshot = SimulationSnapshot::capture(
			world.game, SimulationSnapshot::captureCatalog(world.game),
			ResourceGrowth::Pipeline::requirements(), nullptr, &storage);
		CHECK(snapshot.areaFertility->values[tile] == 20000);
		const auto fertilityGeneration = world.game.areaEffects.fertilityGeneration();
		world.team->allies ^= 2;
		refresh(world, 1);
		CHECK(world.game.areaEffects.fertilityGeneration() == fertilityGeneration);
		auto diplomacy = SimulationSnapshot::capture(world.game, snapshot.catalogs->buildings,
													 snapshot.requirements, &snapshot, &storage);
		CHECK(diplomacy.areaFertility == snapshot.areaFertility);
		ResourceGrowth::Batch before, after;
		MersenneTwister first(991), second(991);
		ResourceGrowth::calculate(snapshot.view(), first, before);
		b->kill();
		refresh(world, 2);
		CHECK(map.resourceGrowthRateAt(tile, WHEAT) == rate);
		CHECK(snapshot.areaFertility->values[tile] == 20000);
		ResourceGrowth::calculate(snapshot.view(), second, after);
		REQUIRE(before.proposals.size() == after.proposals.size());
		for (std::size_t i = 0; i < before.proposals.size(); ++i)
		{
			const auto &a = before.proposals[i];
			const auto &b = after.proposals[i];
			CHECK(
				std::tie(a.tile, a.type, a.material, a.delta, a.variety, a.kind, a.incrementMask) ==
				std::tie(b.tile, b.type, b.material, b.delta, b.variety, b.kind, b.incrementMask));
		}
		auto next = SimulationSnapshot::capture(world.game, snapshot.catalogs->buildings,
												snapshot.requirements, &snapshot, &storage);
		CHECK(next.growth == snapshot.growth);
		CHECK(next.areaFertility->values[tile] == 10000);
		auto unchanged = SimulationSnapshot::capture(world.game, next.catalogs->buildings,
													 next.requirements, &next, &storage);
		CHECK(unchanged.areaFertility == next.areaFertility);
		FertilitySnapshot copied;
		Uint64 bytes = 0;
		world.game.areaEffects.captureFertility(copied, bytes);
		REQUIRE(bytes > 0);
		bytes = 0;
		world.game.areaEffects.captureFertility(copied, bytes);
		CHECK(bytes == 0);
	}
	TEST_CASE("immediate growth obeys temporary land suppression and resumes afterward")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true, .seed = 713});
		auto &map = world.game.map;
		std::array<Uint8, 32> initial{};
		for (int y = 0; y < 32; ++y)
		{
			map.setVertexTerrain(0, y, WATER);
			map.setVertexTerrain(1, y, WATER);
			map.setResourceByIndex(3, y, WHEAT, 0);
			REQUIRE(map.getResource(3, y).type == WHEAT);
			initial[y] = map.getResource(3, y).amount;
		}
		install(world.game, {{"radius", 65535}, {"fertilityWeaknessBps", 10000}});
		auto *source = world.addBuilding("stonewall", 8, 8);
		refresh(world, 0);
		CHECK(map.resourceGrowthRateAt(map.coordToIndex(3, 8), WHEAT) == 0);
		for (int n = 0; n < 100; ++n)
			map.growResources();
		for (int y = 0; y < 32; ++y)
			CHECK(map.getResource(3, y).amount == initial[y]);
		source->kill();
		refresh(world, 1);
		REQUIRE(map.resourceGrowthRateAt(map.coordToIndex(3, 8), WHEAT) > 0);
		bool changed = false;
		for (int n = 0; n < 1024 && !changed; ++n)
		{
			map.growResources();
			for (int y = 0; y < 32; ++y)
				changed |= map.getResource(3, y).amount != initial[y];
		}
		// Legacy growth oscillates stock at its cap, so recovery need not
		// increase the final sum. It must resume actual growth mutations.
		CHECK(changed);
	}
	TEST_CASE("map replacement discards old dense planes before new tile queries")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		install(world.game, {{"radius", 4}, {"healingQ8", 256}});
		auto *b = world.addBuilding("stonewall", 4, 4);
		refresh(world, 0);
		REQUIRE(world.game.areaEffects.fieldBytes() > 0);
		world.game.map.setSize(6, 6, GRASS);
		CHECK(world.game.areaEffects.fieldBytes() == 0);
		CHECK(value(world.game, Healing, 40, 40) == 0);
		world.game.map.setBuilding(4, 4, b->type->width, b->type->height, b->gid);
		refresh(world, 1);
		CHECK(value(world.game, Healing, 5, 5) == 256);
	}
	TEST_CASE("ownership changes invalidate paid coverage and wait for the next pulse")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true});
		install(world.game, {{"radius", 4}, {"healingQ8", 256}, {"cost", {{"food", 1}}}});
		auto *b = world.addBuilding("stonewall", 4, 4);
		b->materials[WHEAT] = 4;
		refresh(world, 0);
		REQUIRE(b->areaFunded);
		CHECK(b->materials[WHEAT] == 3);
		const auto previous = b->gid;
		auto *old = b->owner;
		auto *next = world.game.teams[1];
		const int id = Building::GIDtoID(previous);
		old->removeFromAbilitiesLists(b);
		old->myBuildings[id] = nullptr;
		old->detachBuilding(id);
		b->owner = next;
		b->gid = Building::GIDfrom(id, 1);
		next->myBuildings[id] = b;
		next->attachBuilding(id);
		next->addToStaticAbilitiesLists(b);
		world.game.map.setBuilding(4, 4, b->type->width, b->type->height, b->gid);
		world.game.areaEffects.changed(previous);
		world.game.areaEffects.changed(b->gid);
		CHECK(value(world.game, Healing, 5, 5, 0) ==
			  256); // published coverage is fixed for the tick
		refresh(world, 1);
		CHECK_FALSE(b->areaFunded);
		CHECK(b->materials[WHEAT] == 3);
		CHECK(value(world.game, Healing, 5, 5, 0) == 0);
		CHECK(value(world.game, Healing, 5, 5, 1) == 0);
		refresh(world, 16);
		REQUIRE(b->areaFunded);
		CHECK(b->materials[WHEAT] == 2);
		CHECK(value(world.game, Healing, 5, 5, 0) == 0);
		CHECK(value(world.game, Healing, 5, 5, 1) == 256);
	}

	TEST_CASE("construction deletion and repair retain the intended funding boundaries")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		install(world.game, Json::object(),
				{{"radius", 4}, {"healingQ8", 256}, {"cost", {{"food", 1}}}});
		auto *b = world.addBuilding("inn", 4, 4);
		b->materials[WHEAT] = 10;
		const int completed = b->typeNum, site = b->type->prevLevel;
		REQUIRE(site >= 0);
		refresh(world, 0);
		REQUIRE(b->areaFunded);
		const int stock = b->materials[WHEAT];
		b->constructionOriginTypeNum = completed;
		b->constructionResultState = Building::REPAIR;
		b->bindType(site);
		refresh(world, 1);
		CHECK(b->areaFunded);
		CHECK(value(world.game, Healing, 5, 5) == 256);
		CHECK(b->materials[WHEAT] == stock);
		b->launchDelete();
		refresh(world, 2);
		CHECK_FALSE(b->areaFunded);
		CHECK(value(world.game, Healing, 5, 5) == 0);
		b->cancelDelete();
		refresh(world, 3);
		CHECK_FALSE(b->areaFunded);
		b->bindType(completed);
		b->constructionOriginTypeNum = -1;
		b->constructionResultState = Building::NO_CONSTRUCTION;
		refresh(world, 4);
		CHECK_FALSE(b->areaFunded);
		refresh(world, 16);
		REQUIRE(b->areaFunded);
		b->launchDelete();
		refresh(world, 17);
		CHECK_FALSE(b->areaFunded);
		CHECK(value(world.game, Healing, 5, 5) == 0);
		b->cancelDelete();
		refresh(world, 18);
		CHECK_FALSE(b->areaFunded);
		refresh(world, 32);
		REQUIRE(b->areaFunded);
		REQUIRE(b->launchConstruction(1, 1));
		refresh(world, 33);
		CHECK_FALSE(b->areaFunded);
		CHECK(value(world.game, Healing, 5, 5) == 0);
	}

	TEST_CASE(
		"saved funding and fractions reject values before compact storage narrowing [save-format]")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		install(world.game, {{"radius", 4}, {"healingQ8", 128}});
		world.addBuilding("stonewall", 4, 4);
		world.addUnit(WORKER, 6, 6);
		struct LocatedOutput : GAGCore::BinaryOutputStream
		{
			std::size_t team = 0, fraction = 0;
			explicit LocatedOutput(GAGCore::StreamBackend *backend) : BinaryOutputStream(backend) {}
			void writeSint32(Sint32 value, const std::string name) override
			{
				if (name == "areaFundingTeam")
					team = getPosition();
				BinaryOutputStream::writeSint32(value, name);
			}
			void writeUint16(Uint16 value, const std::string name) override
			{
				if (name == "areaServiceRemainder0")
					fraction = getPosition();
				BinaryOutputStream::writeUint16(value, name);
			}
		};
		auto *memory = new GAGCore::MemoryStreamBackend;
		LocatedOutput output(memory);
		world.game.save(&output, false, "invalid area state");
		output.flush();
		REQUIRE(output.team > 0);
		REQUIRE(output.fraction > 0);
		const std::string original = memory->takeContents();
		for (bool funding : {false, true})
		{
			auto *corrupt = new GAGCore::MemoryStreamBackend(std::string(original));
			GAGCore::BinaryOutputStream patch(corrupt);
			patch.seekFromStart(funding ? output.team : output.fraction);
			if (funding)
				patch.writeSint32(256, "invalid team");
			else
				patch.writeUint16(256, "invalid fraction");
			auto *copy = new GAGCore::MemoryStreamBackend(*corrupt);
			copy->seekFromStart(0);
			GAGCore::BinaryInputStream input(copy);
			GameGUI restored(false);
			CHECK_THROWS_AS(restored.game.load(&input), std::runtime_error);
		}
	}

	TEST_CASE(
		"paid intervals and fractions preserve full save continuation [save-format][artifacts]")
	{
		glob2test::HeadlessGlobals globals;
		for (int checkpoint : {1, 7, 15, 16, 17, 31, 32})
		{
			glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true, .seed = 713});
			install(world.game, {{"radius", 8},
								 {"healingQ8", 128},
								 {"feedingQ8", 128},
								 {"attackBuffBps", 2000},
								 {"fertilityBuffBps", 1000},
								 {"cost", {{"food", 1}}}});
			auto *b = world.addBuilding("stonewall", 4, 4);
			b->materials[WHEAT] = 32;
			auto *u = world.addUnit(WORKER, 6, 6);
			u->hp -= 10;
			u->hungry -= 100;
			for (int y = 0; y < 32; ++y)
			{
				world.game.map.setVertexTerrain(0, y, WATER);
				world.game.map.setVertexTerrain(1, y, WATER);
				world.game.map.setResourceByIndex(3, y, WHEAT, 3);
			}
			world.team->createLists();
			world.game.setWaitingOnMask(0);
			world.step(checkpoint);
			REQUIRE(b->areaFunded);
			REQUIRE(u->areaServiceRemainders[Healing] == (checkpoint <= 16 ? 128 : 0));
			auto *memory = new GAGCore::MemoryStreamBackend;
			GAGCore::BinaryOutputStream output(memory);
			world.game.save(&output, false, "area effects");
			output.flush();
			const auto bytes = memory->takeContents();
			const auto directory = glob2test::artifactDir() / std::to_string(checkpoint);
			std::filesystem::create_directories(directory);
			std::ofstream saved(directory / "between-pulses.game", std::ios::binary);
			saved.write(bytes.data(), bytes.size());
			saved.close();
			std::vector<std::vector<Uint32>> expected;
			for (int i = 0; i < 48; ++i)
			{
				world.step();
				expected.push_back(components(world.game));
			}
			GameGUI restored(false);
			GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(std::string(bytes)));
			REQUIRE(restored.game.load(&input));
			restored.game.setWaitingOnMask(0);
			restored.game.map.configureCompute(4);
			std::ofstream trace(directory / "continuation.csv");
			trace << "tick,original_components_hash,loaded_components_hash\n";
			const auto digest = [](const auto &values)
			{
				Uint32 hash = 2166136261u;
				for (auto v : values)
					hash = (hash ^ v) * 16777619u;
				return hash;
			};
			for (int i = 0; i < 48; ++i)
			{
				restored.game.syncStep(0);
				const auto actual = components(restored.game);
				CHECK(actual == expected[i]);
				trace << restored.game.stepCounter << ',' << digest(expected[i]) << ','
					  << digest(actual) << '\n';
			}
		}
	}
}
