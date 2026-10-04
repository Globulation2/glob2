// SPDX-License-Identifier: GPL-3.0-or-later
// HttpFetch URL policy and response helpers; network behaviour is covered by
// test/transport/test_http_fetch.py.

#include "Glob2Test.h"
#include "HttpFetch.h"

#include <stdexcept>

TEST_SUITE("HttpFetch")
{
	TEST_CASE("URLs split into host, port and target")
	{
		auto url = HttpFetch::parseUrl("https://play.example.org/api/v1/rooms?x=1#frag");
		CHECK(url.secure);
		CHECK_EQ(url.host, "play.example.org");
		CHECK_EQ(url.port, "443");
		CHECK_EQ(url.target, "/api/v1/rooms?x=1");
		CHECK_EQ(url.authority(), "play.example.org");

		url = HttpFetch::parseUrl("https://[2001:db8::1]:8443");
		CHECK_EQ(url.host, "2001:db8::1");
		CHECK_EQ(url.port, "8443");
		CHECK_EQ(url.target, "/");
		CHECK_EQ(url.authority(), "[2001:db8::1]:8443");

		url = HttpFetch::parseUrl("http://localhost:8080?q");
		CHECK_FALSE(url.secure);
		CHECK_EQ(url.target, "/?q");
		CHECK_EQ(url.authority(), "localhost:8080");
		CHECK_EQ(HttpFetch::parseUrl("http://127.0.0.1/").port, "80");
		CHECK_EQ(HttpFetch::parseUrl("http://[::1]:9/").host, "::1");
	}

	TEST_CASE("unsafe or malformed URLs are rejected")
	{
		for (const char *text :
			 {"http://example.org/", "http://127.0.0.1.example.org/", "http://128.0.0.1/",
			  "ftp://localhost/", "wss://localhost/", "https://user:secret@localhost/", "https:///x",
			  "https://localhost:0/", "https://localhost:65536/", "https://localhost:8x/",
			  "https://local host/", "https://[::1/", "https://[::1]x/"})
			CHECK_THROWS_AS(HttpFetch::parseUrl(text), std::invalid_argument);
	}

	TEST_CASE("response headers are matched case-insensitively")
	{
		HttpFetch::Response response;
		response.headers = {{"Content-Type", "application/json"}, {"ETag", "\"v1\""}};
		CHECK_EQ(response.header("content-type"), "application/json");
		CHECK_EQ(response.header("ETAG"), "\"v1\"");
		CHECK_EQ(response.header("Location"), "");
		CHECK_EQ(std::string(HttpFetch::methodName(HttpFetch::Method::Put)), "PUT");
	}
}
