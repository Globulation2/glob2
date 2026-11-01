// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ScopedEnvironment.h"
#include "GradientAccelerators.h"
#include "OpenCLGradient.h"
#include "GradientOracle.h"
#include "test/fixtures/gradients/WrappedMaterialSeeds.h"
#include "test/fixtures/gradients/RetainedVulkanSeeds.h"
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
					if (!(grid.incomingMask(i) & (1u << field::directionIndex(dx, dy))))
						continue;
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
		requestBackendConfiguration({Backend::Scalar, Algorithm::Jacobi});
		CHECK(requestedBackendConfiguration().algorithm == Algorithm::Buckets);
		CHECK(configuration() == guard.applied);
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
			requestBackendConfiguration({invalid, Algorithm(255)});
			CHECK(requestedBackendConfiguration() ==
				  Configuration{Backend::CPU, Algorithm::Buckets});
			applyBackendConfiguration({invalid, Algorithm::Jacobi});
			CHECK(configuration() == Configuration{Backend::CPU, Algorithm::Buckets});
		}
		for (const auto b : {Backend::CPU, Backend::Scalar, Backend::SSE2, Backend::NEON,
							 Backend::OpenCL, Backend::Vulkan, Backend::WebGPU})
		{
			const auto c = normalizedConfiguration({b, Algorithm(255)});
			CHECK(c.backend == b);
			CHECK(supportsAlgorithm(b, c.algorithm));
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
		const Configuration captured{Backend::Vulkan, Algorithm::Jacobi};
		applyBackendConfiguration({Backend::Scalar, Algorithm::Buckets});
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
					if (cap == 70)
					{
						auto edges = std::make_shared<std::vector<std::uint8_t>>(grid.cells());
						for (auto &mask : *edges)
							mask = random() & 255;
						grid = grid.withConnections(edges);
					}
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
	TEST_CASE("Vulkan movement classes preserve deferred seeds on wrapped partial tiles")
	{
		using namespace gradient_kernel;
		if (!initializeVulkanGradient())
		{
			if (std::getenv("GLOB2_REQUIRE_VULKAN"))
				FAIL(vulkanGradientStatus().error);
			MESSAGE("Vulkan unavailable: ", vulkanGradientStatus().error);
			return;
		}
		BackendSession session;
		for (const auto dimensions : {std::pair{1, 17}, std::pair{19, 1},
									 std::pair{17, 33}, std::pair{33, 17}})
			for (unsigned swim = 0; swim < std::size(WATER_STEP); ++swim)
				for (const int cap : {0, 1, 70, 400, COST_LIMIT})
				{
					CAPTURE(dimensions.first);
					CAPTURE(dimensions.second);
					CAPTURE(swim);
					CAPTURE(cap);
					field::Grid grid{dimensions.first, dimensions.second};
					std::vector<std::uint16_t> seeds(grid.cells(), 1);
					std::vector<EntrySteps> costs(grid.cells(), LAND_STEPS);
					for (std::size_t i = 0; i < grid.cells(); ++i)
					{
						if (i % 5 == 2)
						{
							if (!swim)
								seeds[i] = 0;
							else
								costs[i] = entrySteps(WATER_STEP[swim]);
						}
						if (i % 13 == 4)
							seeds[i] = 0;
					}
					seeds.front() = 65535;
					seeds[grid.cells() / 2] = 65535 - 71;
					if (cap == 70)
					{
						auto edges = std::make_shared<std::vector<std::uint8_t>>(grid.cells());
						for (auto &mask : *edges)
							mask = random() & 255;
						grid = grid.withConnections(edges);
					}
					seeds.back() = 65535 - 400;
					const auto expected = oracle(seeds, grid, costs, cap);
					for (const auto plan : {Plan::VulkanFrozen8, Plan::VulkanJacobi})
					{
						const auto planID = int(plan);
						CAPTURE(planID);
						auto actual = seeds;
						BackendRequest request{actual.data(), cap, grid, session, &costs,
							[](void *p, auto i) { return (*static_cast<std::vector<EntrySteps> *>(p))[i]; },
							nullptr};
						bool committed = false, observed = false;
						request.executedOnDevice = &committed;
						request.deviceExecutionObserved = &observed;
						REQUIRE(executeVulkanGradient(std::span(&request, 1), plan));
						REQUIRE(observed);
						REQUIRE(committed);
						CHECK(actual == expected);
					}
				}
	}
	TEST_CASE("retained Vulkan proof inputs require exact device commits")
	{
		using namespace gradient_kernel;
		if (!initializeVulkanGradient())
		{
			if (std::getenv("GLOB2_REQUIRE_VULKAN"))
				FAIL(vulkanGradientStatus().error);
			MESSAGE("Vulkan unavailable: ", vulkanGradientStatus().error);
			return;
		}
		const auto device = vulkanGradientStatus();
		MESSAGE("Vulkan retained proofs: device=", device.device, " driver=", device.driver,
				" shader=", device.shaderIdentity);
		const field::Grid grid{gradient_fixture::retainedVulkanWidth,
			gradient_fixture::retainedVulkanHeight};
		const int cap = gradient_fixture::retainedVulkanCap;
		BackendSession session;
		const std::array<std::span<const std::uint16_t>, 5> fixtures{
			gradient_fixture::retainedVulkanRuns0, gradient_fixture::retainedVulkanRuns1,
			gradient_fixture::retainedVulkanRuns2, gradient_fixture::retainedVulkanRuns3,
			gradient_fixture::retainedVulkanRuns4};
		for (std::size_t fixture = 0; fixture < fixtures.size(); ++fixture)
		{
			CAPTURE(fixture);
			std::vector<std::uint16_t> seeds;
			for (const auto run : fixtures[fixture])
				seeds.insert(seeds.end(), run >> 2, (run & 3) == 2 ? 65535 : run & 3);
			REQUIRE(seeds.size() == grid.cells());
			const std::vector<EntrySteps> costs(grid.cells(), LAND_STEPS);
			const auto expected = oracle(seeds, grid, costs, cap);
			for (const auto plan : {Plan::VulkanFrozen8, Plan::VulkanJacobi})
				{
					const auto planID = int(plan);
					CAPTURE(planID);
					for (int repeat = 0; repeat < 3; ++repeat)
					{
						CAPTURE(repeat);
						auto actual = seeds;
						BackendRequest request{actual.data(), cap, grid, session, nullptr,
							[](void *, auto) { return LAND_STEPS; }, nullptr};
						bool committed = false, observed = false;
						request.executedOnDevice = &committed;
						request.deviceExecutionObserved = &observed;
						const auto before = vulkanGradientStatus();
						REQUIRE(executeVulkanGradient(std::span(&request, 1), plan));
						REQUIRE(observed);
						REQUIRE(committed);
						CHECK(vulkanGradientStatus().fields == before.fields + 1);
						CHECK(vulkanGradientStatus().dispatches > before.dispatches);
						CHECK(actual == expected);
					}
				}
		}
	}

	TEST_CASE("Vulkan storage reuse preserves raw outputs at cohort sizes")
	{
		using namespace gradient_kernel;
		if (!initializeVulkanGradient())
		{
			if (std::getenv("GLOB2_REQUIRE_VULKAN"))
				FAIL(vulkanGradientStatus().error);
			MESSAGE("Vulkan unavailable: ", vulkanGradientStatus().error);
			return;
		}
		BackendSession session;
		for (const int side : {128, 256, 512})
			for (unsigned repeat = 0; repeat < 8; ++repeat)
				for (const auto plan : {Plan::VulkanFrozen8, Plan::VulkanJacobi})
				{
					CAPTURE(side);
					CAPTURE(repeat);
					const auto planID = int(plan);
					CAPTURE(planID);
					const field::Grid grid{side, side};
					std::vector<std::uint16_t> seeds(grid.cells());
					for (std::size_t i = 0; i < grid.cells(); ++i)
						seeds[i] = (i + repeat) % 17 == 0 ? 65535 : (i + repeat) % 5 == 0 ? 0 : 1;
					auto actual = seeds;
					EntrySteps costs{1 + repeat * 17, 2 + repeat * 23};
					BackendRequest request{actual.data(), 0, grid, session, &costs,
						[](void *p, auto) { return *static_cast<EntrySteps *>(p); }, nullptr};
					bool committed = false, observed = false;
					request.executedOnDevice = &committed;
					request.deviceExecutionObserved = &observed;
					REQUIRE(executeVulkanGradient(std::span(&request, 1), plan));
					REQUIRE(observed);
					REQUIRE(committed);
					// With cap zero no positive edge can extend any original source.
					CHECK(actual == seeds);
				}
	}
	TEST_CASE("Vulkan retires one pooled work set before reusing buffers across shapes and algorithms")
    {
        using namespace gradient_kernel;
        glob2test::ScopedEnvironment diagnostics("GLOB2_GRADIENT_DIAGNOSTICS", "1");
        glob2test::ScopedEnvironment transfers("GLOB2_VULKAN_VERIFY_TRANSFERS", "1");
        shutdownVulkanGradient();
        if (!initializeVulkanGradient()) {
            if (std::getenv("GLOB2_REQUIRE_VULKAN")) FAIL(vulkanGradientStatus().error);
            MESSAGE("Vulkan unavailable: ", vulkanGradientStatus().error); return;
        }
        BackendSession session;
        const auto hostBefore = openCLHostBytes();
        unsigned iteration = 0;
        for (const auto& dimensions : {std::pair{128,128}, std::pair{256,256}, std::pair{512,512},
                std::pair{17,19}, std::pair{1,192}, std::pair{192,1}, std::pair{7,13}}) {
            field::Grid grid{dimensions.first, dimensions.second};
            std::vector<EntrySteps> costs(grid.cells());
            std::vector<std::uint16_t> seeds(grid.cells(), 1);
            for (std::size_t i=0; i<grid.cells(); ++i) {
                costs[i] = {unsigned(1+(i+iteration)%97),unsigned(2+(i+iteration)%137)};
                if (i%11==3) seeds[i]=0;
            }
            seeds.front()=65535; seeds.back()=65000;
            const auto expected=oracle(seeds,grid,costs,2000);
            for (const auto plan : {Plan::VulkanFrozen8,Plan::VulkanJacobi}) {
                auto actual=seeds;
                BackendRequest request{actual.data(),2000,grid,session,&costs,
                    [](void* p,std::size_t i){return (*static_cast<std::vector<EntrySteps>*>(p))[i];},nullptr};
                bool committed=false; request.executedOnDevice=&committed;
                REQUIRE(executeVulkanGradient(std::span(&request,1),plan));
                CHECK(committed); CHECK(actual==expected);
            }
            ++iteration;
        }
        CHECK(openCLHostBytes() >= hostBefore+512*512*sizeof(std::uint32_t));
        shutdownVulkanGradient();
        CHECK(openCLHostBytes()==hostBefore);
    }
	TEST_CASE("Vulkan retained work storage shares the host admission budget")
	{
		using namespace gradient_kernel;
        shutdownVulkanGradient();
		if (!initializeVulkanGradient())
		{
			if (std::getenv("GLOB2_REQUIRE_VULKAN"))
				FAIL(vulkanGradientStatus().error);
			MESSAGE("Vulkan unavailable: ", vulkanGradientStatus().error);
			return;
		}
		BackendSession session;
		const field::Grid grid{17, 19};
		std::vector<std::uint16_t> seeds(grid.cells(), 1);
		seeds.front() = 65535;
		auto actual = seeds;
		BackendRequest request{actual.data(), COST_LIMIT, grid, session, nullptr,
			[](void *, auto) { return LAND_STEPS; }, nullptr};
		bool committed = false, observed = false;
		request.executedOnDevice = &committed;
		request.deviceExecutionObserved = &observed;
		const auto before = openCLHostBytes();
		REQUIRE(before + grid.cells() * 2 < OpenCLHostBudget);
		{
			// Leave room for the immutable seed backup, but no mapped storage.
			const auto bytes = OpenCLHostBudget - before - grid.cells() * 2;
			REQUIRE(reserveOpenCLHostBytes(bytes));
			struct Lease { std::size_t bytes; ~Lease() { releaseOpenCLHostBytes(bytes); } } lease{bytes};
			CHECK_FALSE(executeVulkanGradient(std::span(&request, 1), Plan::VulkanFrozen8));
			CHECK_FALSE(committed);
			CHECK_FALSE(observed);
			CHECK(actual == seeds);
			CHECK(openCLHostBytes() == before + bytes);
		}
		CHECK(openCLHostBytes() == before);
		REQUIRE(executeVulkanGradient(std::span(&request, 1), Plan::VulkanFrozen8));
		CHECK(committed);
		CHECK(observed);
		CHECK(openCLHostBytes() > before);
        shutdownVulkanGradient();
        CHECK(openCLHostBytes() == before);
	}
	TEST_CASE("Vulkan batch rollback retains execution evidence without claiming commits")
	{
		using namespace gradient_kernel;
		if (!initializeVulkanGradient())
		{
			if (std::getenv("GLOB2_REQUIRE_VULKAN"))
				FAIL(vulkanGradientStatus().error);
			MESSAGE("Vulkan unavailable: ", vulkanGradientStatus().error);
			return;
		}
		BackendSession session;
		for (const auto plan : {Plan::VulkanFrozen8, Plan::VulkanJacobi})
		{
			const field::Grid grid{17, 19};
			std::vector<std::uint16_t> seeds(grid.cells(), 1);
			seeds[0] = 65535;
			auto first = seeds, second = seeds;
			std::array<BackendRequest, 2> requests{
				BackendRequest{first.data(), COST_LIMIT, grid, session, nullptr,
					[](void *, auto) { return LAND_STEPS; }, nullptr},
				BackendRequest{second.data(), COST_LIMIT, grid, session, nullptr,
					[](void *, auto) { return LAND_STEPS; }, nullptr}};
			requests[1].requiredControl = std::make_shared<OpenCLRequiredControl>();
			requests[1].requiredControl->requestCancel();
			std::array<bool, 2> committed{true, true}, observed{true, true};
			for (unsigned i = 0; i < 2; ++i)
			{
                requests[i].executedOnDevice = &committed[i];
				requests[i].deviceExecutionObserved = &observed[i];
			}
			const auto before = vulkanGradientStatus();
			CHECK_FALSE(executeVulkanGradient(requests, plan));
			CHECK(observed[0]);
			CHECK_FALSE(observed[1]);
			CHECK_FALSE(committed[0]);
			CHECK_FALSE(committed[1]);
			CHECK(first == seeds);
			CHECK(second == seeds);
			CHECK(vulkanGradientStatus().fields == before.fields);
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
			field::Grid grid{dimensions.first, dimensions.second};
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
		values[1] = 1;
		auto other = request;
		other.cpuBatch = nullptr;
		std::array group{request, other};
		CHECK_THROWS_AS(executeGradientGroup(group, Backend::Scalar), std::invalid_argument);
		CHECK(calls == 1);
		CHECK(values[1] == 1);
	}
}

TEST_SUITE("RuntimeGradientBackend")
{

}
