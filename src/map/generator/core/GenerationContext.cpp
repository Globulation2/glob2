// SPDX-License-Identifier: GPL-3.0-or-later
#include "GenerationContext.h"
#include <stdexcept>
std::uint32_t GenerationContext::deriveSeed(std::uint32_t seed, const std::string &name)
{
	// FNV-1a followed by an unsigned avalanche; independent of std::hash/platform.
	std::uint32_t h = 2166136261u ^ seed;
	for (unsigned char c : name)
	{
		h ^= c;
		h *= 16777619u;
	}
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return h;
}
std::mt19937 &GenerationContext::stream(const std::string &name)
{
	return streams.try_emplace(name, deriveSeed(request.seed, name)).first->second;
}
std::uint32_t GenerationContext::randomSeed()
{
	return std::random_device{}();
}

std::uint32_t GenerationContext::bounded(const std::string &name, std::uint32_t bound)
{
	if (!bound)
		throw std::invalid_argument("Empty random choice");
	const std::uint32_t threshold = -bound % bound;
	auto &rng = stream(name);
	std::uint32_t value;
	do
	{
		value = rng();
	} while (value < threshold);
	return value % bound;
}
