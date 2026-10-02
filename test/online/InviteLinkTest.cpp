// SPDX-License-Identifier: GPL-3.0-or-later
// Invite link forms and the pending join.

#include "Glob2Test.h"
#include "InstanceConfig.h"
#include "InviteLink.h"

using namespace Online;

TEST_SUITE("InviteLink")
{
	TEST_CASE("glob2:// links name the instance and code")
	{
		auto invite = parseInviteLink("glob2://join?instance=https%3A%2F%2Fplay.example.org%3A8443&code=Ab12Cd34");
		REQUIRE(invite.has_value());
		CHECK_EQ(invite->origin, "https://play.example.org:8443");
		CHECK_EQ(invite->code, "Ab12Cd34");
		CHECK_EQ(parseInviteLink("glob2://join?code=ABCDEF&instance=https://x.example")->origin,
				 "https://x.example");
		CHECK_EQ(parseInviteLink("GLOB2://JOIN/?instance=https://x.example&code=ABCDEF")->code, "ABCDEF");
		CHECK_EQ(parseInviteLink("glob2:join?instance=https://x.example&code=ABCDEF\n")->code, "ABCDEF");
		// Without an instance, the official one.
		CHECK_EQ(parseInviteLink("glob2://join?code=ABCDEF")->origin, OFFICIAL_INSTANCE_ORIGIN);
		for (const char *bad :
			 {"glob2://join", "glob2://join?instance=https://x.example", "glob2://host?code=ABCDEF",
			  "glob2://join?code=ABC", "glob2://join?code=ABCDEFGHIJKLMNOPQ", "glob2://join?code=ABC-DEF",
			  "glob2://join?code=ABCDEF&instance=http://evil.example",
			  "glob2://join?code=ABCDEF&instance=https://x.example/path",
			  "glob2://join?code=ABCDEF&instance=%ZZ", "glob2://join?code=AB%20CDEF",
			  "glob2://join?code=ABCDEF&instance=https://x.example\x01"})
			CHECK_MESSAGE(!parseInviteLink(bad).has_value(), bad);
	}

	TEST_CASE("https links are <instance>/j/<code>")
	{
		auto invite = parseInviteLink("https://Play.Example.org/j/XyZ12345");
		REQUIRE(invite.has_value());
		CHECK_EQ(invite->origin, "https://play.example.org");
		CHECK_EQ(invite->code, "XyZ12345");
		CHECK_EQ(parseInviteLink("https://play.example.org:8443/j/ABCDEF/?utm=1#x")->origin,
				 "https://play.example.org:8443");
		CHECK_EQ(parseInviteLink("http://localhost:8080/j/ABCDEF")->origin, "http://localhost:8080");
		for (const char *bad :
			 {"https://play.example.org", "https://play.example.org/", "https://play.example.org/j/",
			  "https://play.example.org/x/ABCDEF", "https://play.example.org/j/ABCDEF/more",
			  "http://play.example.org/j/ABCDEF", "https://user@play.example.org/j/ABCDEF",
			  "/j/ABCDEF", "ABCDEF", "file:///j/ABCDEF"})
			CHECK_MESSAGE(!parseInviteLink(bad).has_value(), bad);
	}

	TEST_CASE("former official origins mean the official instance")
	{
		// The default build: the app moved from the apex to app.glob2online.com.
		if (std::string(OFFICIAL_INSTANCE_ORIGIN) != "https://app.glob2online.com")
			return;
		for (const char *link :
			 {"https://glob2online.com/j/KXQ742MNPR", "https://GLOB2ONLINE.com/j/KXQ742MNPR/",
			  "https://app.glob2online.com/j/KXQ742MNPR",
			  "glob2://join?instance=https%3A%2F%2Fglob2online.com&code=KXQ742MNPR"})
		{
			auto invite = parseInviteLink(link);
			REQUIRE_MESSAGE(invite.has_value(), link);
			CHECK_EQ(invite->origin, "https://app.glob2online.com");
			CHECK_EQ(invite->code, "KXQ742MNPR");
		}
		CHECK_EQ(parseInvite("KXQ742MNPR", "https://glob2online.com")->origin,
				 "https://app.glob2online.com");
		// Only the exact former origin: other hosts and ports are not the app.
		CHECK_EQ(parseInviteLink("https://www.glob2online.com/j/KXQ742MNPR")->origin,
				 "https://www.glob2online.com");
		CHECK_EQ(parseInviteLink("https://glob2online.com:8443/j/KXQ742MNPR")->origin,
				 "https://glob2online.com:8443");
	}

	TEST_CASE("bare codes use the given instance; links format back")
	{
		CHECK_EQ(parseInvite("ABCDEF12", "https://x.example")->origin, "https://x.example");
		CHECK_FALSE(parseInvite("ABCDEF12", "http://x.example").has_value());
		CHECK_EQ(parseInvite("https://y.example/j/ABCDEF12", "https://x.example")->origin,
				 "https://y.example");
		InviteLink invite{"https://play.example.org:8443", "Ab12Cd34"};
		CHECK_EQ(formatWebLink(invite), "https://play.example.org:8443/j/Ab12Cd34");
		CHECK_EQ(formatSchemeLink(invite),
				 "glob2://join?instance=https%3A%2F%2Fplay.example.org%3A8443&code=Ab12Cd34");
		CHECK_EQ(parseInviteLink(formatSchemeLink(invite)).value(), invite);
		CHECK_EQ(parseInviteLink(formatWebLink(invite)).value(), invite);
	}

	TEST_CASE("the newest link becomes the pending join until taken")
	{
		clearPendingJoin();
		CHECK_FALSE(pendingJoin().has_value());
		CHECK_FALSE(acceptInviteText("/Users/me/maps/My Map.map"));
		CHECK_FALSE(pendingJoin().has_value());
		CHECK(acceptInviteText("https://a.example/j/AAAAAA"));
		CHECK(acceptInviteText("glob2://join?instance=https://b.example&code=BBBBBB"));
		CHECK_EQ(pendingJoin()->code, "BBBBBB");
		auto taken = takePendingJoin();
		REQUIRE(taken.has_value());
		CHECK_EQ(taken->origin, "https://b.example");
		CHECK_FALSE(takePendingJoin().has_value());
	}

	TEST_CASE("launch arguments: bare links, --join and --instance")
	{
		clearPendingJoin();
		const char *bare[] = {"glob2", "-s", "glob2://join?instance=https://b.example&code=BBBBBB"};
		CHECK_EQ(acceptLaunchArguments(3, const_cast<char **>(bare), 1), 0);
		CHECK_EQ(acceptLaunchArguments(3, const_cast<char **>(bare), 2), 1);
		CHECK_EQ(takePendingJoin()->origin, "https://b.example");

		const char *coded[] = {"glob2", "--join", "CCCCCC", "--instance", "https://c.example:8443"};
		CHECK_EQ(acceptLaunchArguments(5, const_cast<char **>(coded), 1), 2);
		CHECK_EQ(acceptLaunchArguments(5, const_cast<char **>(coded), 3), 2);
		auto invite = takePendingJoin();
		REQUIRE(invite.has_value());
		CHECK_EQ(invite->origin, "https://c.example:8443");
		CHECK_EQ(invite->code, "CCCCCC");

		const char *official[] = {"glob2", "--join", "DDDDDD"};
		CHECK_EQ(acceptLaunchArguments(3, const_cast<char **>(official), 1), 2);
		CHECK_EQ(takePendingJoin()->origin, OFFICIAL_INSTANCE_ORIGIN);

		const char *invalid[] = {"glob2", "--join", "no!"};
		CHECK_EQ(acceptLaunchArguments(3, const_cast<char **>(invalid), 1), 2);
		CHECK_FALSE(pendingJoin().has_value());
	}
}
