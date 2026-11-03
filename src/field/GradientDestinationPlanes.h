// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>

namespace gradient_kernel
{
// Logical coordinate encoding; never reinterpret native bytes as two planes.
// Numeric order is (y,x), matching the lowest row-major cheapest seed.
struct DestinationPlanes
{
    static constexpr std::uint16_t Invalid = UINT16_MAX;
    static constexpr std::uint32_t InvalidCoordinate = UINT32_MAX;
    std::uint32_t* coordinates = nullptr;
    explicit operator bool() const { return coordinates != nullptr; }
    static constexpr std::uint32_t pack(std::uint16_t x,std::uint16_t y)
    { return std::uint32_t(x) | (std::uint32_t(y)<<16); }
    std::uint16_t x(std::size_t i) const { return std::uint16_t(coordinates[i]); }
    std::uint16_t y(std::size_t i) const { return std::uint16_t(coordinates[i]>>16); }
    void set(std::size_t i,std::uint16_t x,std::uint16_t y) const { coordinates[i]=pack(x,y); }
};

// Const ownership leases expose only const coordinate storage.
class DestinationPlane
{
    std::unique_ptr<std::uint32_t[]> data;
public:
    explicit DestinationPlane(std::size_t count) : data(std::make_unique_for_overwrite<std::uint32_t[]>(count)) {}
    std::uint32_t* get() { return data.get(); }
    const std::uint32_t* get() const { return data.get(); }
    std::uint32_t& operator[](std::size_t i) { return data[i]; }
    const std::uint32_t& operator[](std::size_t i) const { return data[i]; }
    explicit operator bool() const { return bool(data); }
};
struct OwnedDestinations
{
    std::size_t cells;
    DestinationPlane coordinates;
    explicit OwnedDestinations(std::size_t count) : cells(count),coordinates(count) {}
    DestinationPlanes planes() { return {coordinates.get()}; }
    std::uint16_t x(std::size_t i) const { return std::uint16_t(coordinates[i]); }
    std::uint16_t y(std::size_t i) const { return std::uint16_t(coordinates[i]>>16); }
    void set(std::size_t i,std::uint16_t x,std::uint16_t y) { coordinates[i]=DestinationPlanes::pack(x,y); }
    std::size_t bytes() const { return cells*sizeof(std::uint32_t); }
};
inline void initializeDestinationSeeds(std::span<const std::uint16_t> seeds,DestinationPlanes destinations,int width)
{
    if(!destinations)return;
    for(std::size_t i=0;i<seeds.size();++i)
        destinations.coordinates[i]=seeds[i]>1?DestinationPlanes::pack(std::uint16_t(i%width),std::uint16_t(i/width)):DestinationPlanes::InvalidCoordinate;
}
}
