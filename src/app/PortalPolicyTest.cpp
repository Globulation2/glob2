// SPDX-License-Identifier: GPL-3.0-or-later
// Portal mode (src/app/PortalPolicy.h): reading window.GLOB2_PORTAL's JSON, and
// installing a policy into ApplicationHost's link switch.
#include "Glob2Test.h"
#include "PortalPolicy.h"

#include <ApplicationHost.h>
#include <SDL3/SDL.h>

#include <string>

using PortalPolicy::parse;

TEST_SUITE("PortalPolicy")
{
	TEST_CASE("no portal description allows everything")
	{
		const auto policy = parse("");
		CHECK_FALSE(policy.portal);
		CHECK(policy.externalLinks);
		CHECK(policy.onlineSignIn);
		CHECK(policy.name.empty());
	}

	TEST_CASE("a host without GLOB2_PORTAL reports no portal and allows everything")
	{
		// Our own site, itch, desktop and mobile: nothing set, nothing changes.
		const char *saved = SDL_getenv("GLOB2_PORTAL");
		const std::string previous = saved ? saved : "";
		SDL_unsetenv_unsafe("GLOB2_PORTAL");
		CHECK(GAGCore::ApplicationHost::portalDescription().empty());
		const auto policy = parse(GAGCore::ApplicationHost::portalDescription());
		CHECK_FALSE(policy.portal);
		CHECK(policy.externalLinks);
		CHECK(policy.onlineSignIn);
		if (saved)
			SDL_setenv_unsafe("GLOB2_PORTAL", previous.c_str(), 1);
	}

	TEST_CASE("a complete description is read as written")
	{
		const auto crazy = parse(R"({"name":"crazygames","externalLinks":false,"onlineSignIn":false})");
		CHECK(crazy.portal);
		CHECK_EQ(crazy.name, "crazygames");
		CHECK_FALSE(crazy.externalLinks);
		CHECK_FALSE(crazy.onlineSignIn);

		const auto newgrounds = parse(R"({"name":"newgrounds","externalLinks":true,"onlineSignIn":true})");
		CHECK(newgrounds.portal);
		CHECK_EQ(newgrounds.name, "newgrounds");
		CHECK(newgrounds.externalLinks);
		CHECK(newgrounds.onlineSignIn);

		const auto mixed = parse(R"({"name":"p","externalLinks":false,"onlineSignIn":true,"extra":[1]})");
		CHECK_FALSE(mixed.externalLinks);
		CHECK(mixed.onlineSignIn);
	}

	TEST_CASE("a partial description turns off what it leaves out")
	{
		const auto linksOnly = parse(R"({"name":"p","externalLinks":true})");
		CHECK(linksOnly.portal);
		CHECK(linksOnly.externalLinks);
		CHECK_FALSE(linksOnly.onlineSignIn);

		const auto nameOnly = parse(R"({"name":"p"})");
		CHECK(nameOnly.portal);
		CHECK_FALSE(nameOnly.externalLinks);
		CHECK_FALSE(nameOnly.onlineSignIn);

		const auto empty = parse("{}");
		CHECK(empty.portal);
		CHECK(empty.name.empty());
		CHECK_FALSE(empty.externalLinks);
		CHECK_FALSE(empty.onlineSignIn);
	}

	TEST_CASE("wrong types fail closed")
	{
		// Only a JSON true grants a permission; a name must be a string.
		const auto policy = parse(R"({"name":7,"externalLinks":"true","onlineSignIn":1})");
		CHECK(policy.portal);
		CHECK(policy.name.empty());
		CHECK_FALSE(policy.externalLinks);
		CHECK_FALSE(policy.onlineSignIn);

		CHECK_FALSE(parse(R"({"externalLinks":null,"onlineSignIn":{}})").externalLinks);
		CHECK_FALSE(parse(R"({"externalLinks":[true],"onlineSignIn":true})").externalLinks);
	}

	TEST_CASE("a description that is not an object is a portal with everything off")
	{
		for (const char *text : {"null", "true", "\"crazygames\"", "[]", "42", "\"unreadable\"", "{not json", "{\"externalLinks\":true"})
		{
			CAPTURE(text);
			const auto policy = parse(text);
			CHECK(policy.portal);
			CHECK_FALSE(policy.externalLinks);
			CHECK_FALSE(policy.onlineSignIn);
		}
	}

	TEST_CASE("installing a policy switches ApplicationHost's external links")
	{
		const auto previous = PortalPolicy::current();
		PortalPolicy::install(parse(R"({"name":"p","externalLinks":false,"onlineSignIn":true})"));
		CHECK_FALSE(GAGCore::ApplicationHost::externalLinksAllowed());
		CHECK_FALSE(PortalPolicy::externalLinks());
		CHECK(PortalPolicy::onlineSignIn());
		CHECK_EQ(PortalPolicy::current().name, "p");
		// Refused before any browser is asked; sign-in pages are not links.
		CHECK_FALSE(GAGCore::ApplicationHost::openUrl("https://example.org/"));
		PortalPolicy::install({});
		CHECK(GAGCore::ApplicationHost::externalLinksAllowed());
		PortalPolicy::install(previous);
	}
}
