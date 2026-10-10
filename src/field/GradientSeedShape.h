// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace gradient_kernel
{
// Collected only during an already-required simple seeder's stores. Unknown
// classes deliberately stay singleton; observing a completed array would add
// another scan and could mistake weighted starting costs for ordinary goals.
struct GradientSeedShape
{
    std::size_t cells=0,sources=0,blockers=0;
    bool known=false;
    void observe(std::uint16_t value) noexcept {sources+=value>1;blockers+=value==0;}
    static std::uint8_t density(std::size_t count,std::size_t cells) noexcept {
        if(!cells || count>cells)return 255;
        // Avoid count*16 overflow even for independent synthetic callers.
        // A field is at most the shared host budget in production.
        std::uint8_t bucket=0;
        for(unsigned i=1;i<=16;++i) {
            const auto threshold=(cells/16)*i+((cells%16)*i+15)/16;
            if(count<threshold)break;bucket=std::uint8_t(i);
        }
        return bucket;
    }
    std::uint8_t seedDensity() const noexcept {return known ? density(sources,cells) : 255;}
    std::uint8_t blockerDensity() const noexcept {return known ? density(blockers,cells) : 255;}
};
}
