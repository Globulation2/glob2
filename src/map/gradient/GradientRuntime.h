// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private Map-owned execution state. Keeping this behind a pointer in Map.h
// prevents queue, thread and scratch-storage details from entering Map's API.
#include "GradientPipeline.h"
#include "ResourceSeedCache.h"
#include "field/GradientWorkspace.h"

#include <vector>
#include <array>
#include <map>
#include <list>
#include "Team.h"
#include "Ressource.h"
#include <unordered_map>

struct GradientRuntime
{
	struct Workspace
	{
		GradientWorkspace propagation;
		struct Crowding
		{
			std::vector<std::uint16_t> warriors, paint, rows;
			std::vector<int> columnSums;
			std::vector<std::size_t> positions, seeds;
		} crowding;
	};
	struct MaterialField
	{
		std::unique_ptr<Uint16[]> cells;
		Uint64 sourceRevision=0, recency=0;
		Uint32 topology=0, builtStep=0, identity=0;
		Sint32 consumer=-1, type=-1, x=0, y=0;
		int team=0, resource=0, swim=0;
		unsigned modes=0;
		std::list<Uint64>::iterator lru;
	};
	std::map<Uint64,MaterialField> materialFields;
	std::list<Uint64> materialLru;
	std::array<std::array<Uint64,MaterialCount>,Team::MAX_COUNT> stockRevision{};
	Uint64 materialCacheClock=0, materialCacheBudget=64ull*1024*1024;
	std::vector<Workspace> workspaces{1};
	GradientPipeline pipeline;
	// One simulation-owned reservation, consumed once during the observation phase.
	// Scalar identities survive the scheduling barrier without borrowing stack lambdas.
	struct Preparation {
		enum class Kind { Materials, Markets, Guard, Clear };
		GradientPipeline::Job *job = nullptr;
		Kind kind = Kind::Materials;
		int team = 0, material = 0, swim = 0;
	} preparation;
	bool supplierLocationsDirty = true;
	std::unordered_map<std::size_t, std::vector<std::uint16_t>> overlaySupplierLocations;
	ResourceSeedCache resourceSeeds;
	// Shared inert field for materials with no natural or supplier source.
	std::vector<Uint16> absentMaterialField;
};
