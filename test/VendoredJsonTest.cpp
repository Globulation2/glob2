// SPDX-License-Identifier: GPL-3.0-or-later
// The vendored nlohmann/json header is reachable from every target's include
// path and behaves as platform messages need.

#include "Glob2Test.h"

#include <nlohmann/json.hpp>
#include <string>

TEST_SUITE("VendoredJson")
{
	TEST_CASE("nlohmann/json round-trips the shapes platform messages use")
	{
		const auto message = nlohmann::json::parse(
			R"({"type":"room.state","id":7,"seats":[{"seat":0,"ai":null},{"seat":1,"ai":"nicowar"}],"name":"Caf\u00e9"})");
		CHECK_EQ(message["type"].get<std::string>(), "room.state");
		CHECK_EQ(message["id"].get<int>(), 7);
		CHECK(message["seats"][0]["ai"].is_null());
		CHECK_EQ(message["name"].get<std::string>(), "Caf\xc3\xa9");
		CHECK_EQ(nlohmann::json::parse(message.dump()), message);
		CHECK_THROWS_AS(static_cast<void>(nlohmann::json::parse("{\"unterminated\":")), nlohmann::json::parse_error);
	}
}
