from pathlib import Path
import hashlib,json
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-bc8/native';src=root/'tools/TranslationPreviewHarness.cpp';s=src.read_text(encoding='utf-8');s=s.replace('#include "EndGameScreen.h"','#include "EndGameScreen.h"\n#include "InGameStatistics.h"\n#include "GameGUITouch.h"\n#include "MapEdit.h"\n#include "EditorDock.h"\n#include "PhoneEditor.h"')
needle='struct TranslationPreviewAccess\n'
helpers='''class MobileGalleryGameplay
{
public:
    static InGameStatisticsPanel &openStatistics(GameGUI &gui) {
        gui.clearSelection();
        if (gui.touch && gui.touch->usesHUD()) gui.touch->menuAction(3);
        else gui.displayMode = GameGUI::STAT_TEXT_VIEW;
        return gui.statistics();
    }
    static void openOverlays(GameGUI &gui) {
        gui.clearSelection();
        if (gui.touch && gui.touch->usesHUD()) {
            gui.touch->closeStatistics();
            gui.touch->lensOpen = true;
        } else gui.displayMode = GameGUI::STAT_GRAPH_VIEW;
    }
    static void closeStatistics(GameGUI &gui) {
        if (gui.touch) { gui.touch->closeStatistics(); gui.touch->lensOpen = false; }
        gui.displayMode = GameGUI::CONSTRUCTION_VIEW;
    }
    static void revealTerrain(MapEdit &editor, const std::string &id) {
        const auto *entry = editor.findBrush(id);
        if (!entry) throw std::runtime_error("Actual terrain brush must exist: " + id);
        if (editor.phone) {
            auto &phone = *editor.phone;
            phone.chooseMode(0); phone.tools = true; phone.prepare();
            const int row = phone.rowOf(id);
            if (row < 0) throw std::runtime_error("Actual phone tray must contain terrain brush: " + id);
            phone.offset = std::max(0.0, phone.rows[row].x - (phone.cardBar.w - phone.rows[row].width) / 2);
            phone.prepare();
        } else if (editor.editorDock()) {
            editor.editorDock()->showTab(EditorDock::Tab::Terrain);
            editor.editorDock()->setSearch(entry->label);
        }
        editor.performAction(entry->action);
    }
    static nlohmann::ordered_json terrainContext(MapEdit &editor, const std::string &id) {
        const auto *entry = editor.findBrush(id);
        nlohmann::ordered_json r = {{"brush_id",id},{"actual_label",entry->label},{"actual_action",entry->action},
                           {"selected_brush",editor.currentBrushId()},{"phone_editor",bool(editor.phone)}};
        if (editor.phone) {
            auto &phone = *editor.phone; const int row = phone.rowOf(id);
            if (row >= 0) {
                const auto &card = phone.rows[row];
                r["phone_card_lines"] = card.lines;
                r["phone_card_bounds"] = {card.rect.x,card.rect.y,card.rect.w,card.rect.h};
            }
        } else if (editor.editorDock()) {
            const auto rect=editor.editorDock()->host().bounds("brush/"+id);
            r["actual_card_bounds"]={rect.x,rect.y,rect.w,rect.h};
        }
        return r;
    }
};
'''
assert needle in s;s=s.replace(needle,helpers+needle)
needle='    result["harfbuzz_version"] = {hb, hbMinor, hbMicro};\n    return result;'
s=s.replace(needle,'''    result["harfbuzz_version"] = {hb, hbMinor, hbMicro};
    result["capture_adapter_source"] = "artifacts/marathi-localization/integration-bc8/native/ExtraPreviewHarness.cpp";
    result["capture_adapter_sha256"] = sha256(root / result["capture_adapter_source"].get<std::string>());
    result["capture_adapter_scope"] = "Capture-only navigation of real production stats and terrain/editor controls; no fabricated readings or completed-match claim.";
    return result;''')
needle='''        if (kind == "gameplay") {
            // Use the real statistics screen'''
extra='''        if (kind == "gameplay") {
            auto &panel = MobileGalleryGameplay::openStatistics(gui);
            // Request history through ordinary engine admission, without injecting data.
            for (int step=0;step<24;++step) {
                require(engine.stepSession(now, {}), "Real session remains running during stats admission");
                now += 40; draw(); SDL_Delay(1);
            }
            const Uint64 statsDeadline=SDL_GetTicks()+15000;
            while (SDL_GetTicks()<statsDeadline) {
                draw(); panel.update(Uint32(SDL_GetTicks()));
                if (panel.history()) break;
                SDL_Delay(1);
            }
            result["in_game_statistics_probe"]={{"kind","Actual production panel on the loaded real local game"},
                {"history_available",bool(panel.history())},{"simulation_tick",gui.game.stepCounter},
                {"readings_invented",false},{"not_a_completed_match",true},
                {"limits","UI navigation fixture. Data are the real extracted game history; missing readings remain missing."}};
            auto controls = [&] {
                Json nodes=Json::array();
                for (auto *node:panel.host().interactiveNodes()) {
                    const auto r=node->bounds;
                    nodes.push_back({{"key",node->key},{"accessible_text",node->accessibleText()},
                        {"bounds",{r.x,r.y,r.w,r.h}}});
                }
                return nodes;
            };
            for (const auto &[topic,name]:std::vector<std::pair<InGameStatisticsPanel::Topic,std::string>>{
                {InGameStatisticsPanel::Topic::Overview,"stats-overview"},
                {InGameStatisticsPanel::Topic::Economy,"stats-economy"},
                {InGameStatisticsPanel::Topic::World,"stats-world"}}) {
                panel.selectTopic(topic); panel.update(Uint32(SDL_GetTicks()));
                capture(name); result["in_game_statistics_probe"]["controls"][name]=controls();
            }
            panel.selectTopic(InGameStatisticsPanel::Topic::Overview); panel.update(Uint32(SDL_GetTicks())); draw();
            panel.host().scrollIntoView("stats/metric/intruders");
            capture("stats-overview-defence");
            panel.selectTopic(InGameStatisticsPanel::Topic::Economy); panel.selectMetric("idle workers");
            panel.update(Uint32(SDL_GetTicks())); draw();
            panel.host().scrollIntoView("stats/full-history/idle workers");
            capture("stats-inline-history");
            panel.openMetric("idle workers");
            capture("stats-full-history");
            TranslationPreviewAccess::closeMenu(gui);
            panel.selectTopic(InGameStatisticsPanel::Topic::Colony); panel.update(Uint32(SDL_GetTicks())); draw();
            panel.host().scrollIntoView("stats/unit-details"); draw();
            const auto detail=panel.host().bounds("stats/unit-details");
            panel.host().tapAt(detail.center());
            capture("stats-unit-details");
            MobileGalleryGameplay::openOverlays(gui);
            capture("stats-map-overlays");
            MobileGalleryGameplay::closeStatistics(gui);
        }
        if (kind == "gameplay") {
            // Use the real statistics screen'''
assert needle in s;s=s.replace(needle,extra)
needle='''    writeJson(output / (captureKind + ".json"), result);'''
extra='''    if (kind == "gameplay") {
        MapEdit editor;
        require(editor.load("maps/balanced.map"), "Load actual editor fixture map");
        editor.beginEditing();
        const fs::path editorProfile=globals.fileManager->getDir(0);
        for (const auto &key:std::vector<std::string>{"lush_grass","dry_grass","dune_sand","pale_sand"}) {
            const std::string id="terrain/"+key, name="editor-"+key;
            MobileGalleryGameplay::revealTerrain(editor,id);
            const auto before=editor.game.checkSum();
            const auto bitmap=editorProfile/("translation-preview-"+name+".bmp");fs::remove(bitmap);
            editor.drawEditing();
            globals.gfx->printScreen(bitmap.filename().string());
            const Uint64 deadline=SDL_GetTicks()+15000;
            while (!fs::exists(bitmap) && SDL_GetTicks()<deadline) {editor.drawEditing();SDL_Delay(1);}
            require(fs::exists(bitmap),"Actual editor screenshot finished");
            Surface image(SDL_LoadBMP(bitmap.string().c_str()),SDL_DestroySurface);
            require(bool(image),"Read actual editor screenshot");
            require(IMG_SavePNG(image.get(),(output/(name+".png")).string().c_str()),"Save editor screenshot");
            require(before==editor.game.checkSum(),"Terrain palette rendering must preserve game checksum");
            result["captures"].push_back({{"name",name},{"checksum_before_draw",before},
                {"checksum_after_draw",editor.game.checkSum()},{"image_width",image->w},{"image_height",image->h}});
            result["terrain_probe"].push_back(MobileGalleryGameplay::terrainContext(editor,id));
        }
    }
    writeJson(output / (captureKind + ".json"), result);'''
assert needle in s;s=s.replace(needle,extra)
target=base/'ExtraPreviewHarness.cpp';target.write_text(s,encoding='utf-8');record={'original':'tools/TranslationPreviewHarness.cpp','original_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'adapter':str(target.relative_to(root)),'adapter_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'changes':['Add capture-only navigation through existing fixture friendship, using real statistics public topic/metric/history controls and actual GameGUITouch menu action.','Admit real history through ordinary engine stepping; no injection of metrics, world state, orders or probability values.','Capture Overview/Economy/World/inline/full/unit/overlays and actual four terrain brush cards via real editor search/scroll/selection. No map painting/saving.','Preserve production screenshots/checksum assertions and append exact adapter identity to evidence.'],'limits':['Desktop compact presentation is not a physical/native mobile-platform run.','No completed-match or calibrated probability claim. Actual available readings remain unchanged; missing readings are not invented.','Fixture directly navigates UI state and scroll positions for coverage; it does not claim physical user input verification.']};(base/'extra-capture-source.json').write_text(json.dumps(record,indent=2)+'\n');print(record['adapter_sha256'])
