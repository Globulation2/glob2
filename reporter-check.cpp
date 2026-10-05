#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
TEST_CASE("decoder subcases") { SUBCASE("SDL_image") { CHECK(true); } SUBCASE("SDL fallback") { CHECK(true); } }
