// SPDX-License-Identifier: GPL-3.0-or-later
// Triple-buffer handoff between the simulation thread and the render thread.

#include "Glob2Test.h"

#include "SceneBuffer.h"
#include "ThreadSupport.h"

#include <thread>
#include <vector>

namespace
{
	struct Frame
	{
		unsigned tick = 0;
		std::vector<unsigned> payload; // every entry equals tick: detects torn values
	};

	void fill(Frame &frame, unsigned tick)
	{
		frame.tick = tick;
		frame.payload.assign(64, tick);
	}
}

TEST_SUITE("SceneBuffer")
{
	TEST_CASE("the consumer sees the newest published value and nothing before the first publish")
	{
		SceneBuffer<Frame> buffer;
		CHECK_FALSE(buffer.acquire());
		CHECK_FALSE(buffer.ready());
		CHECK_FALSE(buffer.pending());
		fill(buffer.back(), 1);
		buffer.publish();
		CHECK(buffer.pending());
		fill(buffer.back(), 2);
		buffer.publish();
		REQUIRE(buffer.acquire());
		CHECK(buffer.current().tick == 2); // 1 was superseded before it was taken
		CHECK_FALSE(buffer.pending());
		CHECK_FALSE(buffer.acquire());     // nothing new: current() stays
		CHECK(buffer.current().tick == 2);
		fill(buffer.back(), 3);
		CHECK(buffer.current().tick == 2); // building never disturbs the consumer
		buffer.publish();
		REQUIRE(buffer.acquire());
		CHECK(buffer.current().tick == 3);
	}

	TEST_CASE("concurrent producer and consumer never observe torn or out-of-order values")
	{
        if constexpr (!GAGCore::ThreadSupport::available) return;
		SceneBuffer<Frame> buffer;
		constexpr unsigned last = 200000;
		std::thread producer([&] {
			for (unsigned tick = 1; tick <= last; ++tick)
			{
				fill(buffer.back(), tick);
				buffer.publish();
			}
		});
		unsigned seen = 0, acquisitions = 0;
		bool torn = false, backwards = false;
		while (seen < last)
		{
			if (!buffer.acquire())
				continue;
			const Frame &frame = buffer.current();
			for (unsigned value : frame.payload)
				torn = torn || value != frame.tick;
			backwards = backwards || frame.tick <= seen;
			seen = frame.tick;
			++acquisitions;
		}
		producer.join();
		CHECK_FALSE(torn);
		CHECK_FALSE(backwards);
		CHECK(acquisitions > 0);
	}

}
