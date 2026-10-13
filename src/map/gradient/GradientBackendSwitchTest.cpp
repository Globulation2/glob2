// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientRuntime.h"
#include "field/GradientSelection.h"

TEST_CASE("backend switch drains privately and preserves pending publication" *
		  doctest::test_suite("GradientBackendSwitch"))
{
	using namespace gradient_kernel;
	const auto previous = configuration(), requested = requestedBackendConfiguration();
	struct Restore
	{
		Configuration applied, requested;
		~Restore()
		{
			applyBackendConfiguration(applied);
			requestBackendConfiguration(requested);
		}
	} restore{previous, requested};
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame fixture({.wDec = 5,
									 .hDec = 5,
									 .teams = 1,
									 .discovered = true,
									 .clearImmobile = true,
									 .header = true});
	auto &map = fixture.game.map;
	setBackend(Backend::Scalar);
	map.configureCompute(2);
	auto &pipeline = map.gradientRuntime->pipeline;
	pipeline.onPublished = {};
	std::atomic<Backend> observed{Backend::CPU};
	pipeline.configure(map.computeExecutor(), true, 3, 1,
					   [&](auto &job, auto &)
					   {
						   observed = backend();
						   job.data[0] += 1;
					   });
	auto *slot = new std::uint16_t[1]{};
	pipeline.advance();
	pipeline.submit(&slot, 0, [](auto &job) { job.data[0] = 41; });
	requestBackendConfiguration({Backend::CPU, Algorithm::Buckets, false});
	CHECK(backend() == Backend::Scalar);
	map.applyGradientBackendRequest();
	CHECK(observed.load() == Backend::Scalar);
	CHECK(backend() == Backend::CPU);
	CHECK(slot[0] == 0);
	CHECK(pipeline.pendingCount() == 1);
	CHECK(pipeline.delayTicks() == 3);
	pipeline.advance();
	pipeline.advance();
	CHECK(slot[0] == 0);
	pipeline.advance();
	CHECK(slot[0] == 42);
	CHECK(pipeline.metrics.published == 1);
	pipeline.submit(&slot, 0, [](auto &job) { job.data[0] = 99; });
	pipeline.finish();
	CHECK(observed.load() == Backend::CPU);
	requestBackendConfiguration({Backend::Scalar, Algorithm::Buckets, false});
	map.applyGradientBackendRequest();
	CHECK(slot[0] == 42);
	CHECK(pipeline.pendingCount() == 1);
	pipeline.advance();
	pipeline.advance();
	pipeline.advance();
	CHECK(slot[0] == 100);
	pipeline.reset();
	delete[] slot;
}

TEST_CASE("repeated backend requests coalesce without moving publication ticks" *
		  doctest::test_suite("GradientBackendSwitch"))
{
	using namespace gradient_kernel;
	struct Restore
	{
		Configuration applied = configuration(), requested = requestedBackendConfiguration();
		~Restore()
		{
			applyBackendConfiguration(applied);
			requestBackendConfiguration(requested);
		}
	} restore;
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame fixture({.wDec = 5,
									 .hDec = 5,
									 .teams = 1,
									 .discovered = true,
									 .clearImmobile = true,
									 .header = true});
	auto &map = fixture.game.map;
	setBackend(Backend::Scalar);
	map.configureCompute(2);
	auto &pipeline = map.gradientRuntime->pipeline;
	pipeline.onPublished = {};
	pipeline.configure(map.computeExecutor(), true, 4, 1, [](auto &job, auto &) { ++job.data[0]; });
	auto *serving = new std::uint16_t[1]{7};
	pipeline.advance();
	pipeline.submit(&serving, 0, [](auto &job) { job.data[0] = 20; });
	const auto before = map.gradientRuntime->backendSession->currentGeneration();
	requestBackendConfiguration({Backend::CPU, Algorithm::Buckets, false});
	requestBackendConfiguration({Backend::Scalar, Algorithm::Buckets, false});
	map.applyGradientBackendRequest(); // Last request cancels the earlier change.
	CHECK(map.gradientRuntime->backendSession->currentGeneration() == before);
	CHECK(serving[0] == 7);
	CHECK(pipeline.pendingCount() == 1);
	map.gradientRuntime->backendSession->failed = true;
	requestBackendConfiguration({Backend::CPU, Algorithm::Buckets, false});
	map.applyGradientBackendRequest();
	CHECK_FALSE(map.gradientRuntime->backendSession->failed.load());
	const auto applied = map.gradientRuntime->backendSession->currentGeneration();
	CHECK(applied != before);
	CHECK(serving[0] == 7);
	CHECK(pipeline.pendingCount() == 1);
	map.applyGradientBackendRequest();
	CHECK(map.gradientRuntime->backendSession->currentGeneration() == applied);
	for (unsigned i = 0; i < 3; ++i)
	{
		pipeline.advance();
		CHECK(serving[0] == 7);
	}
	pipeline.advance();
	CHECK(serving[0] == 21);
	CHECK(pipeline.metrics.published == 1);
	CHECK(pipeline.delayTicks() == 4);
	pipeline.reset();
	delete[] serving;
}
