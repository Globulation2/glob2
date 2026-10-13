// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientCosts.h"
#include "Grid.h"
#include <span>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <stdexcept>
#include <cstdlib>
#include <atomic>
#include <cstdint>
#include <iomanip>
namespace gradient_kernel
{
class GradientProofFailure : public std::runtime_error
{
  public:
	using std::runtime_error::runtime_error;
};
struct GradientProofContext
{
	std::string device;
	std::string driver;
	std::string shader;
	std::uint64_t generation = 0;
};
// Transfer failures precede relaxation. Keep a distinct payload so analysis
// never mistakes packed costs or control words for a computed gradient field.
template <class Expected>
[[noreturn]] void failGradientTransfer(unsigned buffer, std::span<const std::uint16_t> seeds,
									 std::span<const std::uint32_t> actual, field::Grid grid,
									 int limit, Expected expected, GradientProofContext context)
{
	static std::atomic<std::uint64_t> sequence{0};
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto directory = std::getenv("GLOB2_TEST_ARTIFACTS");
	const auto path = std::filesystem::path(directory ? directory : "artifacts") /
		("gradient-transfer-vulkan-" + std::to_string(stamp) + "-" +
		 std::to_string(sequence.fetch_add(1)) + ".bin");
	std::string evidence;
	std::size_t first = 0;
	while (first < actual.size() && actual[first] == expected(first))
		++first;
	const auto detail = first < actual.size()
		? " at word " + std::to_string(first) + ": expected " + std::to_string(expected(first)) +
		  ", actual " + std::to_string(actual[first]) : std::string{};
	try
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		if (seeds.size() != grid.cells() || actual.size() > UINT32_MAX)
			throw std::invalid_argument("transfer evidence dimensions do not match spans");
		const auto writeLE = [&](std::uint32_t value, unsigned bytes)
		{
			for (unsigned i = 0; i < bytes; ++i)
				file.put(char((value >> (i * 8)) & 255));
		};
		// Little-endian GTRF v1: slot, dimensions, cap, word count, original
		// u16 seeds, then expected and actual u32 transfer words.
		for (const auto value : {0x47545246u, 1u, buffer, std::uint32_t(grid.width()),
								std::uint32_t(grid.height()), std::uint32_t(limit),
								std::uint32_t(actual.size())})
			writeLE(value, 4);
		for (const auto value : seeds)
			writeLE(value, 2);
		for (std::size_t i = 0; i < actual.size(); ++i)
			writeLE(expected(i), 4);
		for (const auto value : actual)
			writeLE(value, 4);
		file.flush();
		evidence = file ? "; fixture " + path.string() : "; failed to write fixture " + path.string();
		std::ofstream metadata(path.string() + ".txt");
		metadata << "device " << std::quoted(context.device) << "\ndriver " << std::quoted(context.driver)
			<< "\nshader " << std::quoted(context.shader) << "\ngeneration " << context.generation << '\n';
	}
	catch (const std::exception &error)
	{
		evidence = "; evidence error: " + std::string(error.what());
	}
	throw GradientProofFailure("Vulkan upload roundtrip mismatch in buffer " + std::to_string(buffer) +
							   detail + evidence);
}
// Validation failure is evidence of incorrect execution, never normal admission
// recovery. Keep the 32-bit readback, including values outside field range.
template <class Value, class Costs>
[[noreturn]] void failGradientProof(const char *backend, std::span<const std::uint16_t> seeds,
									std::span<const Value> output, field::Grid grid, int limit,
									Costs costs, GradientProofContext context = {})
{
	static std::atomic<std::uint64_t> sequence{0};
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto directory = std::getenv("GLOB2_TEST_ARTIFACTS");
	const auto path = std::filesystem::path(directory ? directory : "artifacts") /
					  (std::string("gradient-proof-") + backend + "-" + std::to_string(stamp) +
					   "-" + std::to_string(sequence.fetch_add(1)) + ".bin");
	std::string evidence;
	try
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		if (seeds.size() != grid.cells() || output.size() != seeds.size())
			throw std::invalid_argument(
				"proof evidence dimensions do not match input/output spans");
		// Portable little-endian v1 payload: original u16 seeds, untouched u32
		// readback, then packed costs. Sidecar metadata is intentionally separate
		// so existing independent fixture replay tools remain compatible.
		const auto writeLE = [&](std::uint32_t value, unsigned bytes)
		{
			for (unsigned i = 0; i < bytes; ++i)
				file.put(char((value >> (i * 8)) & 255));
		};
		for (const auto v : {0x47505246u, 1u, std::uint32_t(grid.width()),
							 std::uint32_t(grid.height()), std::uint32_t(limit)})
			writeLE(v, 4);
		for (const auto value : seeds)
			writeLE(value, 2);
		for (const auto value : output)
			writeLE(std::uint32_t(value), 4);
		for (std::size_t i = 0; i < seeds.size(); ++i)
		{
			const auto c = costs(i);
			writeLE(c.cardinal | (c.diagonal << 16), 4);
		}
		file.flush();
		evidence =
			file ? "; fixture " + path.string() : "; failed to write fixture " + path.string();
		std::ofstream metadata(path.string() + ".txt");
		metadata << "backend " << std::quoted(backend) << "\ndevice " << std::quoted(context.device)
				 << "\ndriver " << std::quoted(context.driver) << "\nshader "
				 << std::quoted(context.shader) << "\ngeneration " << context.generation << '\n';
	}
	catch (const std::exception &error)
	{
		evidence = "; evidence error: " + std::string(error.what());
	}
	throw GradientProofFailure(std::string(backend) + " gradient failed exact fixed-point proof" +
							   evidence);
}
// Positive integer edge costs make this fixed point unique. Checking against the
// original seeds rejects transient/stale readback, including fabricated sources,
// without trusting the device's convergence flag. Partial results never publish.
template <class Value, class Costs>
bool exactGradientFixedPoint(std::span<const std::uint16_t> seeds, std::span<const Value> output,
							 field::Grid grid, int limit, Costs costs)
{
	if (limit < 0 || output.size() != seeds.size() || seeds.size() != grid.cells())
		return false;
	const int threshold = std::max(2, 65535 - std::min(limit, COST_LIMIT));
	for (std::size_t i = 0; i < seeds.size(); ++i)
	{
		if (output[i] > 65535 || (seeds[i] == 0 && output[i] != 0) || output[i] < seeds[i])
			return false;
		if (!seeds[i])
			continue;
		const auto ownCost = costs(i);
		if (!ownCost.cardinal || !ownCost.diagonal || ownCost.cardinal > 65535 ||
			ownCost.diagonal > 65535)
			return false;
		unsigned best = seeds[i];
		const int x = int(i % grid.width()), y = int(i / grid.width());
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (dx || dy)
				{
					const auto k =
						std::size_t(grid.wrapY(y + dy)) * grid.width() + grid.wrapX(x + dx);
					if (!seeds[k] || output[k] <= 1)
						continue;
					const auto step = costs(k);
					const auto edge = dx && dy ? step.diagonal : step.cardinal;
					if (!edge || edge > 65535)
						return false;
					const int candidate = int(output[k]) - int(edge);
					if (candidate >= threshold)
						best = std::max(best, unsigned(candidate));
				}
		if (output[i] != best)
			return false;
	}
	return true;
}
} // namespace gradient_kernel
