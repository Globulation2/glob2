// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "Team.h"
#include <array>
#include <map>
#include <random>
#include <utility>
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
	/// Shuffles a range, one bounded draw per element from the back forward.
	template <typename It> void shuffle(It first, It last, const std::string &name)
	{
		for (auto n = last - first; n > 1; --n)
			std::swap(first[n - 1], first[bounded(name, std::uint32_t(n))]);
	}
	static std::uint32_t deriveSeed(std::uint32_t seed, const std::string &name);
	static std::uint32_t randomSeed();

  private:
	std::map<std::string, std::mt19937> streams;
};
