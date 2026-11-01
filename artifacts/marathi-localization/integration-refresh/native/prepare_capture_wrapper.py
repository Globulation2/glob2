from pathlib import Path
import hashlib,json
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-refresh/native';original=root/'tools/TranslationPreviewHarness.cpp';source=original.read_text();source=source.replace('#include "SettingsScreen.h"','#include "SettingsScreen.h"\n#include "EndGameScreen.h"\n#include "Team.h"')
source=source.replace('    Json bounds() {','    void redraw() { this->paintFrame(SDL_GetTicks()); this->paintFrame(SDL_GetTicks()); }\n    Json bounds() {')
needle='    result["harfbuzz_version"] = {hb, hbMinor, hbMicro};\n    return result;'
replacement='''    result["harfbuzz_version"] = {hb, hbMinor, hbMicro};
    result["capture_adapter_source"] = "artifacts/marathi-localization/integration-refresh/native/IntegrationPreviewHarness.cpp";
    result["capture_adapter_sha256"] = sha256(root / result["capture_adapter_source"].get<std::string>());
    result["capture_adapter_scope"] = "Capture-only wrapper; actual EndGameScreen/public controls on a loaded game. Not a completed-match result claim.";
    return result;'''
assert needle in source;source=source.replace(needle,replacement)
needle='        gui.isRunning = false;\n        engine.finishSession();'
replacement='''        if (kind == "gameplay") {
            int sampledSteps = 0;
            auto recordedSamples = [&] {
                std::size_t maximum = 0;
                for (int team = 0; team < gui.game.mapHeader.getNumberOfTeams(); ++team)
                    maximum = std::max(maximum, gui.game.teams[team]->stats.getWinProbabilityHistory().size());
                return maximum;
            };
            while (recordedSamples() < 2 && sampledSteps < 1200) {
                require(engine.stepSession(now, {}), "Loaded-game fixture remains running while real samples accumulate");
                now += 40;
                ++sampledSteps;
                if (sampledSteps % 8 == 0) { draw(); SDL_Delay(1); }
            }
            require(recordedSamples() >= 2, "Two actual automatic probability samples exist; never inject chart values");
            const auto checksum = gui.game.checkSum();
            const auto metrics = Stats::catalogForBuildings(gui.game.buildingsTypes);
            const auto found = std::find_if(metrics.begin(), metrics.end(), [](const auto &metric) {
                return std::string(metric.id) == "win probability";
            });
            require(found != metrics.end(), "Actual game metric catalog contains win probability");
            const auto index = int(std::distance(metrics.begin(), found));
            FrontendScope frontend(true);
            Preview<EndGameScreen> results(&gui);
            results.selectMetric(index);
            results.render();
            auto saveResults = [&](const std::string &name) {
                GAGCore::DrawableSurface image(globals.gfx->getW(), globals.gfx->getH());
                image.drawSurface(0, 0, globals.gfx);
                require(IMG_SavePNG(image.getSDLSurface(), (output / (name + ".png")).string().c_str()), "Save actual results-screen fixture");
                require(gui.game.checkSum() == checksum, "Results rendering must preserve simulation checksum");
                result["captures"].push_back({{"name", name}, {"checksum_before_draw", checksum},
                    {"checksum_after_draw", gui.game.checkSum()}, {"image_width", image.getW()}, {"image_height", image.getH()}});
            };
            saveResults("win-probability-results");
            result["results_fixture"] = {{"kind", "production EndGameScreen rendering fixture from a loaded game"},
                {"origin_map", map}, {"simulation_tick", gui.game.stepCounter},
                {"not_a_completed_match", true}, {"probability_values_invented", false},
                {"selected_actual_metric_index", index}, {"title", TeamStatChart::title(*found)},
                {"automatic_sampling_engine_steps", sampledSteps},
                {"sample_origin", "Unmodified engine stepped with no injected orders until actual automatic samples existed; no probability/history values written."},
                {"help", TeamStatChart::about(*found)}, {"axis_title", TeamStatChart::axisTitle(*found, Stats::defaultView(*found))},
                {"control_bounds", results.bounds()},
                {"limits", "Live compact stats intentionally filter this metric. These are post-game results controls rendered on a loaded-game fixture, not a played/completed match."}};
            result["results_fixture"]["actual_probability_samples"] = Json::array();
            for (int team = 0; team < gui.game.mapHeader.getNumberOfTeams(); ++team)
                for (const auto &sample : gui.game.teams[team]->stats.getWinProbabilityHistory())
                    result["results_fixture"]["actual_probability_samples"].push_back({{"team", team}, {"tick", sample.tick}, {"permille", sample.permille}});
            if (GAGCore::phonePresentationRequested()) {
                results.activateResultControl(EndGameScreen::OPEN_METRIC_LIST);
                results.redraw();
                require(results.metricPickerOpen(), "Actual compact metric dropdown opened");
                saveResults("win-probability-metric-help");
                result["results_fixture"]["metric_popup_open"] = true;
            }
        }
        gui.isRunning = false;
        engine.finishSession();'''
assert needle in source;source=source.replace(needle,replacement)
target=base/'IntegrationPreviewHarness.cpp';target.write_text(source);proof={'production_source':str(original.relative_to(root)),'production_source_sha256':hashlib.sha256(original.read_bytes()).hexdigest(),'capture_wrapper':str(target.relative_to(root)),'capture_wrapper_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'differences':['Include actual EndGameScreen/Team definitions and add a redraw helper.','Record this ignored adapter source/hash in executed evidence identities.','After real gameplay/menu captures, create a production EndGameScreen on the loaded GameGUI, select the actual per-game metric via its public semantic entry point, and tap the real compact metric dropdown.','Step the unmodified engine without injected orders until at least two real automatic probability samples exist; never write/fabricate simulation state, match outcomes or chart values. Before/after rendering checksums must remain unchanged.'],'limits':['A results-screen rendering fixture from a loaded game, not proof of playing or completing an actual match.','Compact live statistics filtering is preserved; only post-game result controls display the new metric.']};(base/'capture-wrapper-source.json').write_text(json.dumps(proof,indent=2)+'\n');print('Prepared ignored production results-screen fixture wrapper')
