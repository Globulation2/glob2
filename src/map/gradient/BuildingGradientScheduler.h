// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AsyncGradientExecutor.h"
#include "BuildingGradientResultStorage.h"
#include <map>
#include <set>

// Main-thread scheduling state. Jobs and their frozen inputs have independent lifetimes.
struct BuildingGradientScheduler
{
	using Key = std::pair<int, int>;
	struct Job : BuildingGradientResultStorage
	{
		building_gradient::Destination destination;
		std::uint32_t captured = 0, due = 0, generation = 0;
		std::shared_ptr<const building_gradient::Terrain> terrain;
		std::array<std::vector<std::uint16_t>, MAX_NB_RESOURCES> parents;
		std::array<std::uint32_t, MAX_NB_RESOURCES> parentVersions{};
		AsyncGradientExecutor::Handle task;
		std::uint64_t buildNs = 0, buildCpuNs = 0;
		bool metricsCollected = false;
		std::vector<std::size_t> targets;
		bool partial = false;
		std::size_t reservedBytes = 0, snapshotBytes = 0;
	};
	struct Metrics
	{
		std::uint64_t requests = 0, coalesced = 0, jobs = 0, published = 0, discarded = 0,
					  synchronousFallback = 0;
		std::uint64_t snapshotNs = 0, snapshotCpuNs = 0, fallbackNs = 0, fallbackCpuNs = 0,
					  walkingFields = 0, tripFields = 0, buildNs = 0, buildCpuNs = 0, waitNs = 0,
					  maxBytes = 0, maxPending = 0;
	} metrics;
	bool measure = true; // Local diagnostics only; never saved or simulation-affecting.
	static constexpr std::size_t BYTE_LIMIT = 64 * 1024 * 1024;
	std::map<Key, std::uint32_t> requests;
	std::map<Key, std::shared_ptr<Job>> pending;
	std::map<Key, std::uint32_t> epochs;
	std::shared_ptr<AsyncGradientExecutor> executor;
	explicit BuildingGradientScheduler(std::shared_ptr<AsyncGradientExecutor> pool)
		: executor(std::move(pool))
	{
	}
	~BuildingGradientScheduler()
	{
		for (auto &entry : pending)
			try
			{
				executor->wait(entry.second->task);
			}
			catch (...)
			{
			}
	}
	void account(Job &job)
	{
		if (measure && !job.metricsCollected)
		{
			metrics.buildNs += job.buildNs;
			metrics.buildCpuNs += job.buildCpuNs;
			job.metricsCollected = true;
		}
	}
	void finish(bool materialize = false)
	{
		for (auto &entry : pending)
		{
			executor->wait(entry.second->task);
			if (materialize) entry.second->result.materialize();
			account(*entry.second);
		}
	}
	void reset()
	{
		finish();
		pending.clear();
		requests.clear();
		epochs.clear();
		metrics = {};
	}
	void invalidate(int gid, int swim)
	{
		Key key{gid, swim};
		++epochs[key];
		requests.erase(key);
	}
	std::size_t bytes() const
	{
		std::set<std::uint32_t> waves;
		std::size_t value = 0;
		for (const auto &entry : pending)
		{
			const auto &j = *entry.second;
			value += j.reservedBytes;
			if (j.snapshotBytes && waves.insert(j.captured).second)
				value += j.snapshotBytes;
		}
		return value;
	}
};
