// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EntityRandom.h"
#include <array>

// Stable save/simulation identifiers. Units and buildings reserve domains 0 and 1.
// Adding an owner must not renumber an existing domain or consume another stream.
enum class RandomDomain : std::uint16_t {
    GrowthJobs = 2,
    ReferenceGrowth = 3,
    ResourceStocks = 4,
    ResourcePlacement = 5,
    ResourceSmoothing = 6,
    LegacyStory = 7,
    MatchMap = 8,
    MatchAI = 9,
    GenerationExercise = 10,
    GrowthSource = 11,
    Music = 12
};

class WorldRandomStreams
{
public:
    // This order is the on-disk worldRandom layout; append new owners only behind
    // a format boundary. Domain identifiers themselves must also remain stable.
    static constexpr std::array Domains{
        RandomDomain::GrowthJobs, RandomDomain::ReferenceGrowth,
        RandomDomain::ResourceStocks, RandomDomain::ResourcePlacement,
        RandomDomain::ResourceSmoothing
    };
    static constexpr unsigned Count = Domains.size();
    void initialize(std::uint32_t seed)
    {
        for (unsigned i = 0; i < Count; ++i)
            streams[i].initializeOwner(seed, unsigned(Domains[i]));
        initialized = true;
    }
    EntityRandom& get(std::uint32_t seed, RandomDomain domain)
    {
        const unsigned index = unsigned(domain) - unsigned(RandomDomain::GrowthJobs);
        if (index >= Count || Domains[index] != domain)
            throw std::logic_error("Not a world RNG domain");
        if (!initialized)
            initialize(seed);
        return streams[index];
    }
    // Serialized in domain order. Initialization is deferred for standalone maps;
    // restored states set initialized only after every stream has loaded successfully.
    std::array<EntityRandom, Count> streams;
    bool initialized = false;
};
