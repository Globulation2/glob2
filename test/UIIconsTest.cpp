// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ui/FrontendUI.h"
#include <stdexcept>
#include <Toolkit.h>

TEST_SUITE("UIIcons")
{
	TEST_CASE("all semantic icons have the pinned raster sizes")
	{
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::initGraphic(64, 64, 0, "icon assets");
		for (int i = 0; i < int(Glob2UI::UIIcon::Count); ++i)
		{
			auto asset = Glob2UI::uiIcon(Glob2UI::UIIcon(i));
			INFO(asset->name);
			REQUIRE(asset->available());
			REQUIRE(asset->rasters.size() == 6);
			int index = 0;
			for (int pixels : {20, 24, 40, 48, 60, 72})
			{
				CHECK(asset->rasters[index].pixels == pixels);
				CHECK(bool(asset->rasters[index].surface));
				++index;
			}
		}
		CHECK_THROWS_AS(Glob2UI::uiIcon(Glob2UI::UIIcon::Count), std::out_of_range);
	}
	TEST_CASE("aliases share live assets and the cache releases unused graphics resources")
	{
		glob2test::ToolkitScope toolkit;
		GAGCore::Toolkit::initGraphic(64, 64, 0, "icon assets");
		auto info = Glob2UI::uiIcon(Glob2UI::UIIcon::Info);
		CHECK(Glob2UI::uiIcon(Glob2UI::UIIcon::Credits) == info);
		std::weak_ptr<const GAGGUI::ui::IconAsset> weak = info;
		info.reset();
		CHECK(weak.expired());
		CHECK(Glob2UI::uiIcon(Glob2UI::UIIcon::Info)->available());
	}
}
