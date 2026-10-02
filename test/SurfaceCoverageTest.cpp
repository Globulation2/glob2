// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SDLGraphicContext.h>
#include <GUIStyle.h>
#include <Toolkit.h>
#include <array>
#include <cstring>

namespace
{
using namespace GAGCore;
std::array<int,3> pixel(DrawableSurface& surface,int x,int y)
{
    SDL_Surface* raw=surface.getSDLSurface();
    Uint32 value=0;
    std::memcpy(&value,static_cast<Uint8*>(raw->pixels)+y*raw->pitch+x*raw->format->BytesPerPixel,raw->format->BytesPerPixel);
    Uint8 r,g,b; SDL_GetRGB(value,raw->format,&r,&g,&b);
    return {r,g,b};
}
void black(DrawableSurface& surface)
{
    surface.setClipRect(); surface.drawFilledRect(0,0,surface.getW(),surface.getH(),Color(0,0,0));
}
}
TEST_SUITE("SurfaceCoverage")
{
    TEST_CASE("float and byte alpha grids agree and respect clip rectangles")
    {
        glob2test::ToolkitScope toolkit; Toolkit::initGraphic(64,64,0,"surface coverage");
        DrawableSurface floating(16,16),bytes(16,16);
        black(floating); black(bytes);
        floating.setClipRect(3,3,7,7); bytes.setClipRect(3,3,7,7);
        const std::valarray<float> floats{1,0,1,0,1,0,1,0,1};
        const std::valarray<unsigned char> alphas{255,0,255,0,255,0,255,0,255};
        floating.drawAlphaMap(floats,3,3,2,2,4,4,Color(255,40,20));
        bytes.drawAlphaMap(alphas,3,3,2,2,4,4,Color(255,40,20));
        for (int y=0; y<16; ++y) for (int x=0; x<16; ++x)
        {
            CHECK(pixel(floating,x,y)==pixel(bytes,x,y));
            const bool painted=x>=3 && y>=3 && x<10 && y<10 && ((x<6)==(y<6));
            CHECK(pixel(floating,x,y)==(painted ? std::array<int,3>{255,40,20} : std::array<int,3>{0,0,0}));
        }
    }

    TEST_CASE("scaled partial blits preserve crop coordinates and untouched pixels")
    {
        glob2test::ToolkitScope toolkit; Toolkit::initGraphic(64,64,0,"surface coverage");
        DrawableSurface source(4,4),target(12,12);
        black(source); black(target);
        source.drawFilledRect(1,1,1,1,Color(255,0,0));
        source.drawFilledRect(2,1,1,1,Color(0,255,0));
        source.drawFilledRect(1,2,1,1,Color(0,0,255));
        source.drawFilledRect(2,2,1,1,Color(255,255,255));
        target.setClipRect(4,4,4,4);
        target.drawSurface(3,3,6,6,&source,1,1,2,2,255);
        for (int y=0; y<12; ++y) for (int x=0; x<12; ++x)
        {
            const auto expected=x>=4 && y>=4 && x<8 && y<8
                ? pixel(source,x<6 ? 1:2,y<6 ? 1:2) : std::array<int,3>{0,0,0};
            const auto actual=pixel(target,x,y);
            INFO("pixel "<<x<<","<<y<<" actual "<<actual[0]<<","<<actual[1]<<","<<actual[2]<<" expected "<<expected[0]<<","<<expected[1]<<","<<expected[2]);
            CHECK(actual==expected);
        }
        target.setClipRect();
        target.drawSurface(0,0,0,6,&source,0,0,4,4,255);
        CHECK(pixel(target,0,0)==std::array<int,3>{0,0,0});
    }

    TEST_CASE("legacy progress style paints exact empty half and full interiors")
    {
        glob2test::ToolkitScope toolkit; Toolkit::initGraphic(64,64,0,"surface coverage");
        GAGGUI::Style style;
        DrawableSurface target(40,30);
        for (int value : {0,50,100})
        {
            black(target); style.drawProgressBar(&target,2,2,22,value,100);
            CHECK(pixel(target,2,2)!=std::array<int,3>{0,0,0});
            for (int x=3; x<23; ++x)
                CHECK((pixel(target,x,4)==std::array<int,3>{255,255,255})==(x<3+value/5));
        }
        CHECK(style.getStyleMetric(GAGGUI::Style::STYLE_METRIC_PROGRESS_BAR_HEIGHT)==22);
        CHECK(style.getStyleMetric(GAGGUI::Style::STYLE_METRIC_LIST_SCROLLBAR_WIDTH)==22);
    }
}
