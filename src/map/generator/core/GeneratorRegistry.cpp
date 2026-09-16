// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneratorRegistry.h"
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
#include "UniformGenerator.h"
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
#include "FortsGenerator.h"
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
#include "VulturesGenerator.h"
#include "PlantationsGenerator.h"
#include "SierpinskiGardensGenerator.h"
#include "HilbertRiverGenerator.h"
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
	for (const auto &d : definitions)
	{
		if (!d.id || !*d.id || !d.nameKey || d.legacyId < 0 || !d.generate ||
			!numbers.insert(d.legacyId).second || !ids.insert(d.id).second)
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
				c.normalize(c.defaultValue) != c.defaultValue || !controls.insert(c.id).second)
				throw std::invalid_argument("Invalid generator control");
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
	throw std::invalid_argument("Unknown generator ID");
}
int GeneratorRegistry::idOf(const std::string &id) const
{
	for (const auto &d : definitions)
		if (id == d.id)
			return d.legacyId;
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
	static const GeneratorRegistry registry({fingerprintDefinition(),
											 oldTownDefinition(),
											 symmetricArenaDefinition(),
											 swampDefinition(),
											 tidalFlatsDefinition(),
											 riverDefinition(),
											 islesDefinition(),
											 ringWorldDefinition(),
											 amphitheatreDefinition(),
											 shatteredCoastDefinition(),
											 craterLakesDefinition(),
											 fjordContinentDefinition(),
											 spiderWebDefinition(),
											 concreteIslandsDefinition(),
											 watershedDefinition(),
											 mazeDefinition(),
											 islandsDefinition(),
											 stoneHighlandsDefinition(),
											 switchbacksDefinition(),
											 cityStatesDefinition(),
											 canalsDefinition(),
											 sierpinskiGardensDefinition(),
											 hilbertRiverDefinition(),
											 ruggedArchipelagoDefinition(),
											 contestedCommonsDefinition(),
											 rainShadowDefinition(),
											 evergladesDefinition(),
											 polderDefinition(),
											 carouselDefinition(),
											 oldGrowthDefinition(),
											 anthillDefinition(),
											 coralDefinition(),
											 emojiDefinition(),
											 fortsDefinition(),
											 braidedDeltaDefinition(),
											 breachableHighlandsDefinition(),
											 hedgerowCountryDefinition(),
											 glacisDefinition(),
											 allotmentsDefinition(),
											 caravanseraiDefinition(),
											 braidedRiverDefinition(),
											 drumlinFieldDefinition(),
											 continentsDefinition(),
											 savannahDefinition(),
											 riceTerracesDefinition(),
											 vulturesDefinition(),
											 plantationsDefinition(),
											 uniformDefinition()});
	return registry;
}
