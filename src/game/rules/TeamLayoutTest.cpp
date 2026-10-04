// SPDX-License-Identifier: GPL-3.0-or-later
// Team layouts read from alliances, the layouts offered for each colony count, and the
// alliances an offered layout builds.

#include "Glob2Test.h"
#include "TeamLayout.h"

using TeamLayout::Layout;

namespace
{
Layout split(std::vector<int> sizes, int lone = -1)
{
	return Layout{Layout::Split, std::move(sizes), lone};
}
} // namespace

TEST_SUITE("TeamLayout")
{
	TEST_CASE("classify names the shape, not the slot order")
	{
		CHECK(TeamLayout::classify({}).kind == Layout::FreeForAll);
		CHECK(TeamLayout::classify({0}).kind == Layout::FreeForAll);
		CHECK(TeamLayout::classify({0, 1}).kind == Layout::FreeForAll);
		CHECK(TeamLayout::classify({3, 0, 7, 1}).kind == Layout::FreeForAll);
		CHECK(TeamLayout::classify({0, 0, 1, 1}) == split({2, 2}));
		CHECK(TeamLayout::classify({1, 0, 0, 1}) == split({2, 2}));
		CHECK(TeamLayout::classify({0, 1, 0, 1, 0, 1}) == split({3, 3}));
		CHECK(TeamLayout::classify({0, 0, 0, 0, 1, 1, 1, 1}) == split({4, 4}));
		CHECK(TeamLayout::classify({0, 0, 0, 1, 1, 1, 1, 1}) == split({3, 5}));
		CHECK(TeamLayout::classify({1, 1, 0, 1, 1, 1, 0, 1}) == split({2, 6}));
		CHECK(TeamLayout::classify({0, 0, 1, 1, 2, 2}) == split({2, 2, 2}));
		CHECK(TeamLayout::classify({5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5}).kind == Layout::Custom);
	}

	TEST_CASE("one against all records the lone colony")
	{
		CHECK(TeamLayout::classify({0, 1, 1, 1}) == split({1, 3}, 0));
		CHECK(TeamLayout::classify({1, 1, 0, 1}) == split({1, 3}, 2));
		CHECK(TeamLayout::classify({4, 4, 4, 4, 4, 4, 4, 2}) == split({1, 7}, 7));
		CHECK(TeamLayout::classify({0, 1, 1, 1}).oneVsAll());
		CHECK_FALSE(TeamLayout::classify({0, 0, 1, 1}).oneVsAll());
	}

	TEST_CASE("irregular teams are custom")
	{
		CHECK(TeamLayout::classify({0, 0, 1, 1, 2}).kind == Layout::Custom);
		CHECK(TeamLayout::classify({0, 0, 1, 1, 1, 2, 2, 2}).kind == Layout::Custom);
		CHECK(TeamLayout::classify({0, 0, 0, 1}).kind == Layout::Split);
		CHECK(TeamLayout::classify({0, 0, 0}).kind == Layout::Custom);
	}

	TEST_CASE("people against AIs is HumansVsAI only when the sides split that way")
	{
		CHECK(TeamLayout::classify({0, 0, 1, 1}, {false, false, true, true}).kind == Layout::HumansVsAI);
		CHECK(TeamLayout::classify({0, 1, 1, 1}, {false, true, true, true}).kind == Layout::HumansVsAI);
		CHECK(TeamLayout::classify({0, 0, 1, 1}, {false, true, false, true}) == split({2, 2}));
		CHECK(TeamLayout::classify({0, 0, 1, 1}, {true, true, true, true}) == split({2, 2}));
		CHECK(TeamLayout::classify({0, 1, 2}, {false, true, true}).kind == Layout::FreeForAll);
	}

	TEST_CASE("offered layouts for each colony count")
	{
		using V = std::vector<Layout>;
		const Layout ffa{Layout::FreeForAll, {}, -1};
		CHECK(TeamLayout::offered(1) == V{ffa});
		CHECK(TeamLayout::offered(2) == V{ffa});
		CHECK(TeamLayout::offered(3) == V{ffa, split({1, 2})});
		CHECK(TeamLayout::offered(4) == V{ffa, split({2, 2}), split({1, 3})});
		CHECK(TeamLayout::offered(5) == V{ffa, split({2, 3}), split({1, 4})});
		CHECK(TeamLayout::offered(6) == V{ffa, split({3, 3}), split({2, 4}), split({1, 5}), split({2, 2, 2})});
		CHECK(TeamLayout::offered(7) == V{ffa, split({3, 4}), split({2, 5}), split({1, 6})});
		CHECK(TeamLayout::offered(8) == V{ffa, split({4, 4}), split({3, 5}), split({2, 6}), split({1, 7}), split({2, 2, 2, 2})});
		const auto sixteen = TeamLayout::offered(16);
		CHECK(sixteen.size() == 1 + 8 + 2);
		CHECK(sixteen[9] == split({4, 4, 4, 4}));
		CHECK(sixteen[10] == split(std::vector<int>(8, 2)));
		CHECK(TeamLayout::offered(4, false) == V{ffa, split({2, 2})});
		CHECK(TeamLayout::offered(3, false) == V{ffa});
	}

	TEST_CASE("every offered layout builds alliances that read back as itself")
	{
		for (int n = 1; n <= 16; ++n)
			for (int focus = 0; focus < n; ++focus)
				for (const Layout &layout : TeamLayout::offered(n))
				{
					CAPTURE(n);
					CAPTURE(focus);
					const auto alliances = TeamLayout::alliances(layout, n, focus);
					REQUIRE(int(alliances.size()) == n);
					// Dense, in order of first appearance.
					int next = 0;
					for (int alliance : alliances)
					{
						CHECK(alliance <= next);
						if (alliance == next)
							++next;
					}
					Layout expected = layout;
					if (layout.oneVsAll())
						expected.lone = focus;
					CHECK(TeamLayout::classify(alliances) == expected);
					CHECK(TeamLayout::indexOf(TeamLayout::offered(n), TeamLayout::classify(alliances), focus) >= 0);
				}
	}

	TEST_CASE("the smaller side is built around the focus colony")
	{
		CHECK(TeamLayout::alliances(split({2, 2}), 4, 0) == std::vector<int>{0, 0, 1, 1});
		CHECK(TeamLayout::alliances(split({1, 3}), 4, 2) == std::vector<int>{0, 0, 1, 0});
		CHECK(TeamLayout::alliances(split({3, 5}), 8, 6) == std::vector<int>{0, 1, 1, 1, 1, 1, 0, 0});
		CHECK(TeamLayout::alliances(split({2, 2, 2}), 6, 3) == std::vector<int>{0, 0, 1, 1, 2, 2});
		CHECK(TeamLayout::alliances(Layout{}, 4, 0).empty());
	}

	TEST_CASE("one against all is selected only around the focus colony")
	{
		const auto options = TeamLayout::offered(4);
		CHECK(TeamLayout::indexOf(options, split({1, 3}, 0), 0) == 2);
		CHECK(TeamLayout::indexOf(options, split({1, 3}, 1), 0) == -1);
		CHECK(TeamLayout::indexOf(options, Layout{}, 0) == -1);
	}

	TEST_CASE("the preferences keep their four format names")
	{
		CHECK(std::string(TeamLayout::legacyFormat(Layout{Layout::FreeForAll, {}, -1}, false)) == "FFA");
		CHECK(std::string(TeamLayout::legacyFormat(split({2, 2}), false)) == "2 vs 2");
		CHECK(std::string(TeamLayout::legacyFormat(split({1, 3}, 0), true)) == "You vs all");
		CHECK(std::string(TeamLayout::legacyFormat(split({1, 3}, 0), false)) == "Custom teams");
		CHECK(std::string(TeamLayout::legacyFormat(split({3, 3}), false)) == "Custom teams");
	}
}
