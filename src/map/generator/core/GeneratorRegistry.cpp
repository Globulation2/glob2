// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneratorRegistry.h"
#include <mutex>
#include "CompileProfile.h"
// compile_profile=core keeps only the generators buildsys/sources.py names in CORE_GENERATORS.
#if __has_include(<glob2/CompileProfile.h>)
#include <glob2/CompileProfile.h>
#else
#define GLOB2_PROFILE_HAS_GENERATORS 1
#endif
#include "EvenGroundGenerator.h"
#include "FortsGenerator.h"
#include "HillsGenerator.h"
#include "LavaShieldGenerator.h"
#include "UniformGenerator.h"
#if GLOB2_PROFILE_HAS_GENERATORS
#include "BastionKeysGenerator.h"
#include "CityStatesGenerator.h"
#include "ConcreteIslandsGenerator.h"
#include "ContestedCommonsGenerator.h"
#include "AmphitheatreGenerator.h"
#include "CarouselGenerator.h"
#include "SwitchbacksGenerator.h"
#include "CoralGenerator.h"
#include "CraterLakesGenerator.h"
#include "EvergladesGenerator.h"
#include "FjordContinentGenerator.h"
#include "IslandsGenerator.h"
#include "IslesGenerator.h"
#include "MazeGenerator.h"
#include "RingWorldGenerator.h"
#include "RiverGenerator.h"
#include "RuggedArchipelagoGenerator.h"
#include "ShatteredCoastGenerator.h"
#include "SpiderWebGenerator.h"
#include "StoneHighlandsGenerator.h"
#include "SwampGenerator.h"
#include "SymmetricArenaGenerator.h"
#include "TidalFlatsGenerator.h"
#include "WatershedGenerator.h"
#include "FingerprintGenerator.h"
#include "RainShadowGenerator.h"
#include "OldGrowthGenerator.h"
#include "CanalsGenerator.h"
#include "PolderGenerator.h"
#include "OldTownGenerator.h"
#include "AnthillGenerator.h"
#include "EmojiGenerator.h"
#include "BraidedRiverGenerator.h"
#include "BraidedDeltaGenerator.h"
#include "BreachableHighlandsGenerator.h"
#include "HedgerowCountryGenerator.h"
#include "GlacisGenerator.h"
#include "AllotmentsGenerator.h"
#include "CaravanseraiGenerator.h"
#include "DrumlinFieldGenerator.h"
#include "ContinentsGenerator.h"
#include "SavannahGenerator.h"
#include "RiceTerracesGenerator.h"
#include "LocustGenerator.h"
#include "PlantationsGenerator.h"
#include "SierpinskiGardensGenerator.h"
#include "HilbertRiverGenerator.h"
#include "HoneycombIsleGenerator.h"
#include "KarstTowersGenerator.h"
#include "BajadaGenerator.h"
#include "CentralQuarryGenerator.h"
#include "HiddenOasisGenerator.h"
#include "DrownedForestGenerator.h"
#include "PortageLakesGenerator.h"
#include "FaultedCityGenerator.h"
#include "MarchlandGenerator.h"
#include "OrchardCommonsGenerator.h"
#include "LastTreelineGenerator.h"
#include "GauntletGenerator.h"
#include "CombGenerator.h"
#include "EncircledKingdomGenerator.h"
#include "WhoAteTheMapGenerator.h"
#include "HungryMarchesGenerator.h"
#endif
#include <algorithm>
#include <set>
#include <stdexcept>
namespace
{
// A choice names each of its values 0, 1, 2... in order, shown as a dropdown rather than a number.
bool validChoice(const GeneratorControl &c)
{
	if (c.isToggle() || c.powerOfTwo || c.terrainWeight || c.minimum != 0 || c.step != 1 ||
		c.allowedValues.size() != c.valueLabels.size() ||
		c.maximum != int(c.valueLabels.size()) - 1)
		return false;
	for (size_t i = 0; i < c.valueLabels.size(); ++i)
		if (c.allowedValues[i] != int(i) || !c.valueLabels[i] || !*c.valueLabels[i])
			return false;
	return true;
}
} // namespace
GeneratorRegistry::GeneratorRegistry(std::vector<GeneratorDefinition> values)
	: definitions(std::move(values))
{
	std::set<int> numbers;
	std::set<std::string> ids;
	for (const auto &c : sharedGeneratorControls())
		if (!c.validSearchDomain())
			throw std::invalid_argument("Invalid shared generator search domain: " + c.id);
	for (const auto &d : definitions)
	{
		if (!d.id || !*d.id || !d.nameKey || d.legacyId < 0 || !d.generate ||
			!numbers.insert(d.legacyId).second || !ids.insert(d.id).second ||
			(d.tags.empty() && !d.editorOnly))
			throw std::invalid_argument("Invalid generator registration");
		std::set<std::string> controls;
		for (const auto &c : sharedGeneratorControls())
			controls.insert(c.id);
		for (const auto &c : d.controls)
			if (c.id.empty() || !c.label || c.step <= 0 || c.maximum < c.minimum ||
				(c.allowedValues.empty() && (std::int64_t(c.maximum) - c.minimum) % c.step) ||
				(!c.allowedValues.empty() &&
				 (c.allowedValues.front() != c.minimum || c.allowedValues.back() != c.maximum ||
				  !std::is_sorted(c.allowedValues.begin(), c.allowedValues.end()) ||
				  std::adjacent_find(c.allowedValues.begin(), c.allowedValues.end()) !=
					  c.allowedValues.end())) ||
				(c.powerOfTwo && (c.minimum < 0 || c.maximum > 30)) ||
				(c.isToggle() && (c.minimum != 0 || c.maximum != 1 || c.step != 1 || c.powerOfTwo ||
								  c.terrainWeight || !c.allowedValues.empty())) ||
				(c.isChoice() && !validChoice(c)) ||
				c.normalize(c.defaultValue) != c.defaultValue || !controls.insert(c.id).second ||
				!c.validSearchDomain())
				throw std::invalid_argument(std::string("Invalid generator control: ") + d.id +
											"/" + c.id);
	}
}
const GeneratorDefinition *GeneratorRegistry::find(int id) const
{
	for (const auto &d : definitions)
		if (d.legacyId == id)
			return &d;
	return nullptr;
}
const GeneratorDefinition &GeneratorRegistry::at(int id) const
{
	if (auto *d = find(id))
		return *d;
	if (!CompileProfile::hasGenerators())
		throw std::invalid_argument(CompileProfile::missing("Generator ID " + std::to_string(id)));
	throw std::invalid_argument("Unknown generator ID");
}
int GeneratorRegistry::idOf(const std::string &id) const
{
	for (const auto &d : definitions)
		if (id == d.id)
			return d.legacyId;
	if (!CompileProfile::hasGenerators())
		throw std::invalid_argument(CompileProfile::missing("Generator " + id));
	throw std::invalid_argument("Unknown generator: " + id);
}
std::vector<int> GeneratorRegistry::methods(bool editor) const
{
	std::vector<int> result;
	for (const auto &d : definitions)
		if (editor || !d.editorOnly)
			result.push_back(d.legacyId);
	return result;
}
int GeneratorRegistry::defaultMethod(bool editor) const
{
	const auto ids = methods(editor);
	if (ids.empty())
		throw std::invalid_argument("No available map generators");
	for (int id : ids)
		if (std::string(at(id).id) == "lava-shield")
			return id;
	return ids.front();
}
int GeneratorRegistry::selectionIndex(int id, bool editor) const
{
	auto ids = methods(editor);
	auto it = std::find(ids.begin(), ids.end(), id);
	return it == ids.end() ? -1 : int(it - ids.begin());
}
const GeneratorRegistry &GeneratorRegistry::builtins()
{
	// This is also the product-facing catalog order. It was shuffled once, on 2026-09-14
	// (std::mt19937-free: Python's random.Random(20260914).shuffle over the playable list as it
	// then stood), so browsing the lobby favours no landscape over another; Uniform, editor-only,
	// stays last. Numeric IDs remain stable compatibility identifiers and do not determine
	// presentation order. Add a new landscape wherever you like; do not reorder the rest.
	// The core compile profile drops the optional entries in place, so the order of the
	// rest is the same in both profiles.
#if GLOB2_PROFILE_HAS_GENERATORS
#define OPTIONAL_GENERATOR(definition) definition,
#else
#define OPTIONAL_GENERATOR(definition)
#endif
	static const GeneratorRegistry registry({OPTIONAL_GENERATOR(fingerprintDefinition())
											 OPTIONAL_GENERATOR(oldTownDefinition())
											 OPTIONAL_GENERATOR(symmetricArenaDefinition())
											 OPTIONAL_GENERATOR(swampDefinition())
											 OPTIONAL_GENERATOR(tidalFlatsDefinition())
											 OPTIONAL_GENERATOR(riverDefinition())
											 OPTIONAL_GENERATOR(islesDefinition())
											 OPTIONAL_GENERATOR(ringWorldDefinition())
											 OPTIONAL_GENERATOR(amphitheatreDefinition())
											 OPTIONAL_GENERATOR(shatteredCoastDefinition())
											 OPTIONAL_GENERATOR(craterLakesDefinition())
											 OPTIONAL_GENERATOR(fjordContinentDefinition())
											 OPTIONAL_GENERATOR(spiderWebDefinition())
											 OPTIONAL_GENERATOR(concreteIslandsDefinition())
											 OPTIONAL_GENERATOR(watershedDefinition())
											 OPTIONAL_GENERATOR(mazeDefinition())
											 OPTIONAL_GENERATOR(islandsDefinition())
											 OPTIONAL_GENERATOR(stoneHighlandsDefinition())
											 OPTIONAL_GENERATOR(switchbacksDefinition())
											 OPTIONAL_GENERATOR(cityStatesDefinition())
											 OPTIONAL_GENERATOR(canalsDefinition())
											 OPTIONAL_GENERATOR(sierpinskiGardensDefinition())
											 OPTIONAL_GENERATOR(hilbertRiverDefinition())
											 lavaShieldDefinition(),
											 OPTIONAL_GENERATOR(honeycombIsleDefinition())
											 OPTIONAL_GENERATOR(karstTowersDefinition()) OPTIONAL_GENERATOR(bajadaDefinition())
											 OPTIONAL_GENERATOR(centralQuarryDefinition()) OPTIONAL_GENERATOR(hiddenOasisDefinition()) OPTIONAL_GENERATOR(drownedForestDefinition()) OPTIONAL_GENERATOR(portageLakesDefinition())
											 OPTIONAL_GENERATOR(orchardCommonsDefinition()) OPTIONAL_GENERATOR(lastTreelineDefinition()) OPTIONAL_GENERATOR(gauntletDefinition()) OPTIONAL_GENERATOR(faultedCityDefinition()) OPTIONAL_GENERATOR(combDefinition()) OPTIONAL_GENERATOR(encircledKingdomDefinition()) OPTIONAL_GENERATOR(bastionKeysDefinition())
											 evenGroundDefinition(), OPTIONAL_GENERATOR(marchlandDefinition())
											 OPTIONAL_GENERATOR(whoAteTheMapDefinition()) OPTIONAL_GENERATOR(ruggedArchipelagoDefinition())
											 OPTIONAL_GENERATOR(hungryMarchesDefinition())
											 OPTIONAL_GENERATOR(contestedCommonsDefinition())
											 OPTIONAL_GENERATOR(rainShadowDefinition())
											 OPTIONAL_GENERATOR(evergladesDefinition())
											 OPTIONAL_GENERATOR(polderDefinition())
											 OPTIONAL_GENERATOR(carouselDefinition())
											 OPTIONAL_GENERATOR(oldGrowthDefinition())
											 OPTIONAL_GENERATOR(anthillDefinition())
											 OPTIONAL_GENERATOR(coralDefinition())
											 OPTIONAL_GENERATOR(emojiDefinition())
											 fortsDefinition(),
											 OPTIONAL_GENERATOR(braidedDeltaDefinition())
											 OPTIONAL_GENERATOR(breachableHighlandsDefinition())
											 OPTIONAL_GENERATOR(hedgerowCountryDefinition())
											 OPTIONAL_GENERATOR(glacisDefinition())
											 OPTIONAL_GENERATOR(allotmentsDefinition())
											 OPTIONAL_GENERATOR(caravanseraiDefinition())
											 OPTIONAL_GENERATOR(braidedRiverDefinition())
											 OPTIONAL_GENERATOR(drumlinFieldDefinition())
											 OPTIONAL_GENERATOR(continentsDefinition())
											 OPTIONAL_GENERATOR(savannahDefinition())
											 hillsDefinition(),
											 OPTIONAL_GENERATOR(riceTerracesDefinition())
											 OPTIONAL_GENERATOR(locustDefinition())
											 OPTIONAL_GENERATOR(plantationsDefinition())
											 uniformDefinition()});
#undef OPTIONAL_GENERATOR
	return registry;
}

namespace
{
std::mutex catalogMutex;
std::shared_ptr<const GeneratorRegistry> activeCatalog;

} // namespace
std::shared_ptr<const GeneratorRegistry> GeneratorRegistry::builtinsSnapshot()
{
	// Static native registrations require no ownership, but sharing their lifetime
	// through the same API avoids copying the entire catalog into every request.
	static const auto snapshot = std::shared_ptr<const GeneratorRegistry>(&builtins(), [](auto *) {});
	return snapshot;
}
std::shared_ptr<const GeneratorRegistry> GeneratorRegistry::activeSnapshot()
{
	std::lock_guard lock(catalogMutex);
	if (!activeCatalog)
		activeCatalog = builtinsSnapshot();
	return activeCatalog;
}
const GeneratorRegistry &GeneratorRegistry::active()
{
	// Short-lived reference convenience. Persistent consumers retain activeSnapshot().
    thread_local std::shared_ptr<const GeneratorRegistry> snapshot;
    snapshot=activeSnapshot();
    return *snapshot;
}
void GeneratorRegistry::publish(std::shared_ptr<const GeneratorRegistry> catalog)
{
	if (!catalog)
		throw std::invalid_argument("Missing generator catalog");
	std::lock_guard lock(catalogMutex);
	activeCatalog = std::move(catalog);
}
