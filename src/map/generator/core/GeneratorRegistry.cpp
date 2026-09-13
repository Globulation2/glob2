// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneratorRegistry.h"
#include "CityStatesGenerator.h"
#include "ConcreteIslandsGenerator.h"
#include "ContestedCommonsGenerator.h"
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
#include <algorithm>
#include <set>
#include <stdexcept>
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
	// This is also the product-facing catalog order. Keep the most polished and
	// distinctive playable maps first; numeric IDs remain stable compatibility
	// identifiers and do not determine presentation order.
	static const GeneratorRegistry registry(
		{contestedCommonsDefinition(), mazeDefinition(), fjordContinentDefinition(),
		 shatteredCoastDefinition(), islesDefinition(), watershedDefinition(),
		 stoneHighlandsDefinition(), symmetricArenaDefinition(), ringWorldDefinition(),
		 cityStatesDefinition(), tidalFlatsDefinition(), evergladesDefinition(), spiderWebDefinition(),
		 coralDefinition(), ruggedArchipelagoDefinition(), concreteIslandsDefinition(),
		 craterLakesDefinition(), islandsDefinition(), swampDefinition(), riverDefinition(),
		 uniformDefinition()});
	return registry;
}
