// SPDX-License-Identifier: GPL-3.0-or-later
// GAGCore::Sprite's sheet loader (the <name>.sheet index tools/package_assets.py
// writes for packaged builds) and its fallback to one file per frame.
#include "Glob2Test.h"
#include <chrono>
#include "HighResolutionInputs.h"
#include "LossyAlphaFixture.h"
#include "SpritePatternFixture.h"
#include "ScopedEnvironment.h"
#include "SpriteHighResolution.h"
#include <AssetLoader.h>
#include <DetailHysteresis.h>
#include <HighResolutionPolicy.h>
#include <RenderMemory.h>
#include <FileManager.h>
#include <GraphicContext.h>
#include <Toolkit.h>
#include <RenderDevice.h>
#include <SDL3/SDL.h>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace
{
// Frame i's pixel at (x, y): position dependent, with partial and zero alpha.
SDL_Color patternPixel(int frame, int x, int y)
{
	return SDL_Color{static_cast<Uint8>(frame * 10), static_cast<Uint8>(x * 20 + 5),
		static_cast<Uint8>(y * 30 + 7), static_cast<Uint8>(frame == 1 ? 0 : 255 - frame)};
}

void writePattern(const std::filesystem::path &path, int first, int count, int columns, int tile)
{

    using namespace spritePatternFixture;
    const unsigned char *data = nullptr;
    size_t size = 0;
    if (first == 0 && count == 5 && columns == 2 && tile == 3) { data = pattern_0_5_2_3; size = sizeof(pattern_0_5_2_3); }
    if (first == 2 && count == 2 && columns == 4 && tile == 4) { data = pattern_2_2_4_4; size = sizeof(pattern_2_2_4_4); }
    if (first == 0 && count == 1 && columns == 1 && tile == 4) { data = pattern_0_1_1_4; size = sizeof(pattern_0_1_1_4); }
    if (first == 1 && count == 1 && columns == 1 && tile == 4) { data = pattern_1_1_1_4; size = sizeof(pattern_1_1_1_4); }
    if (first == 3 && count == 1 && columns == 1 && tile == 4) { data = pattern_3_1_1_4; size = sizeof(pattern_3_1_1_4); }
    if (first == 0 && count == 4 && columns == 2 && tile == 3) { data = pattern_0_4_2_3; size = sizeof(pattern_0_4_2_3); }
    REQUIRE(data != nullptr);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data), size);

}

bool samePixels(GAGCore::DrawableSurface *surface, int frame, int size)
{
	if (!surface || surface->getW() != size || surface->getH() != size)
		return false;
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x)
		{
			Uint8 r, g, b, a;
			if (!SDL_ReadSurfacePixel(surface->sdlsurface, x, y, &r, &g, &b, &a))
				return false;
			const SDL_Color c = patternPixel(frame, x, y);
			if (r != c.r || g != c.g || b != c.b || a != c.a)
				return false;
		}
	return true;
}

struct SheetFixture
{
	glob2test::ToolkitScope toolkit;
	glob2test::TempDir scratch{"sprite-sheets"};
	SheetFixture()
	{
		GAGCore::Toolkit::initGraphic(64, 64, 0, "sprite sheet loader");
		GAGCore::Toolkit::getFileManager()->addDir(scratch.path.string());
	}
	void index(const std::string &name, const std::string &text)
	{
		std::ofstream(scratch.path / (name + ".sheet"), std::ios::binary) << text;
	}
};
}

TEST_SUITE("SpriteSheets") {
TEST_CASE("sheet tiles load as the frames they were packed from") {
	SheetFixture fixture;
	// Five recolorable 3x3 frames, sixteen to a row in the packer but two here,
	// and two plain 4x4 frames starting at frame 2 in a wider grid.
	writePattern(fixture.scratch.path / "set-sheet-0.webp", 0, 5, 2, 3);
	writePattern(fixture.scratch.path / "set-sheet-1.webp", 2, 2, 4, 4);
	// Windows line endings, as a text-mode save on Windows writes them.
	fixture.index("set", "# comment\r\n\r\nset-sheet-0.webp rotated 0 5 3 3\r\nset-sheet-1.webp image 2 2 4 4\r\n");
	GAGCore::Sprite sprite;
	REQUIRE(sprite.load("set"));
	REQUIRE(sprite.getFrameCount() == 5);
	for (int i = 0; i < 5; ++i)
	{
		const bool plain = i == 2 || i == 3;
		CHECK(sprite.rotated[i] != nullptr);
		CHECK((sprite.images[i] != nullptr) == plain);
		GLOB2_CHECK(samePixels(sprite.rotated[i]->orig, i, 3), "recolorable frame " + std::to_string(i));
		if (plain)
			GLOB2_CHECK(samePixels(sprite.images[i], i, 4), "plain frame " + std::to_string(i));
		// getW/getH prefer the plain layer, so both sheets' sizes show.
		CHECK(sprite.getW(i) == (plain ? 4 : 3));
	}
}

namespace {
// Restores the process-wide retirement budget a test replaces.
struct RetiredBudget {
    const GAGCore::Sprite::RetiredHighResolutionBudget saved = GAGCore::Sprite::retiredHighResolutionBudget();
    explicit RetiredBudget(GAGCore::Sprite::RetiredHighResolutionBudget budget) {
        GAGCore::Sprite::setRetiredHighResolutionBudget(budget);
    }
    ~RetiredBudget() { GAGCore::Sprite::setRetiredHighResolutionBudget(saved); }
};
}
TEST_CASE("detail hysteresis raises detail at once and lowers it after the delay") {
    GAGCore::DetailHysteresis detail(1);
    CHECK_FALSE(detail.update(1, 100, 50));
    CHECK(detail.update(0, 110, 50));
    CHECK(detail.level() == 0);
    // Less detail waits for the delay, measured from the first frame wanting it.
    CHECK_FALSE(detail.update(2, 200, 50));
    CHECK_FALSE(detail.update(1, 240, 50));
    CHECK(detail.level() == 0);
    // Wanting the current level again restarts the wait.
    CHECK_FALSE(detail.update(0, 245, 50));
    CHECK_FALSE(detail.update(1, 260, 50));
    CHECK_FALSE(detail.update(1, 300, 50));
    CHECK(detail.update(1, 310, 50));
    CHECK(detail.level() == 1);
    CHECK(detail.update(2, 400, 0));
    CHECK(detail.level() == 2);
    // A clock reading of zero starts the wait like any other time.
    GAGCore::DetailHysteresis fromZero(0);
    CHECK_FALSE(fromZero.update(1, 0, 50));
    CHECK_FALSE(fromZero.update(1, 49, 50));
    CHECK(fromZero.update(1, 50, 50));
}
TEST_CASE("the HD sprite policy shows HD above one screen pixel a logical pixel, native at or below") {
    using Policy = GAGCore::HighResolutionPolicy;
    // Thresholds: native up to and at density one, half resolution to two, full beyond.
    CHECK(Policy::levelFor(0.5) == Policy::Native);
    CHECK(Policy::levelFor(1.0) == Policy::Native);
    CHECK(Policy::levelFor(1.01) == Policy::HalfHD);
    CHECK(Policy::levelFor(2.0) == Policy::HalfHD);
    CHECK(Policy::levelFor(2.01) == Policy::FullHD);
    CHECK_FALSE(Policy::wantsHighResolution(1.0));
    CHECK(Policy::wantsHighResolution(1.1));
    // Density counts the drawable's pixels, not the window's points: a
    // 2560x1600 drawable in a 1280x800 window (a 2x display) at the default
    // zoom is two screen pixels a logical pixel, the same window on a 1x
    // display one. Zoomed out, the interface's own scale is the floor.
    CHECK(Policy::density(1, 2560.0 / 1280) == 2);
    CHECK(Policy::levelFor(Policy::density(1, 2560.0 / 1280)) == Policy::HalfHD);
    CHECK(Policy::levelFor(Policy::density(1, 1280.0 / 1280)) == Policy::Native);
    CHECK(Policy::density(0.5, 2) == 2);
    CHECK(Policy::density(1.5, 1) == 1.5);
    CHECK(Policy::levelFor(Policy::density(3, 1)) == Policy::FullHD);
    // Hysteresis: up at once, down only after the delay.
    Policy policy;
    CHECK(policy.level() == Policy::Native);
    CHECK_FALSE(policy.shown());
    CHECK(policy.update(1.1, 100, 50));
    CHECK(policy.level() == Policy::HalfHD);
    CHECK(policy.update(3, 110, 50));
    CHECK(policy.level() == Policy::FullHD);
    CHECK_FALSE(policy.update(0.9, 120, 50));
    CHECK(policy.level() == Policy::FullHD);
    CHECK(policy.shown());
    CHECK(policy.update(0.9, 170, 50));
    CHECK(policy.level() == Policy::Native);
    CHECK(policy.reduction() == 2);
    // The HD setting off forces native, whatever the view.
    policy.reset(Policy::FullHD);
    CHECK(policy.shown());
    CHECK(policy.level(false) == Policy::Native);
    CHECK_FALSE(policy.shown(false));
}
TEST_CASE("a frame draws HD only once every layer its native frame has is HD") {
    using Policy = GAGCore::HighResolutionPolicy;
    // Base and team layers both HD: HD.
    CHECK(Policy::frameDrawsHighResolution(true, true, true, true, true));
    // Not shown (zoomed out, or the setting off): native.
    CHECK_FALSE(Policy::frameDrawsHighResolution(false, true, true, true, true));
    // Missing, streaming or evicted HD: native, never a mix of resolutions.
    CHECK_FALSE(Policy::frameDrawsHighResolution(true, true, true, true, false));
    CHECK_FALSE(Policy::frameDrawsHighResolution(true, true, true, false, true));
    CHECK_FALSE(Policy::frameDrawsHighResolution(true, true, false, false, false));
    // A frame with only a base layer, or only a team layer, needs only that one.
    CHECK(Policy::frameDrawsHighResolution(true, true, false, true, false));
    CHECK(Policy::frameDrawsHighResolution(true, false, true, false, true));
}
namespace {
// The HD sprite policy on one renderer: the same frame, drawn at the same
// screen size, shows its native pixels while the view's density is one and its
// HD pixels once the density is two, and with HD off it is the native frame.
void checkHighResolutionPolicyOn(GAGCore::RendererBackend backend)
{
    glob2test::TempDir scratch("hd-policy");
    const auto artwork = scratch.path / "data/gfx";
    const auto hd = scratch.path / "data/highres/v1";
    std::filesystem::create_directories(artwork);
    std::filesystem::create_directories(hd);
    std::filesystem::copy_file(glob2test::sourceRoot() / "test/fixtures/image-assets/native-sheet.webp",
        artwork / "native-sheet.webp");
    std::ofstream(artwork / "policy-sheet.sheet") << "native-sheet.webp image 0 4 4 2\n";
    std::ofstream frame(hd / "frame.webp", std::ios::binary);
    frame.write(reinterpret_cast<const char*>(lossyAlphaFixture::webp), sizeof(lossyAlphaFixture::webp));
    frame.close();
    std::ofstream index(hd / "frames.txt");
    index << "GLOB2_HIGHRES 1\n";
    for (unsigned i = 0; i < 4; ++i) index << "policy-sheet" << i << " 4 2 4 frame.webp -\n";
    index.close();
    glob2test::ScopedEnvironment assets("GLOB2_ASSET_DIR", scratch.path.string().c_str());
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    const std::string name(GAGCore::rendererBackendName(backend));
    glob2test::ScopedEnvironment selection("GLOB2_RENDERER", name.c_str());
    glob2test::ToolkitScope toolkit;
    auto *gfx = GAGCore::Toolkit::initGraphic(64, 64, backend == GAGCore::RendererBackend::LegacyGL ?
        GAGCore::GraphicContext::USEGPU : backend == GAGCore::RendererBackend::PortableSDL ?
        GAGCore::GraphicContext::PORTABLEGPU : 0, "HD sprite policy");
    if (gfx->backendSwitchStatus().active != backend) {
        MESSAGE("HD sprite policy coverage unavailable on ", name);
        return;
    }
    struct Restore {
        std::int64_t delay = GAGCore::Sprite::highResolutionDetailDelay();
        ~Restore() { GAGCore::Sprite::setHighResolutionDetailDelay(delay); GAGCore::Sprite::setHighResolution(false); }
    } restore;
    GAGCore::Sprite::setHighResolutionDetailDelay(0);
    GAGCore::Sprite::resetHighResolutionPolicy(1);
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/policy-sheet"));
    const auto capture = [&] {
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> shot(nullptr, SDL_DestroySurface);
        if (gfx->renderer) gfx->renderer->flush();
        if (auto *device = gfx->renderDevice()) shot.reset(device->captureSynchronously({}));
        else if (backend == GAGCore::RendererBackend::PortableSDL) shot.reset(gfx->renderer->capture());
        else if (!gfx->rendererCapabilities().accelerated) shot.reset(SDL_DuplicateSurface(gfx->getSDLSurface()));
#ifdef HAVE_OPENGL
        else {
            GLint viewport[4];
            glGetIntegerv(GL_VIEWPORT, viewport);
            shot.reset(SDL_CreateSurface(viewport[2], viewport[3], SDL_PIXELFORMAT_RGBA32));
            REQUIRE(shot);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, shot->pixels);
        }
#endif
        REQUIRE(shot);
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
            SDL_ConvertSurface(shot.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        REQUIRE(rgba);
        std::vector<Uint8> pixels(size_t(rgba->w) * rgba->h * 4);
        for (int row = 0; row < rgba->h; ++row)
            std::memcpy(pixels.data() + size_t(row) * rgba->w * 4,
                static_cast<Uint8 *>(rgba->pixels) + row * rgba->pitch, size_t(rgba->w) * 4);
        return pixels;
    };
    // Draws frame 0 at twice its logical size for a view of `density`, until
    // the HD layers the policy may want are published and drawn.
    const auto draw = [&](double density) {
        std::vector<Uint8> pixels;
        const auto deadline = SDL_GetTicks() + 20000;
        for (unsigned settled = 0; settled < 4 && SDL_GetTicks() < deadline;) {
            GAGCore::Sprite::setHighResolutionDensity(density);
            gfx->setClipRect();
            gfx->drawFilledRect(0, 0, gfx->getW(), gfx->getH(), GAGCore::Color(0, 0, 0));
            gfx->beginMapTransform(2, 0, 0, 0, 0, gfx->getW(), gfx->getH());
            gfx->drawSprite(4.f, 4.f, &sprite, 0);
            GAGCore::Sprite::flushBatches(gfx);
            gfx->endMapTransform();
            pixels = capture();
            gfx->nextFrame();
            GAGCore::Toolkit::pollAssets(2);
            GAGCore::Sprite::pollHighResolution(2);
            const bool ready = !GAGCore::Sprite::highResolutionShown() || sprite.experimentImages[0];
            settled = ready ? settled + 1 : 0;
        }
        return pixels;
    };
    const auto differing = [](const std::vector<Uint8> &a, const std::vector<Uint8> &b) {
        REQUIRE(a.size() == b.size());
        unsigned count = 0;
        for (size_t i = 0; i < a.size(); i += 4) {
            int most = 0;
            for (int c = 0; c < 3; ++c) most = std::max(most, std::abs(int(a[i + c]) - int(b[i + c])));
            count += most > 8;
        }
        return count;
    };
    GAGCore::Sprite::setHighResolution(false);
    const auto native = draw(2);
    CHECK(GAGCore::Sprite::highResolutionLevel() == GAGCore::HighResolutionPolicy::Native);
    GAGCore::Sprite::setHighResolution(true);
    const auto one = draw(1);
    CHECK_FALSE(GAGCore::Sprite::highResolutionShown());
    CHECK(differing(one, native) == 0);
    const auto two = draw(2);
    CHECK(GAGCore::Sprite::highResolutionLevel() == GAGCore::HighResolutionPolicy::HalfHD);
    REQUIRE(sprite.experimentImages[0]);
    CHECK(differing(two, native) > 4);
    // Back at one (the test's delay is zero): native again.
    const auto back = draw(1);
    CHECK(differing(back, native) == 0);
}
}
namespace {
// Draws the unit sheet's HD frames on a device with the phone budget until its
// room for retained HD frames runs out, then moves to other frames: the idle
// blocks are evicted, oldest first, so the new frames draw HD, and the evicted
// ones reload when they are drawn again, unless HD admission is held.
void checkIdleHighResolutionEviction(GAGCore::RendererBackend backend)
{
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", "");
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    // Chosen once a process: the case runs isolated.
    glob2test::ScopedEnvironment budget("GLOB2_RENDER_BUDGET", "mobile");
    const std::string name(GAGCore::rendererBackendName(backend));
    glob2test::ScopedEnvironment selection("GLOB2_RENDERER", name.c_str());
    glob2test::ToolkitScope toolkit;
    auto *gfx = GAGCore::Toolkit::initGraphic(64, 64, 0, "HD eviction");
    if (gfx->backendSwitchStatus().active != backend || !gfx->renderDevice()) {
        MESSAGE("HD eviction coverage unavailable on ", name);
        return;
    }
    struct Restore {
        std::int64_t delay = GAGCore::Sprite::highResolutionDetailDelay();
        ~Restore() { GAGCore::Sprite::setHighResolutionDetailDelay(delay); GAGCore::Sprite::setHighResolution(false); }
    } restore;
    GAGCore::Sprite::setHighResolutionDetailDelay(0);
    GAGCore::Sprite::resetHighResolutionPolicy(2);
    GAGCore::Sprite::setHighResolution(true);
    GAGCore::Sprite units;
    REQUIRE(units.load("data/gfx/unit"));
    const unsigned frames = unsigned(units.images.size()), blocks = (frames + 31) / 32;
    REQUIRE(blocks >= 8);
    const auto range = [&](unsigned firstBlock, unsigned lastBlock) {
        std::vector<unsigned> indices;
        for (unsigned i = firstBlock * 32; i < std::min(frames, lastBlock * 32); ++i)
            if (units.images[i]) indices.push_back(i);
        return indices;
    };
    const auto earlier = range(0, blocks / 2), later = range(blocks / 2, blocks);
    const auto frame = [&](const std::vector<unsigned> &indices) {
        GAGCore::Sprite::setHighResolutionDensity(2);
        gfx->beginMapTransform(2, 0, 0, 0, 0, gfx->getW(), gfx->getH());
        for (auto index : indices) gfx->drawSprite(0.f, 0.f, &units, index);
        GAGCore::Sprite::flushBatches(gfx);
        gfx->endMapTransform();
        gfx->nextFrame();
        GAGCore::Toolkit::pollAssets(4);
        GAGCore::Sprite::pollHighResolution(4);
    };
    const auto counts = GAGCore::Sprite::highResolutionDrawCounts;
    const auto resident = [&](const std::vector<unsigned> &indices) {
        return std::count_if(indices.begin(), indices.end(), [&](unsigned i) { return units.experimentImages[i] != nullptr; });
    };
    // The earlier half fills the room the device keeps for HD frames.
    const auto roomBefore = counts().room;
    for (unsigned i = 0; i < 4000 && counts().room == roomBefore; ++i) frame(earlier);
    REQUIRE(counts().room > roomBefore);
    CHECK_FALSE(gfx->deviceHighResolutionRoom());
    const auto evictedBefore = counts().evicted;
    // While it is still being drawn, nothing goes, though new frames want room.
    for (unsigned i = 0; i < 60; ++i) frame(later);
    CHECK(counts().evicted == evictedBefore);
    // Idle long enough, the earlier blocks make room for the later ones.
    const auto drawnBefore = counts().drawn;
    for (unsigned i = 0; i < 4000 && (counts().evicted == evictedBefore || counts().drawn == drawnBefore); ++i)
        frame(later);
    CHECK(counts().evicted > evictedBefore);
    CHECK(counts().drawn > drawnBefore);
    CHECK(resident(earlier) < std::ptrdiff_t(earlier.size()));
    CHECK(resident(later) > 0);
    // Drawn again, an evicted block reloads and draws HD.
    const auto evicted = std::find_if(earlier.begin(), earlier.end(),
        [&](unsigned i) { return units.experimentImages[i] == nullptr; });
    REQUIRE(evicted != earlier.end());
    const unsigned evictedFrame = *evicted;
    // Held, uploaded frames keep drawing HD while everything else stays
    // native: nothing reloads, wants room or is evicted. Unheld, the first
    // frame would already want room, and an evicted block reloads within
    // about seven frames, so ten rounds suffice; each frame draws hundreds of
    // HD frames, which software Vulkan (CI's lavapipe) draws slowly.
    {
        struct Resume { ~Resume() { GAGCore::Sprite::holdHighResolutionAdmission(false); } } resume;
        GAGCore::Sprite::holdHighResolutionAdmission(true);
        CHECK(GAGCore::Sprite::highResolutionAdmissionHolding());
        const auto before = counts();
        for (unsigned i = 0; i < 10; ++i) { frame(later); frame(earlier); }
        CHECK(units.experimentImages[evictedFrame] == nullptr);
        CHECK(counts().evicted == before.evicted);
        CHECK(counts().room == before.room);
        CHECK(counts().drawn > before.drawn);
    }
    CHECK_FALSE(GAGCore::Sprite::highResolutionAdmissionHolding());
    for (unsigned i = 0; i < 4000 && !units.experimentImages[evictedFrame]; ++i) frame({evictedFrame});
    CHECK(units.experimentImages[evictedFrame] != nullptr);
}
}
TEST_CASE("SDL GPU evicts idle HD blocks when its room for HD frames runs out [display][isolated]") {
    checkIdleHighResolutionEviction(GAGCore::RendererBackend::SDLGPU);
}
TEST_CASE("programmable GL evicts idle HD blocks when its room for HD frames runs out [display][isolated]") {
    checkIdleHighResolutionEviction(GAGCore::RendererBackend::ProgrammableGL);
}
TEST_CASE("legacy GL draws native frames at density one and HD at two [display]") {
    checkHighResolutionPolicyOn(GAGCore::RendererBackend::LegacyGL);
}
TEST_CASE("portable SDL draws native frames at density one and HD at two [display]") {
    checkHighResolutionPolicyOn(GAGCore::RendererBackend::PortableSDL);
}
TEST_CASE("SDL GPU draws native frames at density one and HD at two [display]") {
    checkHighResolutionPolicyOn(GAGCore::RendererBackend::SDLGPU);
}
TEST_CASE("programmable GL draws native frames at density one and HD at two [display]") {
    checkHighResolutionPolicyOn(GAGCore::RendererBackend::ProgrammableGL);
}
TEST_CASE("an on-demand HD sheet releases its layers until a base frame demands them again [display]") {
    glob2test::TempDir scratch("released-demand-hd");
    const auto artwork = scratch.path / "data/gfx";
    const auto hd = scratch.path / "data/highres/v1";
    std::filesystem::create_directories(artwork);
    std::filesystem::create_directories(hd);
    std::filesystem::copy_file(glob2test::sourceRoot() / "test/fixtures/image-assets/native-sheet.webp",
        artwork / "native-sheet.webp");
    std::ofstream(artwork / "demanded-sheet.sheet") << "native-sheet.webp image 0 4 4 2\n";
    std::ofstream frame(hd / "frame.webp", std::ios::binary);
    frame.write(reinterpret_cast<const char*>(lossyAlphaFixture::webp), sizeof(lossyAlphaFixture::webp));
    frame.close();
    std::ofstream index(hd / "frames.txt");
    index << "GLOB2_HIGHRES 1\n";
    for (unsigned i = 0; i < 4; ++i) index << "demanded-sheet" << i << " 4 2 4 frame.webp -\n";
    index.close();
    glob2test::ScopedEnvironment assets("GLOB2_ASSET_DIR", scratch.path.string().c_str());
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    glob2test::ScopedEnvironment cooperative("GLOB2_ASSET_THREADS", "0");
    glob2test::ToolkitScope toolkit;
    GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::PORTABLEGPU, "released HD demand");
    struct Restore { ~Restore() { GAGCore::Sprite::setHighResolution(false); } } restore;
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/demanded-sheet"));
    GAGCore::Sprite::setHighResolution(true);
    const auto resident = [&] {
        return std::count_if(sprite.experimentImages.begin(), sprite.experimentImages.end(),
            [](auto *image) { return image != nullptr; });
    };
    // The portable renderer loads nothing until a base frame asks.
    GAGCore::Sprite::finishHighResolution();
    CHECK(resident() == 0);
    sprite.baseFrame(1);
    GAGCore::Sprite::finishHighResolution(&sprite);
    CHECK(resident() == 4);
    CHECK(sprite.baseFrame(1) != sprite.nativeFrame(1));
    sprite.releaseHighResolutionDemand();
    CHECK(resident() == 0);
    CHECK(sprite.highResolutionDemanded == false);
    // A demand in flight is cancelled too.
    CHECK(sprite.baseFrame(1) == sprite.nativeFrame(1));
    sprite.releaseHighResolutionDemand();
    GAGCore::Sprite::finishHighResolution();
    CHECK(resident() == 0);
    // And the next base frame loads the layers again.
    sprite.baseFrame(2);
    GAGCore::Sprite::finishHighResolution(&sprite);
    CHECK(resident() == 4);
}
TEST_CASE("deferred HD frames that leave their window stay decoded within a budget and a lifetime [display]") {
    glob2test::TempDir scratch("retired-terrain-hd");
    const auto artwork = scratch.path / "data/gfx";
    const auto hd = scratch.path / "data/highres/v1";
    std::filesystem::create_directories(artwork);
    std::filesystem::create_directories(hd);
    std::filesystem::copy_file(glob2test::sourceRoot() / "test/fixtures/image-assets/native-sheet.webp",
        artwork / "native-sheet.webp");
    std::ofstream(artwork / "retired-terrain.sheet") << "native-sheet.webp image 0 16 4 2\n";
    std::ofstream frame(hd / "frame.webp", std::ios::binary);
    frame.write(reinterpret_cast<const char*>(lossyAlphaFixture::webp), sizeof(lossyAlphaFixture::webp));
    frame.close();
    std::ofstream index(hd / "frames.txt");
    index << "GLOB2_HIGHRES 1\n";
    for (unsigned i = 0; i < 16; ++i) index << "retired-terrain" << i << " 4 2 4 frame.webp -\n";
    index.close();
    glob2test::ScopedEnvironment assets("GLOB2_ASSET_DIR", scratch.path.string().c_str());
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    glob2test::ScopedEnvironment cooperative("GLOB2_ASSET_THREADS", "0");
    glob2test::ToolkitScope toolkit;
    GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::PORTABLEGPU, "retired terrain HD");
    GAGCore::Sprite::setHighResolution(true);
    GAGCore::Sprite::deferHighResolution("data/gfx/retired-terrain");
    const RetiredBudget roomy({1024 * 1024, 3'600'000'000'000});
    {
        GAGCore::Sprite sprite;
        REQUIRE(sprite.load("data/gfx/retired-terrain"));
        const auto wait = [&] {
            const auto deadline = SDL_GetTicks() + 10000;
            while (!GAGCore::Toolkit::pollAssets(2) && SDL_GetTicks() < deadline) SDL_Delay(1);
            REQUIRE(GAGCore::Sprite::pollHighResolution(1));
        };
        const auto resident = [&] {
            return std::count_if(sprite.experimentImages.begin(), sprite.experimentImages.end(),
                [](auto *image) { return image != nullptr; });
        };
        const std::array<unsigned, 3> wide{1, 2, 3};
        const std::array<unsigned, 1> narrow{2};
        CHECK(sprite.highResolutionRevision() == 0);
        sprite.setHighResolutionFrames(wide);
        wait();
        REQUIRE(resident() == 3);
        CHECK(GAGCore::Sprite::retiredHighResolutionBytes() == 0);
        // Each published frame names the sprite revision it moved to; frames
        // without an HD layer stay at 0.
        const auto published = sprite.highResolutionRevision();
        CHECK(published >= 3);
        CHECK(sprite.highResolutionRevision(0) == 0);
        for (unsigned frame : wide) CHECK(sprite.highResolutionRevision(frame) > 0);
        const auto *kept = sprite.baseFrame(1);
        const auto decodes = GAGCore::AssetLoader::decodeTotals().images;

        // Leaving the window retires a frame; it still draws and returns without a decode.
        sprite.setHighResolutionFrames(narrow);
        CHECK(resident() == 3);
        CHECK(sprite.baseFrame(1) == kept);
        const auto retired = GAGCore::Sprite::retiredHighResolutionBytes();
        CHECK(retired > 0);
        sprite.setHighResolutionFrames(wide);
        wait();
        CHECK(GAGCore::Sprite::retiredHighResolutionBytes() == 0);
        CHECK(sprite.baseFrame(1) == kept);
        CHECK(GAGCore::AssetLoader::decodeTotals().images == decodes);
        // Retiring and returning changed no layer: nothing to re-record.
        CHECK(sprite.highResolutionRevision() == published);

        // Over budget, the oldest retired frame goes first.
        sprite.setHighResolutionFrames(std::array<unsigned, 2>{2, 3}); // retires 1
        sprite.setHighResolutionFrames(narrow);                        // then 3
        CHECK(resident() == 3);
        GAGCore::Sprite::setRetiredHighResolutionBudget({retired / 2, 3'600'000'000'000});
        CHECK(resident() == 2);
        CHECK(sprite.baseFrame(1) == sprite.nativeFrame(1));
        CHECK(sprite.baseFrame(3) != sprite.nativeFrame(3));
        // Eviction deletes a layer; its texture's invalidation covers geometry.
        CHECK(sprite.highResolutionRevision() == published);

        // Past its lifetime a retired frame goes at the next poll.
        GAGCore::Sprite::setRetiredHighResolutionBudget({1024 * 1024, 0});
        GAGCore::Sprite::pollHighResolution(1);
        CHECK(resident() == 1);
        CHECK(GAGCore::Sprite::retiredHighResolutionBytes() == 0);

        // A destroyed sprite takes its retired frames out of the budget.
        GAGCore::Sprite::setRetiredHighResolutionBudget({1024 * 1024, 3'600'000'000'000});
        const auto stayed = sprite.highResolutionRevision(2);
        sprite.setHighResolutionFrames(wide);
        wait();
        // Decoding the evicted frames again moves only theirs.
        CHECK(sprite.highResolutionRevision(2) == stayed);
        CHECK(sprite.highResolutionRevision(1) > published);
        CHECK(sprite.highResolutionRevision(3) > published);
        sprite.setHighResolutionFrames(narrow);
        CHECK(GAGCore::Sprite::retiredHighResolutionBytes() > 0);
    }
    CHECK(GAGCore::Sprite::retiredHighResolutionBytes() == 0);
}
TEST_CASE("drawn deferred HD frames free their pixels and return after the portable renderer is rebuilt [display]") {
    glob2test::TempDir scratch("drawn-deferred-hd");
    const auto artwork = scratch.path / "data/gfx";
    const auto hd = scratch.path / "data/highres/v1";
    std::filesystem::create_directories(artwork);
    std::filesystem::create_directories(hd);
    std::filesystem::copy_file(glob2test::sourceRoot() / "test/fixtures/image-assets/native-sheet.webp",
        artwork / "native-sheet.webp");
    std::ofstream(artwork / "drawn-decor.sheet") << "native-sheet.webp image 0 16 4 2\n";
    std::ofstream frame(hd / "frame.webp", std::ios::binary);
    frame.write(reinterpret_cast<const char*>(lossyAlphaFixture::webp), sizeof(lossyAlphaFixture::webp));
    frame.close();
    std::ofstream index(hd / "frames.txt");
    index << "GLOB2_HIGHRES 1\n";
    for (unsigned i = 0; i < 16; ++i) index << "drawn-decor" << i << " 4 2 4 frame.webp -\n";
    index.close();
    glob2test::ScopedEnvironment assets("GLOB2_ASSET_DIR", scratch.path.string().c_str());
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    glob2test::ScopedEnvironment cooperative("GLOB2_ASSET_THREADS", "0");
    glob2test::ToolkitScope toolkit;
    auto *context = GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::PORTABLEGPU, "drawn deferred HD");
    if (context->backendSwitchStatus().active != GAGCore::RendererBackend::PortableSDL) return;
    struct Restore {
        std::int64_t delay = GAGCore::Sprite::highResolutionDetailDelay();
        ~Restore() {
            // Back on native frames at once, as the next case expects.
            GAGCore::Sprite::setHighResolutionDetailDelay(0);
            GAGCore::Sprite::setHighResolutionDensity(1);
            GAGCore::Sprite::setHighResolutionDetailDelay(delay);
            GAGCore::Sprite::setHighResolution(false);
        }
    } restore;
    GAGCore::Sprite::setHighResolution(true);
    GAGCore::Sprite::deferHighResolution("data/gfx/drawn-decor");
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/drawn-decor"));
    const auto wait = [&] {
        const auto deadline = SDL_GetTicks() + 10000;
        while (!GAGCore::Toolkit::pollAssets(2) && SDL_GetTicks() < deadline) SDL_Delay(1);
        REQUIRE(GAGCore::Sprite::pollHighResolution(1));
    };
    const std::array<unsigned, 3> window{1, 2, 3};
    sprite.setHighResolutionFrames(window);
    wait();
    for (unsigned i : window) REQUIRE(sprite.experimentImages[i]);
    // Zoomed in past the portable sprite threshold, a draw uploads the frame;
    // the next one finds it uploaded and frees its pixels.
    GAGCore::Sprite::setHighResolutionDensity(4);
    REQUIRE(GAGCore::Sprite::drawsHighResolutionOnDemand());
    for (int pass = 0; pass < 3; ++pass) {
        GAGCore::Sprite::setHighResolutionDensity(4);
        context->drawSprite(0.f, 0.f, &sprite, 1);
        context->nextFrame();
    }
    auto *drawn = sprite.experimentImages[1];
    REQUIRE(drawn);
    CHECK_FALSE(drawn->hasPixels());
    CHECK(drawn->getW() == 16);
    CHECK(drawn->getH() == 8);
    CHECK(sprite.experimentImages[2]->hasPixels()); // Never drawn.
    // Leaving the window retires the frame by what it still holds in memory.
    sprite.setHighResolutionFrames(std::array<unsigned, 2>{2, 3});
    CHECK(GAGCore::Sprite::retiredHighResolutionBytes() == 0);
    sprite.setHighResolutionFrames(window);
    // Rebuilding the renderer loses the texture, the only copy: the window
    // decodes the frame again and keeps the frames that kept their pixels.
    auto *kept = sprite.experimentImages[2];
    REQUIRE(context->setRes(64, 64, context->getOptionFlags()));
    CHECK(sprite.experimentImages[1] == nullptr);
    CHECK(sprite.baseFrame(1) == sprite.nativeFrame(1));
    CHECK(sprite.experimentImages[2] == kept);
    wait();
    REQUIRE(sprite.experimentImages[1]);
    CHECK(sprite.experimentImages[1]->hasPixels());
    CHECK(sprite.experimentImages[2] == kept);
}
TEST_CASE("deferred terrain HD loads native first and bounds frame windows across toggles [display]") {
    glob2test::TempDir scratch("deferred-terrain-hd");
    const auto artwork = scratch.path / "data/gfx";
    const auto hd = scratch.path / "data/highres/v1";
    std::filesystem::create_directories(artwork);
    std::filesystem::create_directories(hd);
    std::filesystem::copy_file(glob2test::sourceRoot() / "test/fixtures/image-assets/native-sheet.webp",
        artwork / "native-sheet.webp");
    std::ofstream(artwork / "deferred-terrain.sheet") << "native-sheet.webp image 0 16 4 2\n";
    std::ofstream frame(hd / "frame.webp", std::ios::binary);
    frame.write(reinterpret_cast<const char*>(lossyAlphaFixture::webp), sizeof(lossyAlphaFixture::webp));
    frame.close();
    std::ofstream index(hd / "frames.txt");
    index << "GLOB2_HIGHRES 1\n";
    for (unsigned i = 0; i < 16; ++i)
        index << "deferred-terrain" << i << " 4 2 4 "
              << (i == 1 || i == 14 ? "frame.webp" : "missing.webp") << " -\n";
    index.close();
    glob2test::ScopedEnvironment assets("GLOB2_ASSET_DIR", scratch.path.string().c_str());
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    glob2test::ScopedEnvironment cooperative("GLOB2_ASSET_THREADS", "0");
    glob2test::ToolkitScope toolkit;
    GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::PORTABLEGPU, "deferred terrain HD");
    GAGCore::Sprite::setHighResolution(true);
    GAGCore::Sprite::deferHighResolution("data/gfx/deferred-terrain");
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/deferred-terrain"));
    REQUIRE(sprite.getFrameCount() == 16);
    const auto wait = [&] {
        const auto deadline = SDL_GetTicks() + 10000;
        while (!GAGCore::Toolkit::pollAssets(2) && SDL_GetTicks() < deadline) SDL_Delay(1);
        REQUIRE(GAGCore::Sprite::pollHighResolution(1));
    };
    const auto resident = [&] {
        return std::count_if(sprite.experimentImages.begin(), sprite.experimentImages.end(),
            [](auto *image) { return image != nullptr; });
    };
    CHECK(resident() == 0);
    for (unsigned i = 0; i < 16; ++i) CHECK(sprite.baseFrame(i) == sprite.nativeFrame(i));
    const std::array<unsigned, 2> both{1, 14};
    const std::array<unsigned, 1> one{14};
    // No retirement here: a frame outside the window is deleted at once.
    const RetiredBudget strict({0, 0});
    sprite.setHighResolutionFrames(both);
    CHECK(sprite.baseFrame(1) == sprite.nativeFrame(1));
    // Queue without executing the cooperative decoder, then advance the window.
    // The overlapping endpoint must keep its original in-flight subscription.
    CHECK_FALSE(GAGCore::Sprite::pollHighResolution(10));
    const auto submitted = GAGCore::Toolkit::assets().metrics().submitted;
    sprite.setHighResolutionFrames(one);
    CHECK_FALSE(GAGCore::Sprite::pollHighResolution(10));
    CHECK(GAGCore::Toolkit::assets().metrics().submitted == submitted);
    sprite.setHighResolutionFrames(both);
    wait();
    CHECK(resident() == 2);
    REQUIRE(sprite.baseFrame(1)->getW() == 16);
    REQUIRE(sprite.baseFrame(14)->getW() == 16);
    sprite.setHighResolutionFrames(one);
    CHECK(resident() == 1);
    CHECK(sprite.baseFrame(1) == sprite.nativeFrame(1));
    GAGCore::Sprite::setHighResolution(false);
    CHECK(resident() == 0);
    GAGCore::Sprite::setHighResolution(true);
    CHECK(resident() == 1);
    CHECK(sprite.baseFrame(14)->getW() == 16);
    CHECK(sprite.baseFrame(1) == sprite.nativeFrame(1));
    // Cancel pending reads when the visible window disappears.
    sprite.setHighResolutionFrames(both);
    GAGCore::Sprite::pollHighResolution(1);
    sprite.setHighResolutionFrames({});
    wait();
    CHECK(resident() == 0);
    for (unsigned i = 0; i < 16; ++i) REQUIRE(sprite.nativeFrame(i));
}

static void checkShippedWaterHD(bool programmable) {
    // Keep the configured packaged runtime asset root: source PNG paths are not
    // the WebP inputs exercised by application startup.
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", "");
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    glob2test::ToolkitScope toolkit;
    // The window alone decides what is resident here.
    const RetiredBudget strict({0, 0});
    auto *context = GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::PORTABLEGPU, "shipped terrain HD window");
#ifdef HAVE_OPENGL
    if (programmable) {
        REQUIRE(context->requestBackendSwitch(GAGCore::RendererBackend::ProgrammableGL));
        const auto deadline = SDL_GetTicks() + 10000;
        do { context->nextFrame(); SDL_Delay(1); }
        while (context->backendSwitchStatus().active != GAGCore::RendererBackend::ProgrammableGL && SDL_GetTicks() < deadline);
        REQUIRE(context->backendSwitchStatus().active == GAGCore::RendererBackend::ProgrammableGL);
    }
#endif
    GAGCore::Sprite::setHighResolution(true);
    GAGCore::Sprite::deferHighResolution("data/gfx/terrain-water");
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/terrain-water"));
    // Water is one 4x4 wave block: 16 variants in each of 16 phases.
    constexpr unsigned variants = 16, frames = variants * 16;
    REQUIRE(sprite.getFrameCount() == frames);
    for (unsigned frame = 0; frame < frames; ++frame) {
        REQUIRE(sprite.nativeFrame(frame));
        CHECK(sprite.nativeFrame(frame)->getW() == 32);
        CHECK(sprite.nativeFrame(frame)->getH() == 32);
    }
    const auto resident = [&] {
        return std::count_if(sprite.experimentImages.begin(), sprite.experimentImages.end(),
            [](auto *image) { return image != nullptr; });
    };
    const auto wait = [&] {
        const auto deadline = SDL_GetTicks() + 30000;
        while (!GAGCore::Toolkit::pollAssets(2) && SDL_GetTicks() < deadline) SDL_Delay(1);
        REQUIRE(GAGCore::Sprite::pollHighResolution(1));
    };
    CHECK(resident() == 0);
    const std::array<unsigned, 2> loopEndpoints{frames - 1, variants - 1};
    sprite.setHighResolutionFrames(loopEndpoints);
    wait();
    REQUIRE(resident() == 2);
    for (unsigned frame : loopEndpoints) {
        REQUIRE(sprite.experimentImages[frame]);
        CHECK(sprite.baseFrame(frame)->getW() == 128);
        CHECK(sprite.baseFrame(frame)->getH() == 128);
    }
    const auto retained = sprite.baseFrame(variants - 1)->lifetimeIdentity();
    const std::array<unsigned, 2> nextEndpoints{variants - 1, 2 * variants - 1};
    sprite.setHighResolutionFrames(nextEndpoints);
    CHECK(sprite.experimentImages[frames - 1] == nullptr);
    CHECK(sprite.baseFrame(variants - 1)->lifetimeIdentity() == retained);
    wait();
    REQUIRE(resident() == 2);
    CHECK(sprite.baseFrame(variants - 1)->lifetimeIdentity() == retained);
    CHECK(sprite.baseFrame(2 * variants - 1)->getW() == 128);
    CHECK(sprite.baseFrame(2 * variants - 1)->getH() == 128);
    CHECK(sprite.nativeFrame(frames - 1)->getW() == 32);
    CHECK(sprite.experimentImages[variants - 1]->preparedMips.empty());
    context->drawFilledRect(0, 0, 64, 64, GAGCore::Color(0, 0, 0));
    auto *native = sprite.nativeFrame(0), *hd = sprite.baseFrame(variants - 1);
    context->drawSurface(0, 0, 32, 32, native, 0, 0, 32, 32);
    context->drawSurface(32, 0, 32, 32, hd, 0, 0, 128, 128);
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(nullptr, SDL_DestroySurface);
    if (context->renderDevice()) {
        context->nextFrame();
        GAGCore::CaptureRequest capture;
        pixels.reset(context->renderDevice()->captureSynchronously(capture));
    } else {
        context->renderer->flush(); pixels.reset(context->renderer->capture());
    }
    REQUIRE(pixels);
    for (int x : {16, 48}) {
        Uint8 red, green, blue, alpha;
        REQUIRE(SDL_ReadSurfacePixel(pixels.get(), x, 16, &red, &green, &blue, &alpha));
        CHECK(blue > red); CHECK(blue > green); CHECK(alpha == 255);
    }
    // A complete current/next bank must eventually resolve under the ordinary
    // polling budget, even when no animation time is allowed for warm-up.
    std::vector<unsigned> fullWindow;
    for (unsigned variant = 0; variant < variants; ++variant) {
        fullWindow.push_back(variant); fullWindow.push_back(variants + variant);
    }
    sprite.setHighResolutionFrames(fullWindow);
    wait();
    REQUIRE(resident() == 2 * variants);
    for (unsigned frame : fullWindow) {
        REQUIRE(sprite.experimentImages[frame]);
        CHECK(sprite.baseFrame(frame)->getW() == 128);
    }
    sprite.setHighResolutionFrames({});
    wait();
    CHECK(resident() == 0);
    CHECK(sprite.nativeFrame(frames - 1)->getW() == 32);
}

TEST_CASE("shipped animated water bank streams only requested HD endpoints [display]") {
    checkShippedWaterHD(false);
}
#ifdef HAVE_OPENGL
TEST_CASE("shipped animated water bank source frames draw through programmable GL [display][writes-preferences]") {
    checkShippedWaterHD(true);
}
#endif

TEST_CASE("shipped water HD keeps up with moving full-variant animation windows [display][artifacts][benchmark]") {
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", "");
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    glob2test::ToolkitScope toolkit;
    GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::PORTABLEGPU, "water HD cadence");
    // Measures decoding against the window alone, without retired frames.
    const RetiredBudget strict({0, 0});
    GAGCore::Sprite::setHighResolution(true);
    GAGCore::Sprite::deferHighResolution("data/gfx/terrain-water");
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/terrain-water"));
    constexpr unsigned variants = 16, phases = 16, ticks = 6;
    REQUIRE(sprite.getFrameCount() == variants * phases);
    std::vector<unsigned> window;
    window.reserve(variants * 2);
    std::size_t readySum = 0, samples = 0, peak = 0, peakResident = 0;
    for (unsigned tick = 0; tick < phases * ticks * 2; ++tick) {
        const unsigned phase = tick / ticks % phases;
        window.clear();
        for (unsigned variant = 0; variant < variants; ++variant) {
            window.push_back(phase * variants + variant);
            window.push_back((phase + 1) % phases * variants + variant);
        }
        sprite.setHighResolutionFrames(window);
        GAGCore::Toolkit::pollAssets(2);
        const auto resident = std::count_if(sprite.experimentImages.begin(), sprite.experimentImages.end(),
            [](auto *image) { return image != nullptr; });
        CHECK(resident <= variants * 2);
        peakResident = std::max(peakResident, std::size_t(resident));
        if (tick >= phases * ticks) {
            std::size_t ready = 0;
            for (unsigned variant = 0; variant < variants; ++variant)
                ready += sprite.experimentImages[phase * variants + variant] != nullptr;
            readySum += ready; ++samples; peak = std::max(peak, ready);
        }
        SDL_Delay(16);
    }
    std::ofstream evidence(glob2test::artifactDir() / "water-hd-cadence.txt");
    evidence << "variants " << variants << "\nphase_polls " << ticks << "\nmeasured_polls " << samples
             << "\ncurrent_hd_average " << double(readySum) / samples
             << "\ncurrent_hd_fraction " << double(readySum) / (samples * variants)
             << "\ncurrent_hd_peak " << peak << "\nresident_peak " << peakResident
             << "\nasset_peak_working_bytes " << GAGCore::Toolkit::assets().metrics().peakWorkingBytes << '\n';
    // Coverage is diagnostic: fast development builds and CI hardware do not
    // establish shipping throughput. Bounded residency and progress are required.
    CHECK(readySum > 0);
}

// Lava streams like a deferred multi-layout bank. The compositor's windows are
// emulated: every variant visible, either the current/next endpoints alone or
// every phase kept resident (the look-ahead used when it fits the budget).
TEST_CASE("lava HD look-ahead keeps streamed animation endpoints resident [display][artifacts][benchmark]") {
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", "");
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    glob2test::ToolkitScope toolkit;
    GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::PORTABLEGPU, "lava HD cadence");
    // Measures decoding against the window alone, without retired frames.
    const RetiredBudget strict({0, 0});
    GAGCore::Sprite::setHighResolution(true);
    GAGCore::Sprite::deferHighResolution("data/gfx/terrain-lava");
    std::ofstream evidence(glob2test::artifactDir() / "lava-hd-cadence.txt");
    for (bool lookahead : {false, true}) {
        GAGCore::Sprite sprite;
        REQUIRE(sprite.load("data/gfx/terrain-lava"));
        constexpr unsigned phases = 4, ticks = 8;
        const unsigned variants = sprite.getFrameCount() / phases;
        REQUIRE(variants * phases == sprite.getFrameCount());
        std::vector<unsigned> window;
        std::size_t readySum = 0, samples = 0, fallbackTicks = 0, peakResident = 0;
        for (unsigned tick = 0; tick < phases * ticks * 3; ++tick) {
            const unsigned phase = tick / ticks % phases;
            window.clear();
            for (unsigned variant = 0; variant < variants; ++variant) {
                window.push_back(phase * variants + variant);
                window.push_back((phase + 1) % phases * variants + variant);
            }
            for (unsigned ahead = 2; lookahead && ahead < phases; ++ahead)
                for (unsigned variant = 0; variant < variants; ++variant)
                    window.push_back((phase + ahead) % phases * variants + variant);
            sprite.setHighResolutionFrames(window);
            GAGCore::Toolkit::pollAssets(2);
            const auto resident = std::size_t(std::count_if(sprite.experimentImages.begin(),
                sprite.experimentImages.end(), [](auto *image) { return image != nullptr; }));
            CHECK(resident <= variants * (lookahead ? phases : 2));
            peakResident = std::max(peakResident, resident);
            // The first cycle warms up; later cycles measure steady streaming.
            if (tick >= phases * ticks) {
                std::size_t ready = 0;
                for (unsigned variant = 0; variant < variants; ++variant)
                    for (unsigned endpoint : {phase, (phase + 1) % phases})
                        ready += sprite.experimentImages[endpoint * variants + variant] != nullptr;
                readySum += ready; ++samples;
                fallbackTicks += ready < 2 * variants;
            }
            SDL_Delay(16);
        }
        evidence << (lookahead ? "lookahead" : "endpoints") << " variants " << variants << " phase_ticks " << ticks
                 << " measured_ticks " << samples << " endpoint_hd_fraction " << double(readySum) / (samples * 2 * variants)
                 << " fallback_ticks " << fallbackTicks << " resident_peak " << peakResident << '\n';
        // Diagnostic coverage: development builds and CI hardware do not
        // establish shipping throughput. Bounded residency and progress are required.
        CHECK(readySum > 0);
        sprite.setHighResolutionFrames({});
    }
    evidence << "asset_peak_working_bytes " << GAGCore::Toolkit::assets().metrics().peakWorkingBytes << '\n';
}

#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
TEST_CASE("the unit sheet keeps no HD pixels after upload and reloads once its textures are lost [display]") {
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", "");
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    glob2test::ToolkitScope toolkit;
    auto *context = GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::USEGPU, "unit HD pixels");
    REQUIRE(context->hasUnitShader());
    struct RestoreNativeArt { ~RestoreNativeArt() { GAGCore::Sprite::setHighResolution(false); } } restoreNativeArt;
    GAGCore::Sprite::setHighResolution(true);
    GAGCore::Sprite units;
    REQUIRE(units.load("data/gfx/unit"));
    GAGCore::Sprite::finishHighResolution();
    unsigned index = 0;
    while (index < units.experimentImages.size() && !(units.experimentImages[index] && units.experimentRotated[index])) ++index;
    REQUIRE(index < units.experimentImages.size());
    const auto requireReleased = [&] {
        for (auto *layer : {units.experimentImages[index], units.experimentRotated[index]->orig}) {
            REQUIRE(layer);
            CHECK_FALSE(layer->hasPixels());
            CHECK(layer->texture != 0);
            CHECK(layer->getW() == GAGCore::Sprite::highResolutionTextureSize);
            CHECK(layer->getH() == GAGCore::Sprite::highResolutionTextureSize);
        }
        std::size_t pixels = 1, textures = 0;
        for (const auto &usage : GAGCore::RenderMemory::sprites())
            if (usage.name == "data/gfx/unit") { pixels = usage.highResolutionCpuBytes; textures = usage.gpuBytes; }
        CHECK(pixels == 0);
        CHECK(textures > 0);
    };
    requireReleased();
    // The shader draws both layers from their textures alone. drawFrame, not
    // baseFrame: CPU sampling would keep the HD pixels from then on.
    context->drawSprite(0, 0, &units, index);
    context->nextFrame();
    CHECK(units.drawFrame(index) == units.experimentImages[index]);

    // A context rebuild frees every texture. Frames without pixels have nothing
    // left: the sheet draws native artwork at once and reloads HD from disk.
    GAGCore::GraphicContext::releaseSurfaceTextures();
    CHECK(units.experimentImages[index] == nullptr);
    CHECK(units.experimentRotated[index] == nullptr);
    REQUIRE(units.drawFrame(index) == units.images[index]);
    CHECK(units.drawFrame(index)->hasPixels());
    GAGCore::GraphicContext::restoreSurfaceTextures();
    context->drawSprite(0, 0, &units, index);
    context->nextFrame();
    GAGCore::Sprite::finishHighResolution();
    requireReleased();
    context->drawSprite(0, 0, &units, index);
    context->nextFrame();
}
#endif
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
TEST_CASE("HD sprite textures follow the zoom's detail band and free covered native textures [display]") {
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", "");
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    glob2test::ToolkitScope toolkit;
    auto *context = GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::USEGPU, "HD detail bands");
    // The policy starts from the display's raster scale: pin density one, so a
    // 2x (HiDPI) display starts at the same HD level as a 1x one.
    GAGCore::Sprite::resetHighResolutionPolicy(1);
    REQUIRE(context->hasUnitShader());
    struct Restore {
        ~Restore() {
            GAGCore::Sprite::setHighResolutionDetailDelay(10'000'000'000);
            GAGCore::Sprite::setHighResolution(false);
        }
    } restore;
    GAGCore::Sprite::setHighResolution(true);
    GAGCore::Sprite units;
    REQUIRE(units.load("data/gfx/unit"));
    // Before any map view, the display's scale picks the band: one screen pixel
    // for each logical pixel needs the 32-texel level of a 128-texel frame.
    GAGCore::Sprite::finishHighResolution();
    unsigned index = 0;
    while (index < units.experimentImages.size() && !units.experimentImages[index]) ++index;
    REQUIRE(index < units.experimentImages.size());
    const auto levelZero = [&] {
        auto *hd = units.experimentImages[index];
        REQUIRE(hd);
        REQUIRE(hd->texture != 0);
        GLint width = 0;
        glBindTexture(GL_TEXTURE_2D, hd->texture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        return width;
    };
    CHECK(GAGCore::Sprite::highResolutionDetailSkip() == 2);
    CHECK(levelZero() == 32);
    // HD frames cover the native frames, whose textures go; their pixels stay.
    CHECK(units.images[index]->texture == 0);
    CHECK(units.images[index]->hasPixels());
    const auto settle = [&] {
        // A wait, not a speed check: slow software-GL runners (GCC 11, Xvfb)
        // took longer than the former 2000-frame bound to publish the detail.
        const auto deadline = SDL_GetTicks() + 120000;
        while (!GAGCore::Sprite::pollHighResolution(4) && SDL_GetTicks() < deadline)
            GAGCore::Toolkit::pollAssets(4);
        REQUIRE(GAGCore::Sprite::pollHighResolution(0));
    };
    // More detail loads at once; the old textures draw until it is published.
    auto *before = units.experimentImages[index];
    GAGCore::Sprite::setHighResolutionDensity(4);
    CHECK(GAGCore::Sprite::highResolutionDetailSkip() == 0);
    CHECK(units.experimentImages[index] == before);
    settle();
    CHECK(levelZero() == 128);
    // Less detail waits until the view has wanted it for the delay.
    GAGCore::Sprite::setHighResolutionDensity(1);
    CHECK(GAGCore::Sprite::highResolutionDetailSkip() == 0);
    GAGCore::Sprite::setHighResolutionDetailDelay(0);
    GAGCore::Sprite::setHighResolutionDensity(1);
    CHECK(GAGCore::Sprite::highResolutionDetailSkip() == 2);
    settle();
    CHECK(levelZero() == 32);
    // A native frame drawn again (here, as a fallback) uploads its pixels again.
    context->drawSurface(0, 0, units.images[index]);
    context->nextFrame();
    CHECK(units.images[index]->texture != 0);
}
TEST_CASE("team-coloured buildings draw through the shader without recoloured copies [display]") {
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", "");
    glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
    glob2test::ToolkitScope toolkit;
    auto *context = GAGCore::Toolkit::initGraphic(256, 256, GAGCore::GraphicContext::USEGPU, "building team shader");
    // The policy starts from the display's raster scale: pin density one, so a
    // 2x (HiDPI) display starts at the same HD level as a 1x one.
    GAGCore::Sprite::resetHighResolutionPolicy(1);
    REQUIRE(context->hasUnitShader());
    struct RestoreNativeArt { ~RestoreNativeArt() { GAGCore::Sprite::setHighResolution(false); } } restoreNativeArt;
    GAGCore::Sprite::setHighResolution(true);
    GAGCore::Sprite building;
    REQUIRE(building.load("data/gfx/swarm0b"));
    GAGCore::Sprite::finishHighResolution();
    REQUIRE_FALSE(building.isDynamicTeamColor());
    unsigned index = 0;
    while (index < building.rotated.size() && !building.rotated[index]) ++index;
    REQUIRE(index < building.rotated.size());
    const auto capture = [&](const GAGCore::Color &team) {
        building.setBaseColor(team);
        context->drawFilledRect(0, 0, 256, 256, 20, 30, 40);
        context->drawSprite(8, 8, &building, index);
        context->drawSprite(8, 8, 96, 96, &building, index); // The sized overload takes the same path.
        context->finishDrawingSprite(&building, 255);
        glFinish();
        // The whole drawable: on a 2x (HiDPI) display it holds four pixels a
        // logical one, and GL's rows start at its bottom.
        const int width = context->getDrawableW(), height = context->getDrawableH();
        std::vector<unsigned char> pixels(std::size_t(width) * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        context->nextFrame();
        return pixels;
    };
    const auto red = capture(GAGCore::Color(220, 30, 30)), blue = capture(GAGCore::Color(30, 30, 220));
    // The team layer took each colour, and no recoloured copy was made for it.
    CHECK(red != blue);
    CHECK(std::count_if(red.begin(), red.end(), [](unsigned char c) { return c > 60; }) > 500);
    CHECK(building.rotated[index]->rotationMap.empty());
    if (index < building.experimentRotated.size() && building.experimentRotated[index]) {
        CHECK(building.experimentRotated[index]->rotationMap.empty());
        // With the shader drawing it, the HD team layer keeps no pixels either.
        CHECK_FALSE(building.experimentRotated[index]->orig->hasPixels());
    }
}
#endif
TEST_CASE("portable HD uses standalone frames without requesting GL atlases [display]") {
    glob2test::TempDir scratch("portable-hd");
    const auto artwork = scratch.path / "data/gfx";
    const auto hd = scratch.path / "hd";
    std::filesystem::create_directories(artwork);
    std::filesystem::create_directories(hd);
    std::filesystem::copy_file(glob2test::sourceRoot() / "test/fixtures/image-assets/native-sheet.webp",
        artwork / "native-sheet.webp");
    std::ofstream(artwork / "terrain.sheet") << "native-sheet.webp image 0 16 4 2\n";
    std::ofstream frame(hd / "frame.webp", std::ios::binary);
    frame.write(reinterpret_cast<const char*>(lossyAlphaFixture::webp), sizeof(lossyAlphaFixture::webp));
    frame.close();
    std::ofstream index(hd / "frames.txt");
    index << "GLOB2_HIGHRES 1\n";
    for (int i = 0; i < 16; ++i) {
        std::filesystem::copy_file(hd / "frame.webp", hd / ("frame" + std::to_string(i) + ".webp"));
        index << "terrain" << i << " 4 2 4 frame" << i << ".webp -\n";
    }
    index.close();
    glob2test::ScopedEnvironment assets("GLOB2_ASSET_DIR", scratch.path.string().c_str());
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
    glob2test::ToolkitScope toolkit;
    GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::PORTABLEGPU, "portable HD frames");
    // The policy starts from the display's raster scale: pin density one, so a
    // 2x (HiDPI) display starts at the same HD level as a 1x one.
    GAGCore::Sprite::resetHighResolutionPolicy(1);
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/terrain"));
    // Each requested frame contributes its base image; no four GL atlas mips.
    CHECK(GAGCore::Sprite::prefetchHighResolution("data/gfx/terrain", 16).size() == 16);
    {
        auto inputs = GAGCore::prefetchHighResolutionIncremental("data/gfx/terrain", 16);
        for (int i = 0; i < 16; ++i) {
            const auto state = inputs.state(hd.string() + "/frame" + std::to_string(i) + ".webp", false);
            CHECK((state != GAGCore::AssetLoader::State::Cancelled) == (i < 8));
        }
        GAGCore::highResolutionFrameReady("data/gfx/terrain", 8, inputs);
        CHECK(inputs.state(hd.string() + "/frame8.webp", false) != GAGCore::AssetLoader::State::Cancelled);
    }
    for (bool shared : {false, true}) {
        auto inputs = GAGCore::Sprite::prefetchHighResolution("data/gfx/terrain", 1);
        auto inspect = GAGCore::Toolkit::assets().requestImage(hd.string() + "/frame0.webp",
            GAGCore::AssetLoader::Priority::Required, false);
        REQUIRE(GAGCore::Toolkit::assets().wait(inspect));
        CHECK(inspect.get()->mips.empty()); // SDL sampling never consumes CPU mip levels.
        std::weak_ptr<const GAGCore::AssetImage> decoded = inspect.get();
        auto* pixels = inspect.get()->surface;
        auto independent = shared ? inspect.retain() : GAGCore::AssetLoader::Handle<GAGCore::AssetImage>{};
        inspect = {};
        GAGCore::Sprite prepared;
        sprite.appendHighResolutionFrame(0, prepared, inputs);
        REQUIRE(prepared.experimentImages[0]);
        CHECK(prepared.experimentImages[0]->preparedMips.empty());
        CHECK(inputs.size() == 0);
        if (shared) {
            REQUIRE(independent.get());
            CHECK(independent.get()->surface == pixels);
            CHECK(prepared.experimentImages[0]->getSDLSurface() != pixels);
        } else {
            CHECK(decoded.expired());
            CHECK(prepared.experimentImages[0]->getSDLSurface() == pixels);
        }
    }
    for (unsigned i = 0; i < 16; ++i) {
        REQUIRE(sprite.baseFrame(i));
        CHECK(sprite.baseFrame(i)->getW() == 16);
        CHECK(sprite.baseFrame(i)->getH() == 8);
        CHECK(sprite.getW(i) == 4);
        CHECK(sprite.getH(i) == 2);
    }
#ifdef HAVE_OPENGL
    CHECK(sprite.highResolutionAtlas == nullptr);
#endif
    // A failed optional frame is terminal for this prefetch, rather than
    // restarting its decode on every readiness poll and hanging publication.
    std::ofstream missing(hd / "frames.txt");
    missing << "GLOB2_HIGHRES 1\n";
    for (int i = 0; i < 16; ++i) missing << "terrain" << i << " 4 2 4 missing.webp -\n";
    missing.close();
    GAGCore::Sprite::requestHighResolution(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool complete = false;
    while (std::chrono::steady_clock::now() < deadline) {
        GAGCore::Toolkit::assets().poll();
        if ((complete = GAGCore::Sprite::pollHighResolution(1))) break;
        SDL_Delay(1);
    }
    REQUIRE(complete);
    REQUIRE(sprite.baseFrame(0));
    CHECK(sprite.baseFrame(0)->getW() == 4);
}

namespace {
void checkModernStandaloneHD(GAGCore::RendererBackend backend, bool switchDuringDecode = false, bool publishBeforeSwitch = false, bool failUpgrade = false, bool initialLegacy = false)
{
    glob2test::TempDir scratch("modern-hd");
    const auto artwork = scratch.path / "data/gfx";
    const auto hd = scratch.path / "hd";
    std::filesystem::create_directories(artwork);
    std::filesystem::create_directories(hd);
    std::filesystem::copy_file(glob2test::sourceRoot() / "test/fixtures/image-assets/native-sheet.webp",
        artwork / "native-sheet.webp");
    std::ofstream(artwork / "terrain.sheet") << "native-sheet.webp image 0 16 4 2\n";
    std::ofstream frame(hd / "frame.webp", std::ios::binary);
    frame.write(reinterpret_cast<const char*>(lossyAlphaFixture::webp), sizeof(lossyAlphaFixture::webp));
    frame.close();
    std::ofstream index(hd / "frames.txt");
    index << "GLOB2_HIGHRES 1\n";
    for (int i = 0; i < 16; ++i) index << "terrain" << i << " 4 2 4 frame.webp -\n";
    index.close();
    glob2test::ScopedEnvironment assets("GLOB2_ASSET_DIR", scratch.path.string().c_str());
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
    const std::string requested(GAGCore::rendererBackendName((switchDuringDecode || publishBeforeSwitch) ? (initialLegacy ? GAGCore::RendererBackend::LegacyGL : GAGCore::RendererBackend::PortableSDL) : backend));
    glob2test::ScopedEnvironment selection("GLOB2_RENDERER", requested.c_str());
    glob2test::ToolkitScope toolkit;
    auto *context = GAGCore::Toolkit::initGraphic(64, 64, 0, "modern standalone HD frames");
    // The policy starts from the display's raster scale: pin density one, so a
    // 2x (HiDPI) display starts at the same HD level as a 1x one.
    GAGCore::Sprite::resetHighResolutionPolicy(1);
    const auto choices = context->rendererAvailability();
    const auto choice = std::find_if(choices.begin(), choices.end(), [backend](const auto &entry) { return entry.backend == backend; });
    REQUIRE(choice != choices.end());
    if (!choice->available) {
        MESSAGE("HD backend coverage unavailable: ", choice->reason);
        return;
    }
    if (initialLegacy && context->backendSwitchStatus().active != GAGCore::RendererBackend::LegacyGL) {
        MESSAGE("Legacy HD boundary coverage unavailable in this build.");
        return;
    }
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/terrain"));
    auto inputs = GAGCore::Sprite::prefetchHighResolution("data/gfx/terrain", 16);
    REQUIRE(inputs.size() == (initialLegacy ? 20 : 16)); // optional legacy atlas files are absent
    if (publishBeforeSwitch) {
        // Outside legacy GL, layers load once something samples them.
        sprite.demandHighResolution();
        GAGCore::Sprite::setHighResolution(true);
        REQUIRE(sprite.experimentImages[0]);
        CHECK(sprite.experimentImages[0]->preparedMips.empty());
        auto *published = sprite.experimentImages[0];
        if (failUpgrade) std::filesystem::remove(hd / "frame.webp");
        context->drawFilledRect(0, 0, context->getW(), context->getH(), GAGCore::Color(0, 0, 0));
        context->nextFrame();
        REQUIRE(context->requestBackendSwitch(backend));
        auto deadline = SDL_GetTicks() + 10000;
        do { context->nextFrame(); SDL_Delay(1); }
        while (context->backendSwitchStatus().active != backend && SDL_GetTicks() < deadline);
        REQUIRE(context->backendSwitchStatus().active == backend);
        CHECK(sprite.experimentImages[0] == published);
        CHECK_FALSE(GAGCore::Sprite::pollHighResolution(0));
        bool complete = false;
        while (!(complete = GAGCore::Sprite::pollHighResolution(1)) && SDL_GetTicks() < deadline) {
            CHECK(sprite.experimentImages[0] == published);
            GAGCore::Toolkit::assets().poll(); SDL_Delay(1);
        }
        REQUIRE(complete);
        REQUIRE(sprite.experimentImages[0]);
        if (failUpgrade) CHECK(sprite.experimentImages[0] == published);
        else CHECK(sprite.experimentImages[0]->preparedMips.size() == 5);
        const auto submitted = GAGCore::Toolkit::assets().metrics().submitted;
        for (int repeat = 0; repeat < 3; ++repeat) GAGCore::Sprite::refreshHighResolutionForRenderer();
        CHECK(GAGCore::Toolkit::assets().metrics().submitted == submitted);
        REQUIRE(context->requestBackendSwitch(GAGCore::RendererBackend::PortableSDL));
        deadline = SDL_GetTicks() + 10000;
        do { context->nextFrame(); SDL_Delay(1); }
        while (context->backendSwitchStatus().active != GAGCore::RendererBackend::PortableSDL && SDL_GetTicks() < deadline);
        REQUIRE(context->backendSwitchStatus().active == GAGCore::RendererBackend::PortableSDL);
        while (!(complete = GAGCore::Sprite::pollHighResolution(1)) && SDL_GetTicks() < deadline) {
            GAGCore::Toolkit::assets().poll(); SDL_Delay(1);
        }
        REQUIRE(complete); REQUIRE(sprite.experimentImages[0]);
        CHECK(sprite.experimentImages[0]->getW() == 16);
        CHECK(sprite.experimentImages[0]->getH() == 8);
        context->drawSurface(0, 0, 4, 2, sprite.experimentImages[0]);
        // Portable SDL clears its backbuffer after presentation. Inspect the
        // submitted artwork before presenting, as its capture contract requires.
        context->renderer->flush();
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(context->renderer->capture(), SDL_DestroySurface);
        REQUIRE(pixels);
        bool visible = false;
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) {
            Uint8 red, green, blue, alpha;
            Uint32 pixel = 0;
            std::memcpy(&pixel, static_cast<Uint8*>(pixels->pixels) + y*pixels->pitch + x*4, 4);
            SDL_GetRGBA(pixel, SDL_GetPixelFormatDetails(pixels->format), nullptr, &red, &green, &blue, &alpha);
            visible = visible || red || green || blue;
        }
        CHECK(visible);
        return;
    }
    if (switchDuringDecode) {
        CHECK_FALSE(context->rendererCapabilities().textureMipmaps);
        context->drawFilledRect(0, 0, context->getW(), context->getH(), GAGCore::Color(0, 0, 0));
        context->nextFrame();
        REQUIRE(context->requestBackendSwitch(backend));
        const auto deadline = SDL_GetTicks() + 5000;
        do { context->nextFrame(); SDL_Delay(1); }
        while (context->backendSwitchStatus().active != backend && SDL_GetTicks() < deadline);
        REQUIRE(context->backendSwitchStatus().active == backend);
        bool ready = false;
        while (!(ready = GAGCore::highResolutionFrameReady("data/gfx/terrain", 0, inputs)) && SDL_GetTicks() < deadline) {
            GAGCore::Toolkit::assets().poll(); SDL_Delay(1);
        }
        REQUIRE(ready);
    }
    REQUIRE(context->backendSwitchStatus().requested == backend);
    REQUIRE(context->backendSwitchStatus().active == backend);
    REQUIRE(context->rendererCapabilities().textureFiltering);
    REQUIRE(context->rendererCapabilities().textureMipmaps);
    // Modern devices intentionally clear both legacy option bits.
    CHECK((context->getOptionFlags() & (GAGCore::GraphicContext::USEGPU |
        GAGCore::GraphicContext::PORTABLEGPU)) == 0);
    auto inspect = GAGCore::Toolkit::assets().requestImage(hd.string() + "/frame.webp",
        GAGCore::AssetLoader::Priority::Required, true);
    REQUIRE(GAGCore::Toolkit::assets().wait(inspect));
    REQUIRE(inspect.get()->mips.size() == 5);
    CHECK(inspect.get()->mips.front().width == 16);
    CHECK(inspect.get()->mips.front().height == 8);
    CHECK(inspect.get()->mips.back().width == 1);
    CHECK(inspect.get()->mips.back().height == 1);
    inspect = {};
    GAGCore::Sprite prepared;
    sprite.appendHighResolutionFrame(0, prepared, inputs);
    REQUIRE(prepared.experimentImages[0]);
    REQUIRE(prepared.experimentImages[0]->preparedMips.size() == 5);
    CHECK(prepared.experimentImages[0]->preparedMips.back().width == 1);
    if (switchDuringDecode) {
        context->drawSurface(0, 0, 4, 2, prepared.experimentImages[0]);
        context->nextFrame();
        REQUIRE(context->renderDevice());
        GAGCore::CaptureRequest capture;
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
            context->renderDevice()->captureSynchronously(capture), SDL_DestroySurface);
        REQUIRE(pixels);
        bool visible = false;
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) {
            Uint32 value = 0;
            std::memcpy(&value, static_cast<Uint8*>(pixels->pixels) + y*pixels->pitch + x*4, 4);
            Uint8 red, green, blue, alpha;
            SDL_GetRGBA(value, SDL_GetPixelFormatDetails(pixels->format), nullptr, &red, &green, &blue, &alpha);
            visible = visible || red || green || blue;
        }
        CHECK(visible);
    } else {
        for (unsigned i = 0; i < 16; ++i) {
            REQUIRE(sprite.baseFrame(i));
            CHECK(sprite.baseFrame(i)->getW() == 16);
            CHECK(sprite.baseFrame(i)->getH() == 8);
            CHECK(sprite.baseFrame(i)->preparedMips.size() == 5);
            CHECK(sprite.getW(i) == 4);
            CHECK(sprite.getH(i) == 2);
        }
    }
#ifdef HAVE_OPENGL
    CHECK(sprite.highResolutionAtlas == nullptr);
#endif
}
}
TEST_CASE("portable HD prefetch upgrades across a live programmable GL switch [display][writes-preferences]") {
    checkModernStandaloneHD(GAGCore::RendererBackend::ProgrammableGL, true);
}
TEST_CASE("published portable HD upgrades after a live GL switch and survives switching back [display][writes-preferences]") {
    checkModernStandaloneHD(GAGCore::RendererBackend::ProgrammableGL, false, true);
}
TEST_CASE("legacy HD without retained CPU levels restores authored mips on a live GL device switch [display][writes-preferences]") {
    checkModernStandaloneHD(GAGCore::RendererBackend::ProgrammableGL, false, true, false, true);
}
TEST_CASE("failed published HD mip upgrade retains valid artwork without retry [display][writes-preferences]") {
    checkModernStandaloneHD(GAGCore::RendererBackend::ProgrammableGL, false, true, true);
}
TEST_CASE("programmable GL retains standalone HD authored mips [display]") {
    checkModernStandaloneHD(GAGCore::RendererBackend::ProgrammableGL);
}
#ifndef __EMSCRIPTEN__
TEST_CASE("SDL GPU retains standalone HD authored mips [display]") {
    checkModernStandaloneHD(GAGCore::RendererBackend::SDLGPU);
}
#endif

#ifdef HAVE_OPENGL
TEST_CASE("independently encoded lossy HD atlases keep exact frame alpha [display]") {
    glob2test::TempDir scratch("lossy-atlas");
    auto artwork = scratch.path / "data/gfx";
    auto hd = scratch.path / "hd";
    std::filesystem::create_directories(artwork);
    std::filesystem::create_directories(hd);
    auto fixtures = glob2test::sourceRoot() / "test/fixtures/image-assets";
    std::filesystem::copy_file(fixtures / "native-sheet.webp", artwork / "native-sheet.webp");
    std::ofstream(artwork / "terrain.sheet") << "native-sheet.webp image 0 16 4 2\n";
    std::ofstream frame(hd / "frame.webp", std::ios::binary);
    frame.write(reinterpret_cast<const char*>(lossyAlphaFixture::webp), sizeof(lossyAlphaFixture::webp));
    frame.close();
    std::ofstream index(hd / "frames.txt");
    index << "GLOB2_HIGHRES 1\n";
    for (int i = 0; i < 16; ++i) index << "terrain" << i << " 4 2 4 frame.webp -\n";
    index.close();
    for (int i = 0; i < 4; ++i) {
        auto name = "terrain-atlas-mip" + std::to_string(i) + ".webp";
        std::filesystem::copy_file(fixtures / name, hd / name);
    }
    glob2test::ScopedEnvironment assets("GLOB2_ASSET_DIR", scratch.path.string().c_str());
    glob2test::ScopedEnvironment pack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
    glob2test::ToolkitScope toolkit;
    GAGCore::Toolkit::initGraphic(64, 64, GAGCore::GraphicContext::USEGPU, "lossy HD atlas");
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("data/gfx/terrain"));
    REQUIRE(sprite.getFrameCount() == 16);
    REQUIRE(sprite.highResolutionAtlas != nullptr);
    for (int i = 0; i < 16; ++i) {
        REQUIRE(sprite.experimentImages[i] != nullptr);
        CHECK(sprite.getW(i) == 4); CHECK(sprite.getH(i) == 2);
        for (int y = 0; y < lossyAlphaFixture::height; ++y)
            for (int x = 0; x < lossyAlphaFixture::width; ++x) {
                Uint8 r, g, b, a;
                REQUIRE(SDL_ReadSurfacePixel(sprite.highResolutionAtlas->atlas->sdlsurface,
                    (i % 4) * 256 + 64 + x, (i / 4) * 256 + 64 + y, &r, &g, &b, &a));
                CHECK(a == lossyAlphaFixture::alpha[y * lossyAlphaFixture::width + x]);
            }
    }
    const auto previousLayers = sprite.experimentImages;
    const auto *previousAtlas = sprite.highResolutionAtlas.get();
    auto *previous = sprite.experimentImages[0];
    GAGCore::Sprite::requestHighResolution(true);
    CHECK_FALSE(GAGCore::Sprite::pollHighResolution(0));
    CHECK(sprite.experimentImages[0] == previous);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool complete = false;
    while (std::chrono::steady_clock::now() < deadline) {
        GAGCore::Toolkit::assets().poll();
        complete = GAGCore::Sprite::pollHighResolution(1);
        if (complete) break;
        CHECK(sprite.experimentImages == previousLayers);
        CHECK(sprite.highResolutionAtlas.get() == previousAtlas);
        SDL_Delay(1);
    }
    REQUIRE(complete);
    CHECK(sprite.experimentImages[0] != previous);
    CHECK(sprite.highResolutionAtlas != nullptr);
    const auto generation = GAGCore::Toolkit::assets().sourceGeneration();
    auto *published = sprite.experimentImages[0];
    for (int i = 0; i < 4; ++i) GAGCore::Sprite::requestHighResolution(true);
    CHECK(GAGCore::Sprite::pollHighResolution(0));
    CHECK(GAGCore::Toolkit::assets().sourceGeneration() == generation);
    CHECK(sprite.experimentImages[0] == published);
    GAGCore::Sprite::setHighResolution(true);
    CHECK(GAGCore::Toolkit::assets().sourceGeneration() != generation);
    CHECK(sprite.experimentImages[0] != published);
    // Cancellation must release staging inputs without publishing a partial sprite.
    {
        glob2test::ScopedEnvironment disabledPack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
        glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        GAGCore::Sprite::requestHighResolution(false);
    }
    GAGCore::Sprite::requestHighResolution(true);
    // An explicit experimental directory forces HD even with the preference
    // disabled. Remove that override while testing ordinary cancellation.
    {
        glob2test::ScopedEnvironment disabledPack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
        glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        GAGCore::Sprite::requestHighResolution(false);
        const auto cancellationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        bool cancelled = false;
        while (std::chrono::steady_clock::now() < cancellationDeadline) {
            cancelled = GAGCore::Toolkit::pollAssets(2);
            if (cancelled) break;
            SDL_Delay(1);
        }
        REQUIRE(cancelled);
        CHECK(sprite.highResolutionAtlas == nullptr);
        CHECK(sprite.experimentImages[0] == nullptr);
    }
    for (int toggle = 0; toggle < 3; ++toggle) {
        GAGCore::Sprite::setHighResolution(true);
        REQUIRE(sprite.highResolutionAtlas != nullptr);
        REQUIRE(sprite.experimentImages[0] != nullptr);
        auto* surface = sprite.experimentImages[0]->getSDLSurface();
        for (int y = 0; y < lossyAlphaFixture::height; ++y)
            for (int x = 0; x < lossyAlphaFixture::width; ++x) {
                Uint8 r, g, b, a;
                REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
                CHECK(a == lossyAlphaFixture::alpha[y * lossyAlphaFixture::width + x]);
            }
        glob2test::ScopedEnvironment disabledPack("GLOB2_EXPERIMENT_TEXTURE_DIR", hd.string().c_str());
        glob2test::unsetEnv("GLOB2_EXPERIMENT_TEXTURE_DIR");
        GAGCore::Sprite::setHighResolution(false);
    }
    // Missing optional mips preserve the valid standalone HD frames.
    std::filesystem::remove(hd / "terrain-atlas-mip1.webp");
    GAGCore::Sprite::setHighResolution(true);
    CHECK(sprite.highResolutionAtlas == nullptr);
    REQUIRE(sprite.experimentImages[0] != nullptr);
    // An invalid supplied mip preserves the established native-art fallback.
    std::filesystem::copy_file(hd / "frame.webp", hd / "terrain-atlas-mip1.webp");
    GAGCore::Sprite::setHighResolution(true);
    CHECK(sprite.highResolutionAtlas == nullptr);
    CHECK(sprite.experimentImages[0] == nullptr);
    std::filesystem::copy_file(fixtures / "terrain-atlas-mip1.webp", hd / "terrain-atlas-mip1.webp",
        std::filesystem::copy_options::overwrite_existing);
    GAGCore::Sprite::setHighResolution(true);
    REQUIRE(sprite.highResolutionAtlas != nullptr);
    REQUIRE(sprite.experimentImages[0] != nullptr);
}
#endif

TEST_CASE("PNG artwork cannot enter the WebP sprite path") {
    SheetFixture fixture;
    SDL_Surface *surface = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface != nullptr);
    REQUIRE(IMG_SavePNG(surface, (fixture.scratch.path / "legacy0.png").string().c_str()));
    REQUIRE(IMG_SavePNG(surface, (fixture.scratch.path / "disguised0.webp").string().c_str()));
    SDL_DestroySurface(surface);
    GAGCore::Sprite legacy, disguised;
    CHECK_FALSE(legacy.load("legacy"));
    CHECK_FALSE(disguised.load("disguised"));
}

TEST_CASE("Q90 sheet tiles preserve exact frame alpha") {
    SheetFixture fixture;
    using namespace lossyAlphaFixture;
    std::ofstream file(fixture.scratch.path / "lossy.webp", std::ios::binary);
    file.write(reinterpret_cast<const char*>(webp), sizeof(webp));
    file.close();
    fixture.index("lossy", "lossy.webp image 0 2 8 8\nlossy.webp rotated 0 2 8 8\n");
    GAGCore::Sprite sprite;
    REQUIRE(sprite.load("lossy"));
    REQUIRE(sprite.getFrameCount() == 2);
    for (int frame = 0; frame < 2; ++frame) {
        REQUIRE(sprite.images[frame] != nullptr);
        REQUIRE(sprite.rotated[frame] != nullptr);
        for (auto surface : {sprite.images[frame], sprite.rotated[frame]->orig}) {
            CHECK(surface->getW() == tile); CHECK(surface->getH() == tile);
            for (int y = 0; y < tile; ++y)
                for (int x = 0; x < tile; ++x) {
                    Uint8 r, g, b, a;
                    REQUIRE(SDL_ReadSurfacePixel(surface->sdlsurface, x, y, &r, &g, &b, &a));
                    CHECK(a == alpha[y * width + frame * tile + x]);
                }
        }
    }
}

TEST_CASE("sprites without an index still load one file per frame") {
	SheetFixture fixture;
	writePattern(fixture.scratch.path / "single0.webp", 0, 1, 1, 4);
	writePattern(fixture.scratch.path / "single1r.webp", 1, 1, 1, 4);
	writePattern(fixture.scratch.path / "single3.webp", 3, 1, 1, 4);
    const auto shadow = fixture.scratch.path / "later-root";
    std::filesystem::create_directories(shadow);
    writePattern(shadow / "single0.webp", 3, 1, 1, 4);
    writePattern(shadow / "single1r.webp", 3, 1, 1, 4);
    writePattern(shadow / "single10.webp", 3, 1, 1, 4);
    writePattern(shadow / "Single2.webp", 3, 1, 1, 4); // case-sensitive membership must not fill frame2
    GAGCore::Toolkit::getFileManager()->addDir(shadow.string());
	GAGCore::Sprite sprite;
	REQUIRE(sprite.load("single"));
	// Frame 2 is missing, so frame 3 is never reached.
	REQUIRE(sprite.getFrameCount() == 2);
	CHECK(samePixels(sprite.images[0], 0, 4));
	CHECK(sprite.rotated[0] == nullptr);
	CHECK(sprite.images[1] == nullptr);
	CHECK(samePixels(sprite.rotated[1]->orig, 1, 4));
}

TEST_CASE("an index the sheets cannot satisfy falls back to the frames") {
	SheetFixture fixture;
	writePattern(fixture.scratch.path / "broken-sheet-0.webp", 0, 4, 2, 3);
	writePattern(fixture.scratch.path / "broken0.webp", 0, 1, 1, 4);
	const std::pair<const char *, const char *> indexes[] = {
		{"too few tiles", "broken-sheet-0.webp rotated 0 64 3 3\n"},
		{"width not a multiple of the tile", "broken-sheet-0.webp rotated 0 4 4 3\n"},
		{"missing sheet", "absent.webp rotated 0 1 3 3\n"},
		{"frame in two sheets", "broken-sheet-0.webp rotated 0 4 3 3\nbroken-sheet-0.webp rotated 3 1 3 3\n"},
		{"path in the sheet name", "../broken-sheet-0.webp rotated 0 4 3 3\n"},
		{"malformed line", "broken-sheet-0.webp rotated zero 4 3 3\n"},
	};
	for (const auto &[problem, text] : indexes)
	{
		INFO(problem);
		fixture.index("broken", text);
		GAGCore::Sprite sprite;
		REQUIRE(sprite.load("broken"));
		REQUIRE(sprite.getFrameCount() == 1);
		CHECK(samePixels(sprite.images[0], 0, 4));
		CHECK(sprite.rotated[0] == nullptr);
	}
}
}
