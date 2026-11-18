// SPDX-License-Identifier: GPL-3.0-or-later
// HeadlessGlobals leaves nothing process-wide behind for the next case in the same
// process: not when it closes normally, and not when its constructor throws.
#include "HeadlessGlobals.h"

#include "FileManager.h"
#include "MapCache.h"
#include "OnlineServices.h"
#include "OnlineStorage.h"

#include <stdexcept>
#include <string>

TEST_SUITE("HeadlessGlobals")
{
	TEST_CASE("a constructor that throws leaves no live global container")
	{
		glob2test::GlobalsOptions options;
		options.beforeLoad = [](GlobalContainer &) { throw std::runtime_error("load failed"); };
		CHECK_THROWS_WITH(glob2test::HeadlessGlobals{options}, "load failed");
		CHECK(globalContainer == nullptr);
		// The next fixture in this process starts normally.
		glob2test::HeadlessGlobals globals;
		CHECK(globalContainer == &globals.globals);
	}

	TEST_CASE("online storage follows each fixture's user directory")
	{
		const std::string name = "online/fixture-probe";
		{
			glob2test::HeadlessGlobals first;
			Online::sharedMapCache();
			CHECK(Online::services().storage.write(name, "first"));
			CHECK(Online::servicesCreated());
		}
		glob2test::HeadlessGlobals second;
		// The first fixture's services, bound to its closed file manager, are gone.
		CHECK_FALSE(Online::servicesCreated());
		auto &storage = Online::services().storage;
		std::string contents;
		REQUIRE(storage.write(name, "second"));
		REQUIRE(storage.read(name, contents));
		CHECK(contents == "second");
		storage.remove(name);
	}
}
