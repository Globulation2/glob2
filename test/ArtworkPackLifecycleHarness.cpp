// SPDX-License-Identifier: GPL-3.0-or-later
#include <GraphicContext.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <SDL_image.h>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace GAGCore;
namespace fs = std::filesystem;

class InspectSprite : public Sprite
{
public:
    bool hasHD() const { return !experimentImages.empty() && experimentImages[0]; }
    DrawableSurface* hd() const { return hasHD() ? experimentImages[0] : nullptr; }
};

static void noLoads(const Sprite::HighResolutionStats& before)
{
    const auto after = Sprite::highResolutionStats();
    assert(after.imageLoads == before.imageLoads);
    assert(after.manifestParses == before.manifestParses);
    assert(after.packReloads == before.packReloads);
}

static void png(const fs::path& path, int side)
{
    auto surface = SDL_CreateRGBSurfaceWithFormat(0, side, side, 32, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 20, 180, 80, 255));
    assert(IMG_SavePNG(surface, path.string().c_str()) == 0);
    SDL_FreeSurface(surface);
}

int main(int argc, char** argv)
{
    const bool software = argc > 1 && std::string(argv[1]) == "software";
    const fs::path root = fs::absolute(".cache/artwork-pack-lifecycle");
    fs::create_directories(root / "a");
    fs::create_directories(root / "b");
    fs::create_directories(root / "missing");
    const auto manifest = root / "a/frames.txt";
    const auto missing = root / "missing/frames.txt";
    fs::remove(missing);
    auto write = [&](const std::string& body) { std::ofstream(manifest) << body; };
    const std::string valid = "GLOB2_HIGHRES 1\nprobe0 2 2 4 hd.png -\n";
    const auto original = (root / "probe").string();
    for (int session = 0; session < 2; ++session)
    {
        unsetenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        Toolkit::init("glob2-artwork-pack-test");
        auto gfx = Toolkit::initGraphic(640, 480, software ? 0 : GraphicContext::USEGPU, "Artwork lifecycle test");
        Toolkit::getFileManager()->addDir(root.string());
        png(root / "probe0.png", 2);
        png(root / "hd.png", 8);
        png(root / "a/hd.png", 8);
        png(root / "b/hd.png", 8);
        assert(Sprite::highResolutionStats().imageLoads == 0);
        if (session == 1)
        {
            InspectSprite sprite;
            assert(sprite.load(original));
            assert(!sprite.hasHD());
        }
        else
        {
            // Select before loading any sprites, then inherit that selection.
            write(valid + "probe0 99 99 4 absent.png -\n");
            std::ofstream(root / "b/frames.txt") << valid;
            setenv("GLOB2_EXPERIMENT_TEXTURE_DIR", (root / "a").string().c_str(), 1);
            Sprite::setHighResolution(true);
            InspectSprite sprite;
            assert(sprite.load(original));
            assert(sprite.hasHD() == !software);
            auto before = Sprite::highResolutionStats();
            assert(before.manifestParses == (software ? 0u : 1u));
            auto hd = sprite.hd();
            gfx->drawSprite(0, 0, &sprite, 0);
            Sprite::setHighResolution(true);
            noLoads(before);
            assert(sprite.hd() == hd);
            {
                InspectSprite late;
                assert(late.load(original));
                assert(late.hasHD() == !software);
                assert(Sprite::highResolutionStats().manifestParses == before.manifestParses);
            }
            before = Sprite::highResolutionStats();
            Sprite::reloadHighResolutionPack();
            assert(Sprite::highResolutionStats().packReloads == before.packReloads + 1);
            assert(sprite.hasHD() == !software);
            setenv("GLOB2_EXPERIMENT_TEXTURE_DIR", (root / "b").string().c_str(), 1);
            before = Sprite::highResolutionStats();
            Sprite::setHighResolution(true);
            assert(Sprite::highResolutionStats().packReloads == before.packReloads + 1);
            assert(sprite.hasHD() == !software);
            // The experimental override intentionally enables HD even when the preference is off.
            before = Sprite::highResolutionStats();
            Sprite::setHighResolution(false);
            noLoads(before);
            unsetenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
            Sprite::setHighResolution(false);
            assert(!sprite.hasHD());
            assert(Sprite::highResolutionStats().cpuBytes == 0);
            before = Sprite::highResolutionStats();
            Sprite::setHighResolution(false);
            noLoads(before);

            setenv("GLOB2_EXPERIMENT_TEXTURE_DIR", (root / "missing").string().c_str(), 1);
            Sprite::setHighResolution(true);
            assert(!sprite.hasHD());
            before = Sprite::highResolutionStats();
            std::ofstream(missing) << "GLOB2_HIGHRES 1\nprobe0 2 2 4 ../hd.png -\n";
            Sprite::setHighResolution(true);
            noLoads(before); // Missing packs remain cached until deliberate invalidation.
            Sprite::reloadHighResolutionPack();
            assert(!sprite.hasHD()); // Unsafe filenames remain rejected.

            setenv("GLOB2_EXPERIMENT_TEXTURE_DIR", (root / "a").string().c_str(), 1);
            for (const auto& invalid : {"bad header", "GLOB2_HIGHRES 9\n", "GLOB2_HIGHRES 1\nprobe0 3 2 4 hd.png -\n", "GLOB2_HIGHRES 1\nprobe0 2 2 4 absent.png -\n"})
            {
                write(invalid);
                Sprite::reloadHighResolutionPack();
                assert(!sprite.hasHD());
                before = Sprite::highResolutionStats();
                Sprite::setHighResolution(true);
                noLoads(before);
            }
            write("GLOB2_HIGHRES 1\nprobe0 3 2 4 hd.png -\nprobe0 2 2 4 hd.png -\n");
            Sprite::reloadHighResolutionPack();
            assert(!sprite.hasHD()); // An invalid first entry is not replaced by a duplicate.
            write(valid);
            png(root / "a/hd.png", 4);
            Sprite::reloadHighResolutionPack();
            assert(!sprite.hasHD());
            png(root / "a/hd.png", 8);
            write(std::string(1024 * 1024 + 1, 'x'));
            Sprite::reloadHighResolutionPack();
            assert(!sprite.hasHD());
            write(valid);
            Sprite::reloadHighResolutionPack();
            assert(sprite.hasHD() == !software);
            assert(Sprite::highResolutionStats().cpuBytes == (software ? 0u : 8u * 8u * 4u));
            Sprite::flushBatches(gfx);
        }
        Toolkit::close();
        assert(Sprite::highResolutionStats().imageLoads == 0);
    }
    unsetenv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    std::cout << "PASS artwork selection, inheritance, no-op application, explicit reload, pack validation, fallback and toolkit reinitialization\n";
}
