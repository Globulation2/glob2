// SPDX-License-Identifier: GPL-3.0-or-later
// Inspect draft translations through the production font and menu renderer.
// Draft overlays and evidence live outside the shipped language inventory.
#include "GlobalContainer.h"
#include "Engine.h"
#include "tutorial/TutorialModel.h"
#include "tutorial/TutorialGuidance.h"
#include "sim/presentation/SceneInputs.h"
#include "MainMenuScreen.h"
#include "SettingsScreen.h"
#include "CustomGameScreen.h"
#include "GameGUIDialog.h"
#include "FrontendTheme.h"
#include "MenuColony.h"
#include "Sha256.h"
#include <FileManager.h>
#include <GraphicContext.h>
#include <ScreenStack.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <TrueTypeFont.h>
#include <ui/TextLayout.h>
#include <InterfacePresentation.h>
#include <MapZoomControls.h>
#include <glob2/TestBuildProvenance.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_net/SDL_net.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#if !defined(WIN32)
#include <dlfcn.h>
#endif

GlobalContainer *globalContainer = nullptr;
// Access only the UI owned by the real engine; never substitute its simulation
// or rendering loop for translation captures.
struct TranslationPreviewAccess
{
    static GameGUI &gui(Engine &engine) { return engine.gui; }
    static bool currentWorldAdmitted(const Engine &engine) {
        return engine.retainedPresentation && engine.retainedPresentation->world.tick == engine.gui.game.stepCounter;
    }
    static void openMenu(GameGUI &gui) { gui.openMainMenu(); }
    static void closeMenu(GameGUI &gui) { gui.closeDialog(); }
    static bool initialBriefingOpen(const GameGUI &gui) { return gui.inGameMenu == GameGUI::IGM_OBJECTIVES; }
    static void openBriefing(GameGUI &gui) {
        gui.openDialog(GameGUI::IGM_OBJECTIVES, std::make_unique<InGameObjectivesScreen>(&gui, true));
    }
    static bool briefingTabShown(const GameGUI &gui) {
        const auto *dialog = dynamic_cast<const InGameObjectivesScreen *>(gui.gameMenuScreen.get());
        return dialog && dialog->tab() == InGameObjectivesScreen::BRIEFING;
    }
    static bool dialogOpen(const GameGUI &gui) { return gui.inGameMenu != GameGUI::IGM_NONE || gui.activeDialog() != nullptr; }
    static Tutorial::Guidance *guidance(GameGUI &gui) { return gui.tutorialGuidance.get(); }
    static void rebaseViewport(GameGUI &gui) {
        gui.torusView.rebaseViewport(gui.viewportX, gui.viewportY);
    }
    static void syncZoomViewport(GameGUI &gui) {
        gui.viewportX = gui.camera.tileX();
        gui.viewportY = gui.camera.tileY();
        rebaseViewport(gui);
    }
};
namespace
{
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
fs::path executablePath;
unsigned installDraftOverlay(const fs::path &, const fs::path &, const fs::path &);
void require(bool ok, const std::string &message)
{
    if (!ok) throw std::runtime_error(message + ": " + SDL_GetError());
}
Json readJson(const fs::path &path)
{
    std::ifstream stream(path);
    require(bool(stream), "Cannot open " + path.string());
    return Json::parse(stream);
}
void writeJson(const fs::path &path, const Json &value)
{
    std::ofstream stream(path);
    stream << value.dump(2) << '\n';
    require(bool(stream), "Cannot write " + path.string());
}
std::string sha256(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "Cannot hash " + path.string());
    Online::Sha256 hash;
    char buffer[65536];
    while (stream.read(buffer, sizeof buffer) || stream.gcount())
        hash.update(buffer, size_t(stream.gcount()));
    require(stream.eof(), "Cannot finish hashing " + path.string());
    return Online::Sha256::toHex(hash.finish());
}
std::string pilotLocale(const fs::path &pilot)
{
    for (const auto *name : {"metadata.json", "authorship.json"}) {
        if (!fs::exists(pilot / name)) continue;
        const auto metadata = readJson(pilot / name);
        if (metadata.contains("locale") && metadata["locale"].is_string())
            return metadata["locale"].get<std::string>();
    }
    throw std::runtime_error("Pilot needs metadata.json/authorship.json with actual locale identity");
}
Json evidenceIdentity(const fs::path &root, const fs::path &pilot)
{
    Json result = {{"locale", pilotLocale(pilot)}, {"source_revision", nullptr},
        {"source_revision_origin", "Frozen translation kit; executable and code hashes identify the actual dirty build."},
        {"executable", executablePath.string()}, {"executable_sha256", sha256(executablePath)},
        {"producer", Json::parse(GLOB2_TEST_PROVENANCE_JSON)},
        {"source_sha256", Json::object()}, {"catalog_sha256", Json::object()},
        {"runtime_fonts", Json::array()}, {"runtime_search_paths", Json::array()}};
    const auto kitRoot = pilot.parent_path().parent_path();
    const auto manifest = kitRoot / "manifest.json";
    const auto legacyKit = kitRoot / "translation-kit.json";
    if (fs::exists(manifest)) result["source_revision"] = readJson(manifest).value("revision", "");
    else if (fs::exists(legacyKit)) result["source_revision"] = readJson(legacyKit).value("source_revision", "");
    if (fs::exists(pilot / "metadata.json"))
        result["input_metadata"] = readJson(pilot / "metadata.json");
    for (const auto *name : {"data/texts.en.txt", "data/texts.keys.txt",
             "platform/packages/i18n/locales/en.json", "libgag/src/TrueTypeFont.cpp",
             "libgag/src/BidiText.cpp", "libgag/src/ui/TextLayout.cpp",
             "src/hud/draw/GameGUIDraw.cpp", "src/hud/touch/GameGUITouchView.cpp",
             "src/hud/touch/GameGUITouch.cpp", "src/hud/touch/GameGUITouch.h",
             "libgag/include/TouchText.h", "libgag/include/TrueTypeFont.h",
             "libgag/include/ui/TextLayout.h", "libgag/src/DrawableSurfaceCompound.cpp",
             "src/game/screens/CustomGameScreen.cpp", "src/render/overlay/MapZoomControls.h",
             "tools/TranslationPreviewHarness.cpp"})
        result["source_sha256"][name] = sha256(root / name);
    for (const auto *surface : {"native", "online"})
        result["catalog_sha256"][surface] = sha256(pilot / (std::string(surface) + ".json"));
    auto *files = GAGCore::Toolkit::getFileManager();
    for (unsigned i = 0; i < files->getDirCount(); ++i)
        result["runtime_search_paths"].push_back(files->getDir(i));
    std::vector<std::string> fontNames{"data/fonts/sans.ttf"};
    for (const auto &font : fs::directory_iterator(root / "data/fonts/scripts"))
        if (font.path().extension() == ".ttf") fontNames.push_back("data/fonts/scripts/" + font.path().filename().string());
    std::sort(fontNames.begin(), fontNames.end());
    for (const auto &font : fontNames) {
        bool found = false;
        for (unsigned i = 0; i < files->getDirCount(); ++i) {
            const auto candidate = fs::absolute(fs::path(files->getDir(i)) / font);
            if (!fs::is_regular_file(candidate)) continue;
            result["runtime_fonts"].push_back({{"virtual_path", font}, {"path", candidate.string()}, {"sha256", sha256(candidate)}});
            found = true; break;
        }
        if (!found) result["runtime_fonts"].push_back({{"virtual_path", font}, {"missing", true}});
    }
#if !defined(WIN32)
    Dl_info core{}, ttf{};
    if (dladdr(reinterpret_cast<const void *>(&SDL_Init), &core) && core.dli_fname)
        result["sdl_library_path"] = core.dli_fname;
    if (dladdr(reinterpret_cast<const void *>(&TTF_Init), &ttf) && ttf.dli_fname)
        result["sdl_ttf_library_path"] = ttf.dli_fname;
#else
    result["dynamic_library_paths"] = "Not available from this harness on Windows; record externally.";
#endif
    result["sdl_version"] = SDL_GetVersion();
    result["sdl_ttf_version"] = TTF_Version();
    int hb = 0, hbMinor = 0, hbMicro = 0;
    TTF_GetHarfBuzzVersion(&hb, &hbMinor, &hbMicro);
    result["harfbuzz_version"] = {hb, hbMinor, hbMicro};
    return result;
}
std::string numbered(unsigned number)
{
    std::ostringstream stream;
    stream << std::setw(3) << std::setfill('0') << number;
    return stream.str();
}
struct Sample
{
    std::string surface, key, branch, text;
};
std::vector<Sample> samples(const fs::path &pilot)
{
    std::vector<Sample> result;
    for (const auto *surface : {"native", "online"}) {
        const Json catalog = readJson(pilot / (std::string(surface) + ".json"));
        require(catalog.is_object(), "Pilot catalog must be an object");
        for (const auto &[key, value] : catalog.items()) {
            if (value.is_string()) result.push_back({surface, key, "", value.get<std::string>()});
            else {
                require(surface == std::string("online") && value.is_object(), "Invalid pilot value");
                for (const auto &[branch, text] : value.items()) {
                    require(text.is_string(), "Plural branch must be text");
                    result.push_back({surface, key, branch, text.get<std::string>()});
                }
            }
        }
    }
    return result;
}
struct ProductionMeasurer : GAGGUI::ui::TextMeasurer
{
    GAGCore::TrueTypeFont &font;
    explicit ProductionMeasurer(GAGCore::TrueTypeFont &font) : font(font) {}
    int width(GAGGUI::ui::FontRole, const std::string &text) const override { return font.getStringWidth(text); }
    int lineHeight(GAGGUI::ui::FontRole) const override { return font.getStringHeight(""); }
    std::vector<std::size_t> clusterEnds(GAGGUI::ui::FontRole, const std::string &text) const override
    { return font.getStringClusterEnds(text); }
};
void fontMatrix(const fs::path &root, const fs::path &pilot, const fs::path &output, int width, int height)
{
    GAGCore::Toolkit::init("glob2-translation-font-preview");
    GAGCore::Toolkit::getFileManager()->addDir(root.string());
    require(TTF_Init(), "Initialize SDL_ttf");
    const auto localized = installDraftOverlay(root, pilot, output);
    // Fonts and surfaces are destroyed before TTF/Toolkit teardown.
    Json manifest = {{"kind", "production-font-matrix"}, {"pilot", pilot.string()},
        {"logical_width", width}, {"logical_height", height}, {"samples", Json::array()},
        {"limits", {"Draft overlay only; no language registered or locale finalized.",
                    "Uses production TrueTypeFont and shared UI wrapText, not a game screen.",
                    "Raw raster fits_width/height remain diagnostic; drawn_fits_width/height use the production draw destination, preserving the whole bitmap.",
                    "Glyph checks and images require independent linguistic/visual inspection.",
                    "Placeholder tokens are shown literally; no claim of formatted-message coverage."}}};
    manifest["evidence_identity"] = evidenceIdentity(root, pilot);
    manifest["native_keys_overridden"] = localized;
    int hb = 0, hbMinor = 0, hbMicro = 0;
    TTF_GetHarfBuzzVersion(&hb, &hbMinor, &hbMicro);
    manifest["harfbuzz"] = {hb, hbMinor, hbMicro};
    {
        const auto rows = samples(pilot);
        for (unsigned size : {10u, 13u, 20u}) for (int scale : {1, 2}) {
            GAGCore::TrueTypeFont font;
            require(font.load("data/fonts/sans.ttf", size), "Load production font");
            font.setStyle(GAGCore::Font::Style(GAGCore::Font::STYLE_NORMAL, 245, 238, 214));
            ProductionMeasurer measurer(font);
            Surface page(nullptr, SDL_DestroySurface);
            int y = 0;
            unsigned pageNumber = 0;
            std::string filename;
            auto save = [&] {
                if (page) require(IMG_SavePNG(page.get(), (output / filename).string().c_str()), "Save font matrix");
            };
            auto freshPage = [&] {
                save();
                filename = "font-" + std::to_string(size) + "-scale-" + std::to_string(scale) +
                    "-page-" + numbered(++pageNumber) + ".png";
                page.reset(SDL_CreateSurface(width * scale, height * scale, SDL_PIXELFORMAT_RGBA32));
                require(bool(page), "Allocate matrix page");
                require(SDL_FillSurfaceRect(page.get(), nullptr, SDL_MapSurfaceRGBA(page.get(), 28, 24, 35, 255)), "Fill page");
                y = 12;
            };
            freshPage();
            for (unsigned index = 0; index < rows.size(); ++index) {
                const auto &row = rows[index];
                const auto wrapped = GAGGUI::ui::wrapText(measurer, GAGGUI::ui::FontRole::Body, row.text, width - 24);
                Json sample = {{"surface", row.surface}, {"key", row.key}, {"branch", row.branch},
                    {"size", size}, {"scale", scale}, {"has_glyphs", font.hasGlyphsFor(row.text)},
                    {"unwrapped_width", font.getStringWidth(row.text)}, {"lines", Json::array()}};
                for (const auto &line : wrapped) {
                    // Shared production text wrapping is itself under review; expose each
                    // actual line and raster extent so split clusters/clipping are visible.
                    auto bitmap = font.rasterizeString(line.empty() ? " " : line, float(scale));
                    require(bool(bitmap), "Rasterize pilot text");
                    const auto drawn = font.getStringDrawSize(line.empty() ? " " : line, float(scale));
                    const int drawnWidth = std::lround(drawn.first * scale);
                    const int drawnHeight = std::lround(drawn.second * scale);
                    const int logicalHeight = (drawnHeight + scale - 1) / scale;
                    if (y + logicalHeight + 12 > height) freshPage();
                    SDL_Rect destination{12 * scale, y * scale, drawnWidth, drawnHeight};
                    require(SDL_BlitSurfaceScaled(bitmap->getSDLSurface(), nullptr, page.get(), &destination,
                        SDL_SCALEMODE_LINEAR), "Compose raster at production destination size");
                    sample["lines"].push_back({{"text", line}, {"png", filename}, {"x", 12}, {"y", y},
                        {"raster_width", bitmap->getW()}, {"raster_height", bitmap->getH()},
                        {"fits_width", bitmap->getW() <= (width - 24) * scale},
                        {"fits_height", y + (bitmap->getH() + scale - 1) / scale <= height},
                        {"destination_width", drawn.first}, {"destination_height", drawn.second},
                        {"drawn_width", drawnWidth}, {"drawn_height", drawnHeight},
                        {"drawn_fits_width", drawnWidth <= (width - 24) * scale},
                        {"drawn_fits_height", y + logicalHeight <= height}});
                    y += logicalHeight + 3;
                }
                y += 8;
                manifest["samples"].push_back(std::move(sample));
            }
            save();
        }
    }
    writeJson(output / "font-matrix.json", manifest);
    GAGCore::Toolkit::close();
    TTF_Quit();
}

template<class T> class Preview : public T
{
public:
    using T::T;
    ~Preview() {
        if (this->isExecutionRunning()) { this->endExecute(0); this->finishExecution(); }
    }
    void render() {
        this->beginExecution(globalContainer->gfx);
        this->paintFrame(SDL_GetTicks());
        this->paintFrame(SDL_GetTicks());
    }
    Json bounds() {
        Json result = Json::array();
        for (auto *node : this->GAGGUI::ui::UIScreen::host().interactiveNodes()) {
            const auto r = node->bounds;
            result.push_back({{"key", node->key}, {"x", r.x}, {"y", r.y}, {"width", r.w}, {"height", r.h},
                {"within_viewport", r.x >= 0 && r.y >= 0 && r.x + r.w <= globalContainer->gfx->getW() &&
                    r.y + r.h <= globalContainer->gfx->getH()}});
        }
        return result;
    }
};
unsigned installDraftOverlay(const fs::path &root, const fs::path &pilot, const fs::path &output)
{
    const Json translations = readJson(pilot / "native.json");
    const auto locale = pilotLocale(pilot);
    require(locale != "en", "Draft overlay must identify a translated locale");
    const fs::path table = output / "draft-overlay.txt";
    std::ifstream english(root / "data/texts.en.txt");
    std::ofstream overlay(table);
    std::string key, value;
    unsigned applied = 0;
    while (std::getline(english, key)) {
        require(bool(std::getline(english, value)), "English key without value");
        if (translations.contains(key)) { value = translations[key].get<std::string>(); ++applied; }
        if (key == "[language-code]") value = locale;
        overlay << key << '\n' << value << '\n';
    }
    require(bool(overlay), "Write draft overlay");
    overlay.close();
    require(applied == translations.size(), "Pilot contains unknown native key");
    const fs::path listing = output / "draft-list.txt";
    std::ofstream list(listing);
    // Legacy SGSL parses language blocks against the installed catalog codes.
    // Retain the existing inventory while adding this unregistered overlay.
    std::ifstream installed(root / "data/texts.list.txt");
    std::string entry;
    while (std::getline(installed, entry)) {
        if (entry.empty() || fs::path(entry).filename() == "texts." + locale + ".txt") continue;
        list << (root / entry).string() << '\n';
    }
    require(installed.eof(), "Read installed language inventory");
    list << table.string() << '\n';
    require(bool(list), "Write draft language inventory");
    list.close();
    auto *strings = GAGCore::Toolkit::getStringTable();
    require(strings->load(listing.string()), "Load draft-only language overlay");
    require(strings->hasLanguage("en"), "English fallback catalog");
    strings->setDefaultLang(strings->getLangCode("en"));
    require(strings->hasLanguage(locale), "Draft locale code");
    strings->setLang(strings->getLangCode(locale));
    return applied;
}
void screenPreview(const fs::path &root, const fs::path &pilot, const fs::path &output,
                   const std::string &screen, int width, int height)
{
    GlobalContainer globals("glob2-translation-screen-preview");
    globalContainer = &globals;
    globals.fileManager->addDir(root.string());
    globals.settings.mute = 1;
    globals.settings.screenWidth = width;
    globals.settings.screenHeight = height;
    globals.settings.screenFlags = 0;
    globals.load();
    const auto localized = installDraftOverlay(root, pilot, output);
    globals.settings.language = pilotLocale(pilot);
    Json controls;
    {
        FrontendTheme theme;
        require(theme.colony->load(), "Load bundled menu colony");
        FrontendScope frontend;
        GAGGUI::ScreenStack stack(*globals.gfx);
        if (screen == "main") {
            Preview<MainMenuScreen> preview; preview.render(); controls = preview.bounds();
        } else if (screen == "settings" || screen == "settings-audio") {
            Preview<SettingsScreen> preview;
            if (screen == "settings-audio") preview.selectCategory(SettingsScreen::Category::Audio);
            preview.render(); controls = preview.bounds();
        } else if (screen == "lobby" || screen == "lobby-players") {
            Preview<CustomGameScreen> preview(stack);
            if (screen == "lobby-players") preview.selectTab(1);
            preview.render(); controls = preview.bounds();
        } else throw std::runtime_error("Unknown screen: " + screen);
        GAGCore::DrawableSurface image(globals.gfx->getW(), globals.gfx->getH());
        image.drawSurface(0, 0, globals.gfx);
        require(IMG_SavePNG(image.getSDLSurface(), (output / (screen + ".png")).string().c_str()), "Save production screen");
    }
    writeJson(output / (screen + ".json"), {{"kind", "production-screen-draft-overlay"}, {"screen", screen},
        {"pilot", pilot.string()}, {"evidence_identity", evidenceIdentity(root, pilot)}, {"native_keys_overridden", localized}, {"width", width}, {"height", height},
        {"controls", controls}, {"limits", {"Only supplied draft keys are translated; native_keys_overridden records their count. Missing draft keys use English.",
            "Control bounds include scrollable content and do not independently prove text fitting.",
            "Does not verify gameplay HUD, tutorial, dialogs, browser, mobile platform, or full locale."}}});
    globalContainer = nullptr;
}

void gameplayPreview(const fs::path &root, const fs::path &pilot, const fs::path &output,
                     const std::string &kind, int width, int height)
{
    const bool tutorial = kind == "tutorial" || kind.starts_with("tutorial:");
    require(kind == "gameplay" || tutorial, "Unknown game preview kind");
    const std::string lesson = kind == "tutorial" ? "introduction" : (tutorial ? kind.substr(9) : "");
    if (tutorial) {
        const auto catalog = readJson(root / "scripts/tutorials/catalog.json");
        bool found = false;
        for (const auto &entry : catalog)
            if (readJson(root / entry["definition"].get<std::string>())["id"] == lesson) found = true;
        require(found, "Known production tutorial lesson");
    }
    const std::string captureKind = tutorial ? "tutorial-" + lesson : kind;
    require(SDL_setenv_unsafe("GLOB2_SIM_THREAD", "0", 1) == 0, "Use serial scripted preview execution");
    GlobalContainer globals("glob2-translation-game-preview");
    globalContainer = &globals;
    globals.fileManager->addDir(root.string());
    globals.settings.mute = 1;
    globals.settings.screenWidth = width;
    globals.settings.screenHeight = height;
    globals.settings.screenFlags = 0;
    globals.load();
    globals.settings.autosaveGames = false;
    globals.settings.gameSpeed = 0;
    const auto localized = installDraftOverlay(root, pilot, output);
    globals.settings.language = pilotLocale(pilot);
    require(NET_Init(), "Initialize SDL_net");
    struct NetworkScope { ~NetworkScope() { NET_Quit(); } } network;
    const std::string map = tutorial ? "campaigns/" + lesson + ".map" : "maps/balanced.map";
    Json result = {{"kind", "production-game-draft-overlay"}, {"locale", pilotLocale(pilot)},
        {"simulation_execution", "serial with scripted host clock"},
        {"map", map}, {"map_sha256", sha256(root / (map + ".gz"))},
        {"evidence_identity", evidenceIdentity(root, pilot)}, {"native_keys_overridden", localized},
        {"width", width}, {"height", height}, {"simulation_steps", Json::array()}, {"captures", Json::array()},
        {"phone_presentation_requested", GAGCore::phonePresentationRequested()},
        {"limits", {"Draft overlay in the actual desktop engine; not a native mobile-platform claim.",
            "Tutorial instructions come from the current production JavaScript lesson and catalog; no script behavior is changed.",
            "No external service or networked match is exercised by this local map session."}}};
    {
        Engine engine;
        auto &gui = TranslationPreviewAccess::gui(engine);
        struct AbortScope {
            Engine &engine;
            bool active = true;
            ~AbortScope() { if (active) engine.abortSession(); }
        } abort{engine};
        require(engine.initCampaign(map) == Engine::EE_NO_ERROR, "Load preview map");
        engine.prepareRun();
        Uint64 now = 1000;
        engine.beginSession(now);
        for (int step = 0; step < 20; ++step) {
            require(engine.stepSession(now, {}), "Preview session running");
            result["simulation_steps"].push_back({{"tick", gui.game.stepCounter},
                {"checksum", gui.game.checkSum()}});
            if (globals.gfx->beginRenderFrame()) engine.drawSession(true);
            now += 40;
            SDL_Delay(1);
        }
        auto draw = [&] {
            if (!globals.gfx->beginRenderFrame()) return false;
            engine.drawSession(true);
            return true;
        };
        const Uint64 readyDeadline = SDL_GetTicks() + 15000;
        while (SDL_GetTicks() < readyDeadline) {
            draw();
            const auto &scene = gui.drawnScene();
            if (scene.world.session && scene.tick == gui.game.stepCounter &&
                (!tutorial || bool(scene.panels.hud.state().tutorial))) break;
            // A final fast simulation tick may overlap a pending scene job.
            // Follow ordinary owner admission until that current world has a
            // presentation request, then keep simulation frozen for capture.
            if (!TranslationPreviewAccess::currentWorldAdmitted(engine)) {
                require(engine.stepSession(now, {}), "Admit preview world");
                now += 40;
            }
            SDL_Delay(1);
        }
        const auto &scene = gui.drawnScene();
        std::cerr << "Preview readiness: simulation=" << gui.game.stepCounter
                  << " scene=" << scene.tick << " world=" << scene.world.tick
                  << " has_session=" << bool(scene.world.session)
                  << " tutorial_shown=" << scene.panels.hud.state().legacyScriptTextShown << '\n';
        require(scene.world.session && scene.tick == gui.game.stepCounter, "Latest gameplay scene published");
        if (tutorial)
            require(bool(scene.panels.hud.state().tutorial), "Actual tutorial instruction published");
        result["published_tick"] = scene.tick;
        result["capture_simulation_tick"] = gui.game.stepCounter;
        result["tutorial_instruction_shown"] = bool(scene.panels.hud.state().tutorial);
        if (const auto &model = scene.panels.hud.state().tutorial) {
            result["tutorial_lesson"] = model->lessonId;
            result["tutorial_instruction"] = model->instruction;
            result["tutorial_touch_instruction"] = model->touchInstruction;
        }
        result["source_script_text"] = scene.panels.hud.state().legacyScriptText;
        const fs::path profile = globals.fileManager->getDir(0);
        auto capture = [&](const std::string &name) {
            const auto before = gui.game.checkSum();
            const auto bitmap = profile / ("translation-preview-" + name + ".bmp");
            fs::remove(bitmap);
            // Software captures may save synchronously; render the changed UI
            // before requesting a screenshot, not only while awaiting its file.
            const Uint64 drawDeadline = SDL_GetTicks() + 15000;
            bool rendered = draw();
            while (!rendered && SDL_GetTicks() < drawDeadline) { SDL_Delay(1); rendered = draw(); }
            require(rendered, "Render the current UI state before capture");
            globals.gfx->printScreen(bitmap.filename().string());
            const Uint64 deadline = SDL_GetTicks() + 15000;
            while (!fs::exists(bitmap) && SDL_GetTicks() < deadline) { draw(); SDL_Delay(1); }
            require(fs::exists(bitmap), "Production game screenshot completed");
            Surface image(SDL_LoadBMP(bitmap.string().c_str()), SDL_DestroySurface);
            require(bool(image), "Read production screenshot");
            require(IMG_SavePNG(image.get(), (output / (name + ".png")).string().c_str()), "Save game PNG");
            const auto after = gui.game.checkSum();
            require(before == after, "Drawing must preserve simulation checksum");
            result["captures"].push_back({{"name", name}, {"checksum_before_draw", before},
                {"checksum_after_draw", after}, {"image_width", image->w}, {"image_height", image->h}});
        };

        if (tutorial) {
            // A tick-12 automatic briefing can cover the current lesson model.
            // Preserve that real opening state. Other lessons still need their
            // actual briefing tab captured before exposing the guidance.
            const bool briefing = TranslationPreviewAccess::initialBriefingOpen(gui);
            const auto before = gui.game.checkSum();
            if (!briefing) TranslationPreviewAccess::openBriefing(gui);
            require(TranslationPreviewAccess::briefingTabShown(gui), "Actual tutorial briefing tab available");
            require(before == gui.game.checkSum(), "Opening briefing must preserve simulation state");
            capture(captureKind + "-briefing");
            TranslationPreviewAccess::closeMenu(gui);
            require(!TranslationPreviewAccess::dialogOpen(gui), "Tutorial guidance capture must be dialog-free");
            require(before == gui.game.checkSum(), "Dismissing briefing must preserve simulation state");
            result["automatic_briefing_dismissed"] = briefing;
            result["briefing_explicitly_opened"] = !briefing;
            result["tutorial_capture_dialog_free"] = true;
        }
        capture(captureKind);
        if (tutorial) {
            auto *card = TranslationPreviewAccess::guidance(gui);
            require(card != nullptr, "Actual tutorial guidance card available");
            const auto before = gui.game.checkSum();
            const auto expandedHeight = card->panelBounds().h;
            auto toggleGuidance = [&] {
                card->host().layoutIfNeeded();
                const auto bounds = card->host().bounds("intro/collapse");
                require(bounds.w > 0 && bounds.h > 0, "Actual guidance collapse control available");
                SDL_Event event{};
                if (GAGCore::phonePresentationRequested()) {
                    event.type = SDL_EVENT_FINGER_DOWN;
                    event.tfinger.x = float(bounds.x + bounds.w / 2) / globals.gfx->getW();
                    event.tfinger.y = float(bounds.y + bounds.h / 2) / globals.gfx->getH();
                    require(card->handle(event), "Guidance collapse finger down handled");
                    event.type = SDL_EVENT_FINGER_UP;
                } else {
                    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    event.button.button = SDL_BUTTON_LEFT;
                    event.button.x = bounds.x + bounds.w / 2;
                    event.button.y = bounds.y + bounds.h / 2;
                    require(card->handle(event), "Guidance collapse mouse down handled");
                    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
                }
                require(card->handle(event), "Guidance collapse release handled");
                card->update(SDL_GetTicks());
                require(before == gui.game.checkSum(), "Guidance toggle preserves simulation checksum");
            };
            toggleGuidance();
            const auto collapsedHeight = card->panelBounds().h;
            require(collapsedHeight < expandedHeight, "Collapsed guidance reduces card to its heading");
            capture(captureKind + "-collapsed");
            toggleGuidance();
            require(card->panelBounds().h == expandedHeight, "Expanded guidance restores full card height");
            capture(captureKind + "-expanded");
            result["guidance_toggle_probe"] = {{"control", "intro/collapse"},
                {"input", GAGCore::phonePresentationRequested() ? "finger" : "mouse"},
                {"expanded_height", expandedHeight}, {"collapsed_height", collapsedHeight},
                {"checksum_before_ui", before}, {"checksum_after_ui", gui.game.checkSum()},
                {"limits", "Uses actual guidance hit bounds and public event handling; does not represent physical input hardware."}};
        }
        if (kind == "gameplay" && !GAGCore::phonePresentationRequested()) {
            const auto originalCamera = gui.camera;
            const int originalViewportX = gui.viewportX, originalViewportY = gui.viewportY;
            const auto before = gui.game.checkSum();
            const auto box = mapZoomControlsLayout(gui.camera, true, true);
            require(box.height > 0, "Desktop zoom controls visible");
            const int plusX = box.x + box.plus() + 12, hitY = box.y + box.height / 2;
            const double zoom = gui.camera.zoom;
            auto clickZoom = [&](int x, int y) {
                const bool handled = clickMapZoomControls(gui.camera, x, y, true, true);
                if (handled) TranslationPreviewAccess::syncZoomViewport(gui);
                return handled;
            };
            require(clickZoom(plusX, hitY), "Zoom plus click accepted");
            require(std::abs(gui.camera.zoom - std::min(MapCamera::MAX_ZOOM, zoom * 1.1)) < 1e-8,
                "Zoom plus retains its action");
            const auto afterPlus = mapZoomControlsLayout(gui.camera, true, true);
            require(box.plus() == afterPlus.plus() && box.middleWidth == afterPlus.middleWidth &&
                box.x == afterPlus.x && box.y == afterPlus.y && box.width == afterPlus.width,
                "Zoom button hit geometry stays stable after zoom changes");
            capture(captureKind + "-zoom-plus");
            require(clickZoom(plusX, hitY), "Repeated plus uses same screen point");
            require(std::abs(gui.camera.zoom - std::min(MapCamera::MAX_ZOOM, zoom * 1.21)) < 1e-8,
                "Repeated plus keeps targeting plus");
            const int middleX = box.x + 24 + box.middleWidth / 2;
            require(clickZoom(middleX, hitY), "100 percent stop click");
            require(gui.camera.zoom == 1.0, "Middle control restores100 percent");
            require(clickZoom(middleX, hitY), "Fit stop click");
            require(std::abs(gui.camera.zoom - gui.camera.minimumZoom()) < 1e-8, "Middle control fits whole map");
            capture(captureKind + "-zoom-fit");
            result["zoom_control_probe"] = {{"x", box.x}, {"y", box.y}, {"width", box.width},
                {"height", box.height}, {"middle_width", box.middleWidth}, {"plus_offset", box.plus()},
                {"repeated_plus_point", {plusX, hitY}}, {"fit_zoom", gui.camera.zoom},
                {"checksum_before_ui", before}, {"checksum_after_ui", gui.game.checkSum()},
                {"limits", "Exercises the actual shared click helper and real draws, not physical pointer hardware."}};
            require(before == gui.game.checkSum(), "Zoom UI must preserve simulation state");
            gui.camera = originalCamera;
            gui.viewportX = originalViewportX;
            gui.viewportY = originalViewportY;
            TranslationPreviewAccess::rebaseViewport(gui);
        }
        TranslationPreviewAccess::openMenu(gui);
        capture(captureKind + "-dialog");
        TranslationPreviewAccess::closeMenu(gui);
        gui.isRunning = false;
        engine.finishSession();
        abort.active = false;
        engine.restoreCursor();
    }
    writeJson(output / (captureKind + ".json"), result);
    globalContainer = nullptr;
}
} // namespace
int main(int argc, char **argv)
{
    try {
        require(argc >= 7, "usage: TranslationPreviewHarness matrix ROOT DRAFT OUTPUT W H | screen ROOT DRAFT OUTPUT W H SCREEN | game ROOT DRAFT OUTPUT W H gameplay|tutorial[:LESSON-ID]");
        executablePath = fs::absolute(argv[0]);
        const std::string mode = argv[1];
        const auto root = fs::absolute(argv[2]), pilot = fs::absolute(argv[3]), output = fs::absolute(argv[4]);
        const int width = std::stoi(argv[5]), height = std::stoi(argv[6]);
        require(width >= 240 && width <= 4096 && height >= 240 && height <= 4096, "Viewport out of supported range");
        fs::create_directories(output);
        if (mode == "matrix") fontMatrix(root, pilot, output, width, height);
        else if (mode == "screen") {
            require(argc == 8, "Screen mode needs screen name");
            screenPreview(root, pilot, output, argv[7], width, height);
        } else if (mode == "game") {
            require(argc == 8, "Game mode needs gameplay or tutorial");
            gameplayPreview(root, pilot, output, argv[7], width, height);
        } else throw std::runtime_error("Unknown mode");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "TranslationPreviewHarness: " << error.what() << '\n';
        return 2;
    }
}
