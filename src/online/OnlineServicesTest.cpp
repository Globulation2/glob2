// SPDX-License-Identifier: GPL-3.0-or-later
// The application's online services: owned for a run (ServicesOwner), reached
// through Online::services() while the owner lives, destroyed with it, and
// pump hooks that can be removed.

#include "Glob2Test.h"
#include "InstanceConfig.h"
#include "MapCache.h"
#include "OnlineServices.h"
#include "OnlineStorage.h"
#include "PlatformClient.h"
#include "QuickMatch.h"

#include <memory>

using namespace Online;

namespace
{
struct Files
{
	MemoryStorage storage;
	MapCache maps{storage, [](HttpFetch::Request) -> std::unique_ptr<HttpFetch::Fetch> { return nullptr; }};
};
} // namespace

TEST_SUITE("OnlineServices")
{
	TEST_CASE("an owner's services are the ones services() returns, until the owner goes")
	{
		Files files;
		int created = 0;
		auto make = [&] {
			++created;
			return std::make_unique<Services>(files.storage, files.maps);
		};
		{
			ServicesOwner owner(make);
			CHECK_FALSE(owner.created());
			CHECK_FALSE(servicesCreated());
			Services &first = services();
			CHECK(created == 1);
			CHECK(owner.created());
			CHECK(servicesCreated());
			CHECK(&services() == &first);
			CHECK(&first.maps == &files.maps);
			CHECK(&first.storage == &files.storage);
			CHECK(&first.quickMatch() == &first.quickMatch());
			{
				// A nested owner (a second run) gets its own services.
				ServicesOwner inner(make);
				CHECK(&services() != &first);
				CHECK(created == 2);
			}
			CHECK(&services() == &first);
		}
		CHECK(created == 2);
	}

	TEST_CASE("pump hooks run on update and stop once removed")
	{
		Files files;
		Services online(files.storage, files.maps);
		int a = 0, b = 0;
		const auto first = online.addHook([&] { ++a; });
		online.addHook([&] { ++b; });
		online.update();
		CHECK(a == 1);
		CHECK(b == 1);
		online.removeHook(first);
		online.update();
		CHECK(a == 1);
		CHECK(b == 2);
		// A hook may remove itself while running.
		Services::HookId self = 0;
		self = online.addHook([&] { online.removeHook(self); ++a; });
		online.update();
		online.update();
		CHECK(a == 2);
		CHECK(b == 4);
	}
}
