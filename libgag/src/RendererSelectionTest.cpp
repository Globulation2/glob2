// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <RendererSelection.h>

using namespace GAGCore;
TEST_SUITE("RendererSelection")
{
    TEST_CASE("stable names aliases and startup precedence")
    {
        for (auto backend : RendererBackends) CHECK(parseRendererBackend(rendererBackendName(backend)) == backend);
        CHECK(parseRendererBackend("gpu") == RendererBackend::LegacyGL);
        CHECK(parseRendererBackend("sdl") == RendererBackend::PortableSDL);
        CHECK_FALSE(parseRendererBackend("unknown"));
        CHECK(resolveRendererPreference(RendererBackend::Software, {}, {}) == RendererBackend::Software);
        CHECK(resolveRendererPreference(RendererBackend::Software, {}, RendererBackend::PortableSDL) == RendererBackend::PortableSDL);
        CHECK(resolveRendererPreference(RendererBackend::Software, RendererBackend::Auto, RendererBackend::PortableSDL) == RendererBackend::Auto);
    }
}
