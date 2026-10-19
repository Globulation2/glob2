// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnitConsts.h"
#include <compare>
#include <cstdint>

// Persistent logical identities. Values are wire-format IDs, independent of
// buffer addresses, request order and the backend used to calculate a field.
// These IDs also select deployed family models. Append new IDs without changing
// existing values; an unmeasured model ID uses the combined inference fallback.
enum class GradientFieldFamily : std::uint8_t {
    Materials=0, Markets=1, Guard=2, Clear=3, Forbidden=4
};
struct GradientFieldKey
{
    static constexpr std::uint8_t NoMaterial=255;
    GradientFieldFamily family=GradientFieldFamily::Materials;
    std::uint8_t team=0, swim=0, material=NoMaterial;
    auto operator<=>(const GradientFieldKey&) const = default;
    std::uint32_t packed() const {
        return (std::uint32_t(family)<<24)|(std::uint32_t(team)<<16)|(std::uint32_t(swim)<<8)|material;
    }
    static GradientFieldKey unpack(std::uint32_t value) {
        return {GradientFieldFamily(value>>24),std::uint8_t(value>>16),std::uint8_t(value>>8),std::uint8_t(value)};
    }
    bool valid(unsigned teams,unsigned movementClasses,unsigned materials) const {
        if (unsigned(family)>unsigned(GradientFieldFamily::Forbidden) || team>=teams || swim>=movementClasses) return false;
        return family==GradientFieldFamily::Materials || family==GradientFieldFamily::Markets
            ? material<materials : material==NoMaterial;
    }
};
// Target eligibility can differ by movement class (especially ground/air).
// Mutation hooks may share a summary only when membership equivalence is proven.
struct GradientTargetKey
{
    GradientFieldFamily family=GradientFieldFamily::Materials;
    std::uint8_t team=0, swim=0, material=GradientFieldKey::NoMaterial;
    auto operator<=>(const GradientTargetKey&) const = default;
    static GradientTargetKey of(GradientFieldKey key) {
        const bool sharedGround=key.family==GradientFieldFamily::Materials || key.family==GradientFieldFamily::Markets || key.family==GradientFieldFamily::Clear;
        return {key.family,key.team,std::uint8_t(sharedGround && key.swim<AIR_MOVEMENT_CLASS?0:key.swim),key.material};
    }
    std::uint32_t packed() const { return GradientFieldKey{family,team,swim,material}.packed(); }
};
