// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/Glob2Test.h"
#include "support/ScopedEnvironment.h"
#include <FileManager.h>
#include <GraphicContext.h>

TEST_SUITE("InstalledPaths")
{
    TEST_CASE("omit compiled checkout lookups and preserve explicit overrides")
    {
        glob2test::ScopedEnvironment assets("GLOB2_ASSET_DIR", "/explicit/asset/override");
        GAGCore::FileManager files("glob2");
        bool foundOverride = false;
        for (unsigned i = 0; i < files.getDirCount(); ++i)
        {
            const auto directory = files.getDir(i);
            CHECK(directory != glob2test::sourceRoot().string());
            CHECK(directory.find("/runtime-assets") == std::string::npos);
            foundOverride |= directory == "/explicit/asset/override";
        }
        CHECK(foundOverride);
        glob2test::ScopedEnvironment shaders("GLOB2_RENDERER_SHADER_DIR", "/explicit/shader/override");
        CHECK(GAGCore::GraphicContext::rendererShaderDirectory() == "/explicit/shader/override");
        {
            glob2test::ScopedEnvironment noShaderOverride("GLOB2_RENDERER_SHADER_DIR", nullptr);
            CHECK(GAGCore::GraphicContext::rendererShaderDirectory().find(glob2test::sourceRoot().string()) == std::string::npos);
        }
    }
}
