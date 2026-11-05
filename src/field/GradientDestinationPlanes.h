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
// Optional output, owned and published with the corresponding cost plane.
// Coordinates identify the lowest row-major seed among all cheapest routes.
// Forbidden/unreachable cells have Invalid in both planes. A retained seed
// beyond the propagation cap still identifies itself.
struct DestinationPlanes
{
    static constexpr std::uint16_t Invalid = std::numeric_limits<std::uint16_t>::max();
    std::uint16_t* x = nullptr;
    std::uint16_t* y = nullptr;
    explicit operator bool() const { return x && y; }
};

// Unlike const unique_ptr<T[]>, a const plane cannot mutate its elements. This
// keeps published shared_ptr<const OwnedDestinations> leases actually read-only.
class DestinationPlane
{
    std::unique_ptr<std::uint16_t[]> data;
public:
    explicit DestinationPlane(std::size_t count) : data(std::make_unique_for_overwrite<std::uint16_t[]>(count)) {}
    std::uint16_t* get() { return data.get(); }
    const std::uint16_t* get() const { return data.get(); }
    std::uint16_t& operator[](std::size_t index) { return data[index]; }
    const std::uint16_t& operator[](std::size_t index) const { return data[index]; }
    explicit operator bool() const { return bool(data); }
    void reset(std::uint16_t* pointer = nullptr) { data.reset(pointer); }
};

struct OwnedDestinations
{
    std::size_t cells;
    DestinationPlane x, y;
    explicit OwnedDestinations(std::size_t count) : cells(count), x(count), y(count) {}
    DestinationPlanes planes() { return {x.get(), y.get()}; }
    std::size_t bytes() const { return cells * 2 * sizeof(std::uint16_t); }
};

inline void initializeDestinationSeeds(std::span<const std::uint16_t> seeds,
    DestinationPlanes destinations, int width)
{
    if (!destinations) return;
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        destinations.x[i] = seeds[i] > 1 ? std::uint16_t(i % width) : DestinationPlanes::Invalid;
        destinations.y[i] = seeds[i] > 1 ? std::uint16_t(i / width) : DestinationPlanes::Invalid;
    }
}
}
