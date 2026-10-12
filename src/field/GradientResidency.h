// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace gradient_kernel::residency
{
enum class State
{
	Invalid,
	Uploading,
	Ready
};
struct Identity
{
	std::uint64_t world = 0, componentVersion = 0, tableVersion = 0;
	unsigned width = 0, height = 0, component = 0;
	std::shared_ptr<const void> owner, tableOwner;
	std::vector<std::uint64_t> chunks;
	bool operator==(const Identity &other) const
	{
		const auto same = [](const auto &a, const auto &b)
		{ return !a.owner_before(b) && !b.owner_before(a); };
		return world == other.world && componentVersion == other.componentVersion &&
			   tableVersion == other.tableVersion && width == other.width &&
			   height == other.height && component == other.component && chunks == other.chunks &&
			   same(owner, other.owner) && same(tableOwner, other.tableOwner);
	}
};
struct Range
{
	std::size_t offset = 0, bytes = 0;
	bool operator==(const Range &) const = default;
};
struct UploadPlan
{
	std::vector<Range> ranges;
	std::size_t bytes = 0, metadataBytes = 0;
	bool full = false;
	bool fitsSpeculation() const
	{
		return bytes <= 256 * 1024 && ranges.size() + (metadataBytes ? 1 : 0) <= 16;
	}
};
// Ranges refer directly to the original array. Alignment widening may include
// unchanged cells; it is counted, coalesced and never packed on the host.
inline UploadPlan planUploads(unsigned width, unsigned height, std::size_t stride,
							  std::span<const std::uint64_t> previous,
							  std::span<const std::uint64_t> current, std::size_t alignment = 4,
							  std::size_t metadata = 0, bool fullRefresh = false)
{
	if (!width || !height || !stride || !alignment || (alignment & (alignment - 1)))
		throw std::invalid_argument("Invalid residency array layout");
	// Validate before multiplying: overflow must never become a small admitted
	// allocation or a copy range that extends outside the captured owner.
	constexpr auto maximum = std::numeric_limits<std::size_t>::max();
	const auto cells = std::size_t(width) * height;
	if (cells / height != width || stride > maximum / cells)
		throw std::invalid_argument("Residency array extent overflows");
	const auto total = cells * stride;
	if (total % alignment || metadata > maximum - total)
		throw std::invalid_argument("Unsupported aligned residency extent");
	const auto chunksWide = (std::size_t(width) - 1) / 16 + 1;
	const auto chunksHigh = (std::size_t(height) - 1) / 16 + 1;
	if (current.size() != chunksWide * chunksHigh)
		throw std::invalid_argument("Current residency stamps do not cover the array");
	UploadPlan plan;
	plan.metadataBytes = metadata;
	const auto full = [&]
	{
		plan.ranges = {{0, total}};
		plan.bytes = total + metadata;
		plan.full = true;
	};
	if (fullRefresh || previous.size() != chunksWide * chunksHigh)
	{
		full();
		return plan;
	}
	for (std::size_t chunk = 0; chunk < current.size(); ++chunk)
	{
		if (current[chunk] == previous[chunk])
			continue;
		const auto x = (chunk % chunksWide) * 16, y = (chunk / chunksWide) * 16;
		for (std::size_t row = y; row < std::min<std::size_t>(y + 16, height); ++row)
		{
			const auto begin = (row * width + x) * stride,
					   end = (row * width + std::min<std::size_t>(x + 16, width)) * stride;
			const auto alignedBegin = begin & ~(alignment - 1);
			const auto alignedEnd = end + (alignment - end % alignment) % alignment;
			plan.ranges.push_back({alignedBegin, alignedEnd - alignedBegin});
		}
	}
	std::sort(plan.ranges.begin(), plan.ranges.end(),
			  [](const auto &a, const auto &b) { return a.offset < b.offset; });
	std::vector<Range> coalesced;
	for (const auto &range : plan.ranges)
	{
		if (!coalesced.empty() && range.offset <= coalesced.back().offset + coalesced.back().bytes)
			coalesced.back().bytes = std::max(coalesced.back().offset + coalesced.back().bytes,
											  range.offset + range.bytes) -
									 coalesced.back().offset;
		else
			coalesced.push_back(range);
	}
	plan.ranges = std::move(coalesced);
	plan.bytes = metadata;
	for (const auto &range : plan.ranges)
		plan.bytes += range.bytes;
	if (plan.bytes - metadata >= total / 2 + total % 2)
		full();
	return plan;
}

// Contract model only: production adapters do not yet bind residency stores.
// An adapter resource must own this state until every upload and lease retires.
// Invalid contents can still retain live storage and its immutable input owners;
// invalidation is not a transfer/fence retirement proof.
class Buffer
{
	State state_ = State::Invalid;
	Identity identity_, target_;
	std::uint64_t serial_ = 0;
	bool retired_ = true;
	unsigned leases_ = 0;

  public:
	Buffer() = default;
	// Copying or moving would duplicate outstanding lease counts and proofs.
	Buffer(const Buffer &) = delete;
	Buffer &operator=(const Buffer &) = delete;
	Buffer(Buffer &&) = delete;
	Buffer &operator=(Buffer &&) = delete;
	State state() const { return state_; }
	bool idle() const { return leases_ == 0 && retired_ && state_ != State::Uploading; }
	bool matches(const Identity &identity) const
	{
		return state_ == State::Ready && identity_ == identity;
	}
	std::uint64_t begin(Identity identity)
	{
		if (!identity.owner || !identity.world || !identity.width || !identity.height ||
			(identity.tableVersion && !identity.tableOwner) || !idle() ||
			serial_ == std::numeric_limits<std::uint64_t>::max())
			return 0;
		// The previous identity ceases to be available before any upload starts.
		identity_ = {};
		target_ = std::move(identity);
		state_ = State::Uploading;
		retired_ = false;
		return ++serial_;
	}
	bool complete(std::uint64_t serial, bool allTransfersSucceeded, bool retirementProof)
	{
		if (serial != serial_ || state_ != State::Uploading)
			return false;
		if (!retirementProof)
		{
			if (!allTransfersSucceeded)
				state_ = State::Invalid;
			// Keep the captured owners until a matching retirement proof arrives.
			return false;
		}
		retired_ = true;
		if (allTransfersSucceeded)
		{
			identity_ = std::move(target_);
			state_ = State::Ready;
		}
		else
		{
			target_ = {};
			state_ = State::Invalid;
		}
		return allTransfersSucceeded;
	}
	void invalidate()
	{
		state_ = State::Invalid;
		if (!leases_)
			identity_ = {};
		if (retired_)
			target_ = {};
	}
	void proveRetirement(std::uint64_t serial)
	{
		if (serial == serial_ && state_ == State::Invalid)
		{
			retired_ = true;
			target_ = {};
		}
	}
	bool acquire(const Identity &identity)
	{
		if (!matches(identity) || leases_ == std::numeric_limits<unsigned>::max())
			return false;
		++leases_;
		return true;
	}
	void release()
	{
		if (!leases_)
			throw std::logic_error("Unbalanced residency lease");
		--leases_;
		if (!leases_ && state_ == State::Invalid)
			identity_ = {};
	}
};
} // namespace gradient_kernel::residency
