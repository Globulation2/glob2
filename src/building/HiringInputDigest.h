// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/snapshot/WorldSnapshot.h"
#include <map>
#include <tuple>

// Owner-only verification cache. Private fill epochs distinguish refilled pool
// buffers; epochs and allocation identities are cache keys, never hash inputs.
class HiringInputDigests
{
	struct Key
	{
		std::weak_ptr<const void> owner;
		const void *address;
		unsigned domain;
		Uint64 epoch;
	};
	struct Less
	{
		bool operator()(const Key &a, const Key &b) const
		{
			const std::owner_less<std::weak_ptr<const void>> less;
			if (less(a.owner, b.owner))
				return true;
			if (less(b.owner, a.owner))
				return false;
			return std::tie(a.address, a.domain, a.epoch) < std::tie(b.address, b.domain, b.epoch);
		}
	};
	std::map<Key, Uint32, Less> cached;
	template <class T, class F>
	Uint32 component(const std::shared_ptr<const T> &value, unsigned domain, F hash)
	{
		Key key{value, value.get(), domain, value->fillEpoch};
		if (auto it = cached.find(key); it != cached.end())
			return it->second;
		if (cached.size() >= 256)
		{
			std::erase_if(cached, [](const auto &entry) { return entry.first.owner.expired(); });
			if (cached.size() >= 256)
				cached.clear();
		}
		auto digest = hash(*value);
		cached.emplace(std::move(key), digest);
		return digest;
	}

  public:
	Uint32 digest(const SimulationSnapshot::Handle &, Uint64 routeIdentity);
};
