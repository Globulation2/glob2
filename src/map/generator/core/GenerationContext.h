// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "Team.h"
#include <array>
#include <map>
#include <random>
class Game;
struct GenerationContext
{
	const GenerationRequest &request;
	std::array<int, Team::MAX_COUNT> bootX{}, bootY{};
	std::string stage = "terrain";
	std::string detail;
	explicit GenerationContext(const GenerationRequest &r) : request(r) {}
	std::mt19937 &stream(const std::string &name);
	std::uint32_t bounded(const std::string &name, std::uint32_t bound);
	static std::uint32_t deriveSeed(std::uint32_t seed, const std::string &name);
	static std::uint32_t randomSeed();

  private:
	std::map<std::string, std::mt19937> streams;
};
