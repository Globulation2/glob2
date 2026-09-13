// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GeneratorControls.h"
#include "Ressource.h"
#include "TerrainType.h"
#include <array>
#include <cstdint>
#include <map>
#include <string>
class GeneratorRegistry;
struct GenerationRequest
{
	/// The numeric ids the fixed legacy descriptor (compatibility/) knows by name. Every other
	/// generator's id lives only in its GeneratorDefinition; look one up by its string id with
	/// GeneratorRegistry::idOf.
	enum Method : int
	{
		/// No terrain (terrain undefined)
		eNONE = -1,
		/// Uniform terrain (all of one type. completely unstructured)
		eUNIFORM = 0,
		/// swamp-like terrain with water here and land there
		eSWAMP = 1,
		/// a more or less winding river
		eRIVER = 2,
		/// islands that have organic shape and no passage from one to the next
		eISLANDS = 3,
		/// all connected land with round lakes
		eCRATERLAKES = 4,
		eCONCRETEISLANDS = 5,
		eISLES = 6,
		eOLDRANDOM = 7,
		eOLDISLANDS = 8
	};

	using Control = GeneratorControl;
	using ControlGroup = ::ControlGroup;
	GenerationRequest();
	int method = eUNIFORM; // stable registry ID, never a selection index
	int wDec = 0, hDec = 0, nbTeams = 0, nbWorkers = 0;
	TerrainType terrainType = GRASS;
	std::uint32_t seed = 0;
	std::map<std::string, int> options;
	// Legacy resource quantities remain supported by the compatibility adapter.
	std::array<int, MAX_NB_RESOURCES> resourceAmounts{};
	int option(const std::string &id) const { return options.at(id); }
	void setMethodDefaults(int method);
	void setMethodDefaults(int method, const GeneratorRegistry &registry);
	bool hasTerrainWeight() const;
	bool hasTerrainWeight(const std::vector<Control> &definitions) const;
	static const std::vector<Control> &controls(int method);
	static const std::vector<Control> &sharedControls() { return sharedGeneratorControls(); }
	static const Control &control(int method, const std::string &id);
	static const char *methodName(int method);
};
class GenerationHistory
{
	std::map<int, GenerationRequest> settings;

  public:
	void select(GenerationRequest &current, int method);
	void select(GenerationRequest &current, int method, const GeneratorRegistry &registry);
};
