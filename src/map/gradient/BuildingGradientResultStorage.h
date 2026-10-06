// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingGradientBuild.h"
#include <cstdio>
#include <memory>
#include <stdexcept>

// Completed overflow results stay private until their simulation deadline. Anonymous
// temporary files bound retained RAM; their contents, rather than local paths, enter saves.
struct BuildingGradientSpool
{
	std::unique_ptr<std::FILE, int (*)(std::FILE *)> file{std::tmpfile(), std::fclose};
	std::uint64_t end = 0;
	BuildingGradientSpool()
	{
		if (!file)
			throw std::runtime_error("Cannot create private building gradient storage");
	}
	void seek(std::uint64_t position) const
	{
#ifdef _WIN32
		const auto error = ::_fseeki64(file.get(), position, SEEK_SET);
#else
		const auto error = ::fseeko(file.get(), position, SEEK_SET);
#endif
		if (error != 0)
			throw std::runtime_error("Cannot seek completed building gradient");
	}
};

class BuildingGradientResultStorage
{
	std::shared_ptr<BuildingGradientSpool> spill;
	std::uint64_t offset = 0;
	static void write(std::FILE *file, const void *data, std::size_t bytes)
	{
		if (bytes && std::fwrite(data, 1, bytes, file) != bytes)
			throw std::runtime_error("Cannot retain completed building gradient");
	}
	static void read(std::FILE *file, void *data, std::size_t bytes)
	{
		if (bytes && std::fread(data, 1, bytes, file) != bytes)
			throw std::runtime_error("Cannot read completed building gradient");
	}

  public:
	building_gradient::Result result;
	void spillResult(const std::shared_ptr<BuildingGradientSpool> &storage)
	{
		result.materialize();
		storage->seek(storage->end);
		const auto start = storage->end;
		auto field = [&](auto &values)
		{
			const std::uint64_t count = values.size();
			write(storage->file.get(), &count, sizeof(count));
			write(storage->file.get(), values.data(), values.size() * sizeof(values[0]));
			storage->end += sizeof(count) + values.size() * sizeof(values[0]);
		};
		field(result.walking);
		for (auto &trip : result.trips)
			field(trip);
		if (std::fflush(storage->file.get()) != 0)
			throw std::runtime_error("Cannot flush completed building gradient");
		spill = storage;
		offset = start;
		std::vector<std::uint16_t>().swap(result.walking);
		for (auto &trip : result.trips)
			std::vector<std::uint16_t>().swap(trip);
	}
	template <class Visitor> void visitResult(Visitor visitor) const
	{
		if (!spill)
		{
			visitor(result);
			return;
		}
		spill->seek(offset);
		building_gradient::Result values;
		values.locked = result.locked;
		values.resourceState = result.resourceState;
		auto field = [&](auto &cells)
		{
			std::uint64_t count = 0;
			read(spill->file.get(), &count, sizeof(count));
			cells.resize(count);
			read(spill->file.get(), cells.data(), cells.size() * sizeof(cells[0]));
		};
		field(values.walking);
		for (auto &trip : values.trips)
			field(trip);
		visitor(values);
	}
};
