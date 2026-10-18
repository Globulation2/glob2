// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <span>
#include <array>
namespace ResourceExperiment { struct Lazy; }
namespace ResourceSnapshotExperiment {
void registerSlot(std::uint16_t** slot,ResourceExperiment::Lazy* field);
void unregisterSlot(std::uint16_t** slot);
bool resolveOwner(const std::uint16_t* values,size_t cell);
std::shared_ptr<std::vector<std::uint16_t>> clonePlane(std::uint16_t* const* slot,std::shared_ptr<std::vector<std::uint16_t>> values);
// Full readers materialize all values; positional readers hold the returned lock
// throughout their read. Both operate only on captured inputs, never a live Map.
void finish(const std::shared_ptr<const std::vector<std::uint16_t>>& values);
void resolve(const std::shared_ptr<const std::vector<std::uint16_t>>& values,size_t cell,std::unique_lock<std::mutex>& guard);
std::array<std::uint64_t,4> metrics();
}
