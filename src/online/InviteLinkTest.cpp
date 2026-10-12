// SPDX-License-Identifier: GPL-3.0-or-later
// Invite link forms and the pending join.

#include "Glob2Test.h"
#include "InstanceConfig.h"
#include "InviteLink.h"
#include "CommandLine.h"
#include "OnlineHandoff.h"

using namespace Online;

TEST_SUITE("InviteLink")
{
	TEST_CASE("catalog launches preserve mode origin and exact version across URL forms")
	{
		const std::string query = "map=5f6a7b8c-9d0e-4f1a-8b2c-3d4e5f6a7b8c&version=" + std::string(64, 'a') + "&title=Two+%26+Three";
		for (const auto &prefix : {std::string("glob2://play?instance=https%3A%2F%2Fplay.example.org&"), std::string("https://play.example.org/play/?")})
			for (const auto &mode : {std::string("local"), std::string("multiplayer")})
			{
				REQUIRE(acceptInviteText(prefix + query + "&mode=" + mode));
				REQUIRE(pendingMapPlay());
				CHECK_EQ(pendingMapPlay()->origin, "https://play.example.org");
				CHECK_EQ(pendingMapPlay()->map.hash, std::string(64, 'a'));
				CHECK_EQ(pendingMapPlay()->map.title, "Two & Three");
				CHECK(pendingMapPlay()->mode == (mode == "local" ? MapPlayRequest::Mode::Local : MapPlayRequest::Mode::Multiplayer));
				REQUIRE(takePendingMapPlay());
				CHECK_FALSE(pendingMapPlay());
			}
		REQUIRE(acceptMapPlayText("https://play.example.org/play/?" + query));
		CHECK(pendingMapPlay()->mode == MapPlayRequest::Mode::Multiplayer);
		takePendingMapPlay();
		for (const auto &bad : {"http://untrusted.example/play/?" + query, "https://user@play.example.org/play/?" + query,
			"https://play.example.org/play/?" + query + "&mode=unknown", "glob2://play?" + query + "&instance=%ZZ",
			"https://play.example.org/play/?map=x&version=" + std::string(64, 'a')})
			CHECK_FALSE(acceptMapPlayText(bad));
		CHECK_FALSE(pendingMapPlay());
	}

	TEST_CASE("catalog CLI arguments open the selected destination")
	{
        auto request=Cli::parse({"online","play-map","5f6a7b8c-9d0e-4f1a-8b2c-3d4e5f6a7b8c","--hash",std::string(64,'a'),"--title","Map","--instance","https://play.example.org"});
        acceptLaunchRequest(request);
		REQUIRE(pendingMapPlay());
		CHECK(pendingMapPlay()->mode == MapPlayRequest::Mode::Local);
		CHECK_EQ(pendingMapPlay()->origin, "https://play.example.org");
		takePendingMapPlay();
        request.command="online host-map";
        acceptLaunchRequest(request);
		CHECK(pendingMapPlay()->mode == MapPlayRequest::Mode::Multiplayer);
		takePendingMapPlay();
	}

	TEST_CASE("the latest invite or map launch replaces the older destination")
	{
		setPendingJoin({"https://play.example.org", "ABCDEF"});
		setRoomMapHandler({});
		CHECK_FALSE(useMapInRoom({"older", std::string(64, 'b'), "Earlier map"}));
		REQUIRE(pendingRoomMap());
		const std::string link = "glob2://play?map=5f6a7b8c-9d0e-4f1a-8b2c-3d4e5f6a7b8c&version=" + std::string(64, 'a') + "&mode=local";
        acceptLaunchRequest(Cli::parse({link}));
		REQUIRE(pendingMapPlay());
		CHECK_FALSE(pendingJoin());
		CHECK_FALSE(pendingRoomMap());
		REQUIRE(acceptInviteText("glob2://join?code=ABCDEF"));
		CHECK_FALSE(pendingMapPlay());
		CHECK_EQ(pendingJoin()->code, "ABCDEF");
		clearPendingJoin();
	}

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

    TEST_CASE("launch requests accept links codes and origins and reject invalid invites")
    {
        clearPendingJoin();
        acceptLaunchRequest(Cli::parse({"glob2://join?instance=https://b.example&code=BBBBBB"}));
        CHECK_EQ(takePendingJoin()->origin,"https://b.example");
        for(const auto &link:{"http://localhost:8080/j/ABCDEF", "GLOB2:join?code=ABCDEF", "HTTPS://b.example/j/ABCDEF"}) {
            acceptLaunchRequest(Cli::parse({link}));
            CHECK_EQ(takePendingJoin()->code,"ABCDEF");
        }
        acceptLaunchRequest(Cli::parse({"online","join","CCCCCC","--instance","https://c.example:8443"}));
        const auto invite=takePendingJoin();REQUIRE(invite);
        CHECK_EQ(invite->origin,"https://c.example:8443");CHECK_EQ(invite->code,"CCCCCC");
        acceptLaunchRequest(Cli::parse({"online","join","DDDDDD"}));
        CHECK_EQ(takePendingJoin()->origin,OFFICIAL_INSTANCE_ORIGIN);
        CHECK_THROWS_AS(acceptLaunchRequest(Cli::parse({"online","join","no!"})),std::invalid_argument);
        CHECK_FALSE(pendingJoin());
    }
}
