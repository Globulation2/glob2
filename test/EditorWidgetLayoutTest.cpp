// SPDX-License-Identifier: GPL-3.0-or-later

#include "Glob2Test.h"
#include "WidgetRectangle.h"

class EditorWidgetLayoutTest
{

public:
	void testResizeKeepsClickCoordinatesAndEdges()
	{
		RightAnchoredWidgetRectangle area(widgetRectangle(656, 160, 128, 96), 800);
		// A size change can be followed by input before the next paint.
		area.updateWindowWidth(1000);
		CHECK_EQ(856, area.x);
		CHECK_EQ(160, area.y);
		CHECK(!area.is_in(656, 160));
		CHECK(area.is_in(856, 160));
		CHECK(area.is_in(983, 255));
		CHECK(!area.is_in(984, 255));
		CHECK(!area.is_in(983, 256));
		// The same brush cell has the same local coordinates after moving.
		CHECK_EQ(65, 921 - area.x);
		CHECK_EQ(37, 197 - area.y);
	}

	void testRepeatedResizeDoesNotDrift()
	{
		RightAnchoredWidgetRectangle area(widgetRectangle(656, 160, 128, 96), 800);
		for (int width : {1024, 1024, 640, 1920, 800})
		{
			area.updateWindowWidth(width);
			CHECK_EQ(16, width - (area.x + area.width));
			CHECK_EQ(160, area.y);
			CHECK_EQ(128, area.width);
			CHECK_EQ(96, area.height);
		}
		CHECK_EQ(656, area.x);
		// Controls created at a different window width use their own anchor.
		RightAnchoredWidgetRectangle later(widgetRectangle(880, 160, 128, 96), 1024);
		later.updateWindowWidth(800);
		CHECK_EQ(area.x, later.x);
	}

	void testOrdinaryRectanglesStayAbsolute()
	{
		widgetRectangle map(0, 16, 640, 480);
		CHECK(map.is_in(0, 16));
		CHECK(!map.is_in(640, 16));
		CHECK(!map.is_in(0, 496));
	}
};

TEST_SUITE("EditorWidgetLayout")
{
	TEST_CASE_FIXTURE(EditorWidgetLayoutTest, "ResizeKeepsClickCoordinatesAndEdges") { testResizeKeepsClickCoordinatesAndEdges(); }
	TEST_CASE_FIXTURE(EditorWidgetLayoutTest, "RepeatedResizeDoesNotDrift") { testRepeatedResizeDoesNotDrift(); }
	TEST_CASE_FIXTURE(EditorWidgetLayoutTest, "OrdinaryRectanglesStayAbsolute") { testOrdinaryRectanglesStayAbsolute(); }
}
