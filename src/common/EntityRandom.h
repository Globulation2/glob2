// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <stdexcept>

/// Private simulation stream. PCG32 XSH-RR, based on Melissa E. O'Neill's
/// reference implementation (copyright 2014 Melissa E. O'Neill),
/// https://www.pcg-random.org/download.html. This implementation adds entity
/// seeding and persistence; it does not embed the upstream library.
/// Fixed-width arithmetic and sampling at callers define the portable sequence.
class EntityRandom
{
public:
    enum class Kind : std::uint64_t { Unit = 0, Building = 1 };
    struct State
    {
        std::uint64_t value;
        std::uint64_t increment;
        bool operator==(const State&) const = default;
    };

    void initialize(std::uint32_t gameSeed, Kind kind, std::uint16_t gid, std::uint32_t generation)
    {
        if (!generation) throw std::runtime_error("Invalid entity RNG generation");
        const std::uint64_t identity = (std::uint64_t(kind) << 48) |
            (std::uint64_t(generation) << 16) | gid;
        seed(mix(std::uint64_t(gameSeed) ^ identity), identity);
    }

    // Owner domains are a separate identity namespace from units/buildings.
    // Keep the domain numbers stable: they are part of the save/simulation format.
    void initializeOwner(std::uint32_t seedValue, std::uint16_t domain, std::uint32_t identity = 0)
    {
        if (domain < 2 || domain >= 0x8000) throw std::runtime_error("Invalid owner RNG domain");
        const std::uint64_t selector = (std::uint64_t(domain) << 48) | identity;
        seed(mix(std::uint64_t(seedValue) ^ selector), selector);
    }

    /// PCG reference seeding protocol; exposed for portable reference vectors.
    void seed(std::uint64_t initialState, std::uint64_t streamSelector)
    {
        state = {0, (streamSelector << 1) | 1};
        nextU32();
        state.value += initialState;
        nextU32();
    }

    std::uint32_t nextU32()
    {
        const std::uint64_t old = state.value;
        state.value = old * 6364136223846793005ULL + state.increment;
        const std::uint32_t shifted = std::uint32_t(((old >> 18) ^ old) >> 27);
        const std::uint32_t rotation = std::uint32_t(old >> 59);
        return (shifted >> rotation) | (shifted << ((0u - rotation) & 31));
    }

    State exportState() const { return state; }
    void importState(State saved)
    {
        if (!(saved.increment & 1)) throw std::runtime_error("Invalid entity RNG increment");
        state = saved;
    }
    bool operator==(const EntityRandom&) const = default;

    std::uint32_t checksum() const
    {
        const auto hash = mix(state.value ^ mix(state.increment));
        return std::uint32_t(hash) ^ std::uint32_t(hash >> 32);
    }

private:
    // SplitMix64's fixed increment and finalizer. This is a stateless seed mixer.
    static std::uint64_t mix(std::uint64_t value)
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }
    State state{0, 1}; // Constructors/loaders explicitly initialize before use.
};
