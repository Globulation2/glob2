// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ColonySkinPreview.h"
#include "online/SkinAuthorization.h"
#include <Toolkit.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>

namespace {
struct SkinFixture {
    glob2test::ToolkitScope toolkit;
    glob2test::TempDir directory{"colony-skin-preview"};
    std::string paintPath = (directory.path / "paint.webp").string();
    std::string materialPath = (directory.path / "material.webp").string();
    SkinFixture() {
        GAGCore::Toolkit::initGraphic(64, 64, 0, "colony skin subscriber test");
        const auto fixture = nlohmann::json::parse(glob2test::readFile(
            glob2test::sourceRoot() / "test/fixtures/skins/authorization.json"));
        auto decode = [&](const char *name) {
            const auto hex = fixture.at(name).get<std::string>();
            std::string bytes;
            for (size_t i = 0; i < hex.size(); i += 2)
                bytes.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
            return bytes;
        };
        glob2test::writeFile(paintPath, decode("textureHex"));
        glob2test::writeFile(materialPath, decode("materialHex"));
    }
    Online::AuthorizedSkin appearance(std::uint32_t color, unsigned angle) {
        Online::AuthorizedSkin skin;
        skin.team = 2;
        skin.expiresAt = std::time(nullptr) + 3600;
        skin.buildingColor = color;
        skin.swarmViewAngle = angle;
        return skin;
    }
    void enqueue(ColonySkinPreview &preview, Online::AuthorizedSkin appearance) {
        preview.prepareSkin(std::move(appearance), paintPath, materialPath);
    }
    void finish(ColonySkinPreview &a, ColonySkinPreview &b) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((a.preparingSkins[2].state() != GAGCore::AssetLoader::State::Cancelled ||
                b.preparingSkins[2].state() != GAGCore::AssetLoader::State::Cancelled) &&
               std::chrono::steady_clock::now() < deadline)
            GAGCore::Toolkit::assets().poll();
        REQUIRE(a.preparingSkins[2].state() == GAGCore::AssetLoader::State::Cancelled);
        REQUIRE(b.preparingSkins[2].state() == GAGCore::AssetLoader::State::Cancelled);
    }
};
}

TEST_SUITE("ColonySkinPreview") {
    TEST_CASE("shared images retain each owner's fresh appearance [display:64x64]") {
        SkinFixture fixture;
        ColonySkinPreview a, b;
        fixture.enqueue(a, fixture.appearance(0x112233, 10));
        fixture.enqueue(b, fixture.appearance(0x445566, 20));
        // Refresh A while B still holds the same immutable image preparation.
        fixture.enqueue(a, fixture.appearance(0x778899, 30));
        fixture.finish(a, b);
        CHECK(a.buildingColor(2) == 0x778899);
        CHECK(b.buildingColor(2) == 0x445566);
        CHECK(a.swarmAngles[2] == 30);
        CHECK(b.swarmAngles[2] == 20);
    }
    TEST_CASE("expired and cancelled subscribers cannot affect another owner [display:64x64]") {
        SkinFixture fixture;
        ColonySkinPreview a, b;
        auto expired = fixture.appearance(0x112233, 10);
        expired.expiresAt = std::time(nullptr) - 1;
        fixture.enqueue(a, expired);
        fixture.enqueue(b, fixture.appearance(0x445566, 20));
        fixture.finish(a, b);
        CHECK_FALSE(a.buildingColor(2));
        CHECK(b.buildingColor(2) == 0x445566);
        fixture.enqueue(a, fixture.appearance(0x778899, 30));
        fixture.enqueue(b, fixture.appearance(0xaabbcc, 40));
        a.uninstall(2);
        fixture.finish(a, b);
        CHECK_FALSE(a.buildingColor(2));
        CHECK(b.buildingColor(2) == 0xaabbcc);
    }
}
