// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ScopedEnvironment.h"
#include "GradientAccelerators.h"
#include "GradientGpuValidation.h"
#include "test/fixtures/gradients/WrappedMaterialSeeds.h"
#include "GradientPropagation.h"
#include "TerrainGradient.h"
#include <queue>
#include <random>
#include <type_traits>

namespace
{
struct ConfigurationGuard
{
	gradient_kernel::Configuration applied = gradient_kernel::configuration(),
								   requested = gradient_kernel::requestedBackendConfiguration();
	~ConfigurationGuard()
	{
		gradient_kernel::applyBackendConfiguration(applied);
		gradient_kernel::requestBackendConfiguration(requested);
	}
};
std::vector<std::uint16_t> oracle(std::vector<std::uint16_t> values, field::Grid grid,
								  const std::vector<gradient_kernel::EntrySteps> &costs, int cap)
{
	using Item = std::pair<unsigned, std::size_t>;
	std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
	for (std::size_t i = 0; i < values.size(); ++i)
		if (values[i] > 1)
			queue.emplace(65535 - values[i], i);
	while (!queue.empty())
	{
		const auto [cost, i] = queue.top();
		queue.pop();
		if (cost > unsigned(cap) || values[i] != 65535 - cost)
			continue;
		const int x = int(i % grid.width()), y = int(i / grid.width());
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (dx || dy)
				{
					const auto n =
						std::size_t(grid.wrapY(y + dy)) * grid.width() + grid.wrapX(x + dx);
					const auto next = cost + (dx && dy ? costs[i].diagonal : costs[i].cardinal);
					if (values[n] && next <= unsigned(cap) && 65535 - next > values[n])
					{
						values[n] = 65535 - next;
						queue.emplace(next, n);
					}
				}
	}
	return values;
}
} // namespace
TEST_SUITE("RuntimeGradientBackend")
{
	TEST_CASE("stable selection IDs and unsupported algorithm normalization")
	{
		using namespace gradient_kernel;
		ConfigurationGuard guard;
		for (unsigned i = 0; i < unsigned(Backend::Count); ++i)
			CHECK(parseBackend(backendName(Backend(i))) == Backend(i));
		CHECK_FALSE(parseBackend("unknown"));
		CHECK_FALSE(supportsAlgorithm(Backend::OpenCL, Algorithm::Jacobi));
		requestBackendConfiguration({Backend::Scalar, Algorithm::Jacobi, true});
		CHECK(requestedBackendConfiguration().algorithm == Algorithm::Buckets);
		CHECK(configuration() == guard.applied);
	}
	TEST_CASE("verification environment accepts CLI and boolean spellings")
	{
		using namespace gradient_kernel;
		for (const char *value : {"1", "on", "true"})
		{
			CHECK(parseVerification(value) == true);
			glob2test::ScopedEnvironment env("GLOB2_GRADIENT_VERIFY", value);
			CHECK(initialEnvironmentConfiguration().verify);
		}
		for (const char *value : {"0", "off", "false", "", "unknown", "TRUE"})
		{
			const auto parsed = parseVerification(value);
			if (std::string_view(value) == "0" || std::string_view(value) == "off" ||
				std::string_view(value) == "false")
				CHECK(parsed == false);
			else
				CHECK_FALSE(parsed.has_value());
			glob2test::ScopedEnvironment env("GLOB2_GRADIENT_VERIFY", value);
			CHECK_FALSE(initialEnvironmentConfiguration().verify);
		}
	}
	TEST_CASE("invalid preferences normalize before indexed dispatch")
	{
		using namespace gradient_kernel;
		ConfigurationGuard guard;
		for (const auto invalid : {Backend::Count, Backend(255)})
		{
			CHECK_FALSE(supportsAlgorithm(invalid, Algorithm::Buckets));
			CHECK_THROWS_AS(acceleratorStatus(invalid), std::invalid_argument);
			const auto recovered = acceleratorStatus(Backend::CPU).fallbacks;
			recordGradientFallback(invalid);
			CHECK(acceleratorStatus(Backend::CPU).fallbacks == recovered + 1);
			requestBackendConfiguration({invalid, Algorithm(255), true});
			CHECK(requestedBackendConfiguration() ==
				  Configuration{Backend::CPU, Algorithm::Buckets, true});
			applyBackendConfiguration({invalid, Algorithm::Jacobi, false});
			CHECK(configuration() == Configuration{Backend::CPU, Algorithm::Buckets, false});
		}
		for (const auto b : {Backend::CPU, Backend::Scalar, Backend::SSE2, Backend::NEON,
							 Backend::OpenCL, Backend::Vulkan, Backend::WebGPU})
		{
			const auto c = normalizedConfiguration({b, Algorithm(255), true});
			CHECK(c.backend == b);
			CHECK(supportsAlgorithm(b, c.algorithm));
			CHECK(c.verify);
		}
	}
	TEST_CASE("CPU scopes have one restoration owner and restore after exceptions")
	{
		using namespace gradient_kernel;
		ConfigurationGuard guard;
		static_assert(!std::is_copy_constructible_v<CPUSelectionScope>);
		static_assert(!std::is_move_constructible_v<CPUSelectionScope>);
		const auto original = cpuOverride;
		setBackend(Backend::CPU);
		{
			CPUSelectionScope outer(Backend::Scalar);
			CHECK(cpuBackend() == Backend::Scalar);
			try
			{
				CPUSelectionScope inner(Backend::CPU);
				CHECK(cpuBackend() == automaticCPU());
				throw std::runtime_error("scope unwind");
			}
			catch (const std::runtime_error &)
			{
			}
			CHECK(cpuBackend() == Backend::Scalar);
		}
		CHECK(cpuOverride == original);
	}
	TEST_CASE("CPU scopes freeze resolved variants across preference changes")
	{
		using namespace gradient_kernel;
		ConfigurationGuard guard;
		setBackend(Backend::CPU);
		const auto automatic = automaticCPU();
		{
			CPUSelectionScope scope(Backend::CPU);
			CHECK(cpuOverride == automatic);
			setBackend(Backend::Scalar);
			CHECK(cpuBackend() == automatic);
			{
				CPUSelectionScope fallback(Backend::Vulkan);
				CHECK(cpuBackend() == automatic);
			}
			CHECK(cpuBackend() == automatic);
		}
		CHECK(cpuBackend() == Backend::Scalar);
	}
	TEST_CASE("plan decisions retain the captured backend and compatible algorithm")
	{
		using namespace gradient_kernel;
		ConfigurationGuard guard;
		BackendSession session;
		const Configuration captured{Backend::Vulkan, Algorithm::Jacobi, true};
		applyBackendConfiguration({Backend::Scalar, Algorithm::Buckets, false});
		const auto decision = session.choose(Family::Materials, 1, captured);
		CHECK(decision.configuration == captured);
		CHECK(decision.generation == session.currentGeneration());
		CHECK(decision.plan == Plan::CPU); // Simulation owner cannot dispatch a device.
		const auto direct = session.choose(Family::Clear, 1, Backend::OpenCL);
		CHECK(direct.configuration.backend == Backend::OpenCL);
		CHECK(direct.configuration.algorithm == Algorithm::Frozen8);
		const auto oldGeneration = decision.generation;
		session.invalidateConfiguration();
		CHECK(session.currentGeneration() != oldGeneration);
		CHECK(decision.generation == oldGeneration);
		CHECK(decision.configuration == captured);
	}
	TEST_CASE("shadow verification rejects mismatches with original-input evidence")
	{
		using namespace gradient_kernel;
		ConfigurationGuard guard;
		setBackend(Backend::Scalar);
		const auto directory = std::filesystem::path("artifacts") /
							   ("shadow-verification-" + std::to_string(monotonicNs()));
		std::filesystem::create_directories(directory);
		glob2test::ScopedEnvironment artifacts("GLOB2_TEST_ARTIFACTS", directory.string().c_str());
		BackendSession session;
		std::array<std::uint16_t, 2> seeds{65535, 1}, actual{65535, 123};
		BackendRequest request{actual.data(),
							   COST_LIMIT,
							   {2, 1},
							   session,
							   nullptr,
							   [](void *, std::size_t) { return LAND_STEPS; },
							   [](void *, std::uint16_t *out)
							   {
								   out[0] = 65535;
								   out[1] = 65525;
							   }};
		CHECK_THROWS_AS(verifyGradientResult(request, seeds), std::runtime_error);
		std::size_t files = 0;
		for (const auto &entry : std::filesystem::directory_iterator(directory))
		{
			CHECK(entry.file_size() == 40);
			++files;
		}
		CHECK(files == 1);
		actual[1] = 65525;
		CHECK_NOTHROW(verifyGradientResult(request, seeds));
		std::filesystem::remove_all(directory);
	}
	TEST_CASE("shadow telemetry follows admitted configuration after preferences change")
	{
		using namespace gradient_kernel;
		ConfigurationGuard guard;
		BackendSession session;
		std::array<std::uint16_t, 2> seeds{65535, 1}, actual{65535, 65525};
		BackendRequest request{actual.data(),
							   COST_LIMIT,
							   {2, 1},
							   session,
							   nullptr,
							   [](void *, std::size_t) { return LAND_STEPS; },
							   [](void *, std::uint16_t *out)
							   {
								   out[0] = 65535;
								   out[1] = 65525;
							   }};
		request.capturedConfiguration = Configuration{Backend::Scalar, Algorithm::Buckets, true};
		setBackend(Backend::CPU);
		const auto scalarBefore = acceleratorStatus(Backend::Scalar).verified;
		const auto automaticBefore = acceleratorStatus(Backend::CPU).verified;
		verifyGradientResult(request, seeds);
		CHECK(acceleratorStatus(Backend::Scalar).verified == scalarBefore + 1);
		CHECK(acceleratorStatus(Backend::CPU).verified == automaticBefore);
	}
	TEST_CASE("direct CPU recovery honors explicit variant rather than process preference")
	{
		using namespace gradient_kernel;
		ConfigurationGuard guard;
		setBackend(Backend::CPU);
		BackendSession session;
		std::array<std::uint16_t, 2> values{65535, 1};
		Backend observed = Backend::CPU;
		BackendRequest request{values.data(),
							   COST_LIMIT,
							   {2, 1},
							   session,
							   &observed,
							   [](void *, std::size_t) { return LAND_STEPS; },
							   [](void *context, std::uint16_t *out)
							   {
								   *static_cast<Backend *>(context) = cpuBackend();
								   out[1] = 65525;
							   }};
		executeGradientGroup(std::span(&request, 1), Backend::Scalar);
		CHECK(observed == Backend::Scalar);
		CHECK(values[1] == 65525);
		CHECK(cpuBackend() == automaticCPU());
	}
	TEST_CASE("runtime CPU variants agree with independent heap oracle")
	{
		using namespace gradient_kernel;
		ConfigurationGuard guard;
		std::mt19937 random(17137);
		for (const auto backend : {Backend::Scalar, Backend::SSE2, Backend::NEON, Backend::CPU})
		{
			if (!cpuVariantAvailable(backend))
				continue;
			setBackend(backend);
			for (const auto &dimensions : {std::pair{1, 32}, std::pair{32, 1}, std::pair{2, 16},
										   std::pair{17, 19}, std::pair{32, 32}})
				for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
					for (const int cap : {0, 70, COST_LIMIT})
					{
						field::Grid grid{dimensions.first, dimensions.second};
						std::vector<std::uint16_t> seeds(grid.cells(), 1);
						std::vector<bool> water(grid.cells());
						std::vector<EntrySteps> costs(grid.cells());
						for (std::size_t i = 0; i < grid.cells(); ++i)
						{
							water[i] = random() % 3 == 0;
							if (random() % 7 == 0 || (swim == 0 && water[i]) ||
								(swim == WATER_ONLY_CLASS && !water[i]))
								seeds[i] = 0;
							costs[i] = weightedClass(swim) && water[i]
										   ? entrySteps(WATER_STEP[swim])
										   : LAND_STEPS;
						}
						seeds[0] = 65535;
						seeds.back() = 65535 - 320;
						auto actual = seeds;
						GradientWorkspace workspace;
						propagateFieldCPU(actual.data(), swim, cap, grid, workspace,
										  [&](std::size_t i) { return water[i]; });
						CHECK(actual == oracle(seeds, grid, costs, cap));
					}
		}
	}
	TEST_CASE("runtime terrain kernels preserve every completed cost layer")
	{
		using namespace gradient_kernel;
		ConfigurationGuard guard;
		const field::Grid grid{16, 16};
		const PreparedTerrainCosts<3> profile(
			std::array<EntrySteps, 3>{{{10, 14}, {5, 7}, {20, 20}}});
		std::vector<std::uint16_t> seeds(grid.cells(), 1);
		seeds[0] = 65535;
		for (std::size_t i = 13; i < seeds.size(); i += 17)
			seeds[i] = 0;
		std::vector<EntrySteps> costs(grid.cells());
		for (std::size_t i = 0; i < costs.size(); ++i)
			costs[i] = profile.classes[i % profile.classCount];
		std::vector<std::vector<std::uint16_t>> scalarLayers;
		for (const auto backend : {Backend::Scalar, Backend::SSE2, Backend::NEON})
		{
			if (!cpuVariantAvailable(backend))
				continue;
			setBackend(backend);
			auto values = seeds;
			std::array<GradientBucket, BUCKETS> queue;
			queue[0].push(0);
			std::size_t pending = 1;
			for (int cost = 0; cost <= 300; ++cost)
			{
				expandPreparedTerrainBucket(values.data(), queue.data(), pending, cost, 300, grid,
											profile,
											[&](std::size_t i) { return i % profile.classCount; });
				if (backend == Backend::Scalar)
					scalarLayers.push_back(values);
				else
					CHECK(values == scalarLayers[cost]);
			}
			CHECK(values == oracle(seeds, grid, costs, 300));
		}
	}
	TEST_CASE("Vulkan algorithms agree with oracle for weighted capped wrapped fields")
	{
		using namespace gradient_kernel;
		ConfigurationGuard guard;
		if (!initializeVulkanGradient())
		{
			if (std::getenv("GLOB2_REQUIRE_VULKAN"))
				FAIL(vulkanGradientStatus().error);
			MESSAGE("Vulkan unavailable: ", vulkanGradientStatus().error);
			return;
		}
		std::mt19937 random(98731);
		BackendSession session;
		for (const auto plan : {Plan::VulkanFrozen8, Plan::VulkanJacobi})
			for (const auto &dimensions : {std::pair{1, 1}, std::pair{1, 32}, std::pair{32, 1},
										   std::pair{2, 16}, std::pair{17, 19}, std::pair{32, 32}})
				for (const int cap : {0, 70, COST_LIMIT})
				{
					const auto planID = int(plan);
					CAPTURE(planID);
					CAPTURE(dimensions.first);
					CAPTURE(dimensions.second);
					CAPTURE(cap);
					field::Grid grid{dimensions.first, dimensions.second};
					std::vector<std::uint16_t> seeds(grid.cells(), 1);
					std::vector<EntrySteps> costs(grid.cells());
					for (std::size_t i = 0; i < grid.cells(); ++i)
					{
						if (random() % 6 == 0)
							seeds[i] = 0;
						const unsigned step = 1 + random() % 181;
						costs[i] = {step, step * 14 / 10};
					}
					seeds[0] = 65535;
					seeds.back() = 65535 - 400;
					auto actual = seeds;
					BackendRequest request{
						actual.data(),
						cap,
						grid,
						session,
						&costs,
						[](void *p, std::size_t i)
						{ return (*static_cast<std::vector<EntrySteps> *>(p))[i]; },
						nullptr};
					bool committed = false;
					request.executedOnDevice = &committed;
					REQUIRE(executeVulkanGradient(std::span(&request, 1), plan));
					CHECK(committed);
					const auto expected = oracle(seeds, grid, costs, cap);
					if (actual != expected)
					{
						const auto mismatch =
							std::mismatch(actual.begin(), actual.end(), expected.begin());
						const auto i = mismatch.first - actual.begin();
						CAPTURE(i);
						CAPTURE(actual[i]);
						CAPTURE(expected[i]);
						CHECK(actual == expected);
					}
					else
						CHECK(actual == expected);
				}
	}
	TEST_CASE("wrapped dense sources reject invented paths before GPU publication")
	{
		using namespace gradient_kernel;
		const field::Grid grid{128, 128};
		std::vector<std::uint16_t> seeds;
		for (const auto run : gradient_fixture::wrappedMaterialRuns)
		{
			const auto code = run & 3;
			seeds.insert(seeds.end(), run >> 2, code == 2 ? 65535 : code);
		}
		REQUIRE(seeds.size() == grid.cells());
		const std::vector<EntrySteps> costs(grid.cells(), LAND_STEPS);
		const auto expected = oracle(seeds, grid, costs, COST_LIMIT);
		CHECK(exactGradientFixedPoint(std::span<const std::uint16_t>(seeds),
									  std::span<const std::uint16_t>(expected), grid, COST_LIMIT,
									  [](auto) { return LAND_STEPS; }));
		auto corrupted = expected;
		corrupted[111] = 65365;
		CHECK_FALSE(exactGradientFixedPoint(std::span<const std::uint16_t>(seeds),
											std::span<const std::uint16_t>(corrupted), grid,
											COST_LIMIT, [](auto) { return LAND_STEPS; }));
		if (!initializeVulkanGradient())
		{
			if (std::getenv("GLOB2_REQUIRE_VULKAN"))
				FAIL("Vulkan dense fixture requires an available device");
			return;
		}
		BackendSession session;
		for (const auto plan : {Plan::VulkanFrozen8, Plan::VulkanJacobi})
			for (int repeat = 0; repeat < 3; ++repeat)
			{
				const auto planID = int(plan);
				CAPTURE(planID);
				CAPTURE(repeat);
				auto actual = seeds;
				BackendRequest request{actual.data(), COST_LIMIT,
									   grid,          session,
									   nullptr,       [](void *, auto) {return LAND_STEPS; },
									   nullptr};
				bool committed = false, observed = false;
				request.executedOnDevice = &committed;
				request.deviceExecutionObserved = &observed;
				REQUIRE(executeVulkanGradient(std::span(&request, 1), plan));
				CHECK(committed);
				CHECK(observed);
				CHECK(actual == expected);
			}
	}
	TEST_CASE("Vulkan cancellation leaves original seeds untouched")
	{
		using namespace gradient_kernel;
		if (!initializeVulkanGradient())
			return;
		BackendSession session;
		std::vector<std::uint16_t> values(1024, 1);
		values[0] = 65535;
		const auto seeds = values;
		BackendRequest request{values.data(), COST_LIMIT,
							   {32, 32},      session,
							   nullptr,       [](void *, std::size_t) {return LAND_STEPS; },
							   nullptr};
		request.requiredControl = std::make_shared<OpenCLRequiredControl>();
		request.requiredControl->requestCancel();
		CHECK_FALSE(executeVulkanGradient(std::span(&request, 1), Plan::VulkanJacobi));
		CHECK(values == seeds);
	}

	TEST_CASE("fixed-point proof agrees with independent weighted oracle on narrow tori")
	{
		using namespace gradient_kernel;
		for (const auto &dimensions : {std::pair{1, 1}, std::pair{1, 17}, std::pair{19, 1},
									   std::pair{3, 5}, std::pair{18, 33}})
		{
			const field::Grid grid{dimensions.first, dimensions.second};
			std::vector<std::uint16_t> seeds(grid.cells(), 1);
			std::vector<EntrySteps> costs(grid.cells());
			for (std::size_t i = 0; i < seeds.size(); ++i)
			{
				costs[i] = {unsigned(i % 11 + 1), unsigned(i % 13 + 1)};
				if (i % 7 == 3)
					seeds[i] = 0;
			}
			seeds.front() = 65535;
			seeds.back() = 65520;
			for (const auto cap : {0, 1, 10, 37, COST_LIMIT})
			{
				const auto expected = oracle(seeds, grid, costs, cap);
				CHECK(exactGradientFixedPoint(std::span<const std::uint16_t>(seeds),
											  std::span<const std::uint16_t>(expected), grid, cap,
											  [&](std::size_t i) { return costs[i]; }));
				auto corrupted = std::vector<std::uint32_t>(expected.begin(), expected.end());
				corrupted.front() = 0;
				CHECK_FALSE(exactGradientFixedPoint(std::span<const std::uint16_t>(seeds),
													std::span<const std::uint32_t>(corrupted), grid,
													cap, [&](std::size_t i) { return costs[i]; }));
			}
		}
	}
	TEST_CASE("fixed-point proof rejects malformed spans costs caps and fabricated sources")
	{
		using namespace gradient_kernel;
		const field::Grid grid{3, 1};
		const std::array<std::uint16_t, 3> seeds{65535, 0, 1};
		const std::array<std::uint32_t, 3> output{65535, 0, 65525};
		const auto valid = [](std::size_t) { return LAND_STEPS; };
		CHECK(exactGradientFixedPoint(std::span<const std::uint16_t>(seeds),
									  std::span<const std::uint32_t>(output), grid, COST_LIMIT,
									  valid));
		CHECK_FALSE(exactGradientFixedPoint(std::span<const std::uint16_t>(seeds),
											std::span<const std::uint32_t>(output), grid, -1,
											valid));
		CHECK_FALSE(exactGradientFixedPoint(std::span<const std::uint16_t>(seeds),
											std::span<const std::uint32_t>(output).first(2), grid,
											COST_LIMIT, valid));
		for (const auto bad :
			 {EntrySteps{0, 14}, EntrySteps{10, 0}, EntrySteps{65536, 14}, EntrySteps{10, 65536}})
			CHECK_FALSE(exactGradientFixedPoint(std::span<const std::uint16_t>(seeds),
												std::span<const std::uint32_t>(output), grid,
												COST_LIMIT, [&](std::size_t) { return bad; }));
		for (const auto bad : {std::array<std::uint32_t, 3>{65535, 65535, 65525},
							   std::array<std::uint32_t, 3>{65536, 0, 65525},
							   std::array<std::uint32_t, 3>{65535, 0, 65535}})
			CHECK_FALSE(exactGradientFixedPoint(std::span<const std::uint16_t>(seeds),
												std::span<const std::uint32_t>(bad), grid,
												COST_LIMIT, valid));
	}
	TEST_CASE("proof evidence preserves original seeds and out-of-range readback portably")
	{
		using namespace gradient_kernel;
		const auto directory = std::filesystem::path("artifacts") /
							   ("proof-validation-" + std::to_string(monotonicNs()));
		glob2test::ScopedEnvironment artifacts("GLOB2_TEST_ARTIFACTS", directory.string().c_str());
		const std::array<std::uint16_t, 2> seeds{65535, 1};
		const std::array<std::uint32_t, 2> output{0, 70000};
		CHECK_THROWS_AS(failGradientProof("test", std::span<const std::uint16_t>(seeds),
										  std::span<const std::uint32_t>(output), {2, 1}, 37,
										  [](std::size_t) { return LAND_STEPS; },
										  {"device", "driver", "shader", 42}),
						GradientProofFailure);
		std::filesystem::path binary, metadata;
		for (const auto &file : std::filesystem::directory_iterator(directory))
			if (file.path().extension() == ".bin")
				binary = file.path();
			else
				metadata = file.path();
		REQUIRE_FALSE(binary.empty());
		REQUIRE_FALSE(metadata.empty());
		std::ifstream fixture(binary, std::ios::binary);
		std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(fixture)), {});
		REQUIRE(bytes.size() == 40);
		CHECK(bytes[0] == 0x46);
		CHECK(bytes[1] == 0x52);
		CHECK(bytes[20] == 255);
		CHECK(bytes[21] == 255);
		const auto read32 = [&](std::size_t i)
		{
			return unsigned(bytes[i]) | (unsigned(bytes[i + 1]) << 8) |
				   (unsigned(bytes[i + 2]) << 16) | (unsigned(bytes[i + 3]) << 24);
		};
		CHECK(read32(24) == 0);
		CHECK(read32(28) == 70000);
		CHECK(read32(32) == (10u | (14u << 16)));
		std::ifstream info(metadata);
		const std::string text((std::istreambuf_iterator<char>(info)), {});
		CHECK(text.find("generation 42") != std::string::npos);
		CHECK(text.find("shader \"shader\"") != std::string::npos);
	}
}

TEST_SUITE("RuntimeGradientBackend")
{
	TEST_CASE("discovery distinguishes supported devices from selectable status")
	{
		using namespace gradient_kernel;
		BackendDescriptor descriptor{Backend::Vulkan,      "vulkan",         true, true,
									 {Algorithm::Frozen8}, {Backend::Vulkan}};
		for (const auto state :
			 {Availability::Uninitialized, Availability::Initializing, Availability::Ready,
			  Availability::Unavailable, Availability::Failed})
		{
			CAPTURE(state);
			descriptor.status.availability = state;
			CHECK(descriptor.selectable() ==
				  (state == Availability::Ready || state == Availability::Uninitialized));
			descriptor.compiled = false;
			CHECK_FALSE(descriptor.selectable());
			descriptor.compiled = true;
			descriptor.available = false;
			CHECK_FALSE(descriptor.selectable());
			descriptor.available = true;
		}
	}
}

TEST_SUITE("RuntimeGradientBackend")
{
	TEST_CASE("malformed direct groups reject before touching fields or diagnostics")
	{
		using namespace gradient_kernel;
		BackendSession session;
		std::uint16_t value = 65535;
		bool executed = true;
		unsigned calls = 0;
		BackendRequest request{&value,
							   COST_LIMIT,
							   {1, 1},
							   session,
							   &calls,
							   [](void *, std::size_t) { return LAND_STEPS; },
							   [](void *context, std::uint16_t *)
							   { ++*static_cast<unsigned *>(context); }};
		request.executedOnDevice = &executed;
		for (unsigned missing = 0; missing < 3; ++missing)
		{
			auto malformed = request;
			if (missing == 0)
				malformed.gradient = nullptr;
			if (missing == 1)
				malformed.costAt = nullptr;
			if (missing == 2)
				malformed.cpu = nullptr;
			CHECK_THROWS_AS(executeGradientGroup(std::span(&malformed, 1), Backend::Scalar),
							std::invalid_argument);
			CHECK(value == 65535);
			CHECK(executed);
			CHECK(calls == 0);
		}
	}
}

TEST_SUITE("RuntimeGradientBackend")
{
	TEST_CASE("direct group accepts one owned batch recovery without individual callbacks")
	{
		using namespace gradient_kernel;
		BackendSession session;
		std::array<std::uint16_t, 2> values{65535, 1};
		unsigned calls = 0;
		BackendRequest request{values.data(), COST_LIMIT,
							   {2, 1},        session,
							   &calls,        [](void *, std::size_t) {return LAND_STEPS; },
							   nullptr};
		request.cpuBatch = [](std::span<const BackendRequest *const> requests,
							  std::span<std::uint16_t *const> outputs)
		{
			++*static_cast<unsigned *>(requests.front()->context);
			outputs.front()[1] = 65525;
		};
		executeGradientGroup(std::span(&request, 1), Backend::Scalar);
		CHECK(calls == 1);
		CHECK(values[1] == 65525);
		const std::array<std::uint16_t, 2> original{65535, 1};
		request.capturedConfiguration = Configuration{Backend::Scalar, Algorithm::Buckets, true};
		CHECK_NOTHROW(verifyGradientResult(request, original));
		CHECK(calls == 2);
		CHECK_THROWS_AS(verifyGradientResult(request, std::span(original).first(1)),
						std::invalid_argument);
		CHECK(calls == 2);
		values[1] = 1;
		auto other = request;
		other.cpuBatch = nullptr;
		std::array group{request, other};
		CHECK_THROWS_AS(executeGradientGroup(group, Backend::Scalar), std::invalid_argument);
		CHECK(calls == 2);
		CHECK(values[1] == 1);
	}
}

TEST_SUITE("RuntimeGradientBackend")
{
	TEST_CASE("shadow mismatch reports evidence failure without losing the primary error")
	{
		using namespace gradient_kernel;
		const auto directory = std::filesystem::temp_directory_path() /
							   ("gradient-evidence-file-" + std::to_string(monotonicNs()));
		struct Cleanup
		{
			std::filesystem::path path;
			~Cleanup()
			{
				std::error_code error;
				std::filesystem::remove(path, error);
			}
		} cleanup{directory};
		{
			std::ofstream file(directory);
			file << "this is a file, not an artifact directory";
		}
		glob2test::ScopedEnvironment artifacts("GLOB2_TEST_ARTIFACTS", directory.string().c_str());
		BackendSession session;
		std::array<std::uint16_t, 2> actual{65535, 1}, original = actual;
		BackendRequest request{actual.data(),
							   COST_LIMIT,
							   {2, 1},
							   session,
							   nullptr,
							   [](void *, std::size_t) { return LAND_STEPS; },
							   [](void *, std::uint16_t *output) { output[1] = 65525; }};
		try
		{
			verifyGradientResult(request, original);
			FAIL("mismatch must stop execution");
		}
		catch (const std::runtime_error &error)
		{
			CHECK(std::string(error.what()).find("mismatch at cell 1") != std::string::npos);
			CHECK(std::string(error.what()).find("unable to preserve fixture") !=
				  std::string::npos);
		}
		CHECK(actual[1] == 1);
	}
}
