from pathlib import Path
import json,hashlib,shutil,subprocess
root=Path.cwd();base=root/'artifacts/marathi-localization/integration-bc8/native-registration187';src=root/'tools/TranslationPreviewHarness.cpp';s=src.read_text(encoding='utf-8');needle='''    const auto locale = pilotLocale(pilot);
    require(locale != "en", "Draft overlay must identify a translated locale");''';replacement='''    const auto locale = pilotLocale(pilot);
    if (SDL_getenv_unsafe("GLOB2_CAPTURE_REGISTERED")) {
        auto *strings = GAGCore::Toolkit::getStringTable();
        require(strings->hasLanguage(locale), "Actual registered locale exists");
        const int index = strings->getLangCode(locale);
        require(strings->isLangComplete(index), "Actual registered Marathi has no incomplete marker");
        require(strings->getStringInLang("[language]", index) == "मराठी", "Actual native Marathi name");
        strings->setDefaultLang(strings->getLangCode("en"));
        strings->setLang(index);
        return 0; // No draft overlay or reordered language list is written/loaded.
    }
    require(locale != "en", "Draft overlay must identify a translated locale");''';assert needle in s;s=s.replace(needle,replacement)
s=s.replace('screen == "settings" || screen == "settings-audio"','screen == "settings" || screen == "settings-audio" || screen == "settings-player"');s=s.replace('''            if (screen == "settings-audio") preview.selectCategory(SettingsScreen::Category::Audio);
            preview.render(); controls = preview.bounds();''','''            if (screen == "settings-audio") preview.selectCategory(SettingsScreen::Category::Audio);
            if (screen == "settings-player") preview.selectCategory(SettingsScreen::Category::Player);
            preview.render();
            if (screen == "settings-player") {
                auto *language = preview.host().find("player.language");
                require(language != nullptr, "Actual player language selector exists");
                require(language->accessibleText().find("मराठी") != std::string::npos &&
                        language->accessibleText().find("अपूर्ण") == std::string::npos,
                        "Actual selected registered-language label is Marathi without incomplete suffix");
            }
            controls = preview.bounds();''')
needle='    result["harfbuzz_version"] = {hb, hbMinor, hbMicro};\n    return result;';replacement='''    result["harfbuzz_version"] = {hb, hbMinor, hbMicro};
    result["capture_variant_source"] = "artifacts/marathi-localization/integration-bc8/native-registration187/RegisteredPreviewHarness.cpp";
    result["capture_variant_sha256"] = sha256(root / result["capture_variant_source"].get<std::string>());
    if (SDL_getenv_unsafe("GLOB2_CAPTURE_REGISTERED")) {
        auto *strings = GAGCore::Toolkit::getStringTable();
        const int index = strings->getLangCode("mr");
        result["registered_resource_probe"] = {{"locale", "mr"}, {"index", index},
            {"complete", strings->isLangComplete(index)}, {"native_name", strings->getStringInLang("[language]", index)},
            {"incomplete_metadata_sha256", sha256(root / "data/texts.incomplete.txt")},
            {"registered_language_inventory_sha256", sha256(root / "data/texts.list.txt")},
            {"actual_native_catalog_sha256", sha256(root / "data/texts.mr.txt")},
            {"overlay_keys_overridden", 0}, {"scope", "Actual registered language resources; no draft overlay/reordering."}};
    }
    return result;''';assert needle in s;s=s.replace(needle,replacement)
s=s.replace('{"kind", "production-screen-draft-overlay"}', '{"kind", "production-screen-registered-native-resource-capture"}');target=base/'RegisteredPreviewHarness.cpp';target.write_text(s,encoding='utf-8')
meta=json.loads((root/'artifacts/marathi-localization/integration-bc8/previews/mr/metadata.json').read_text(encoding='utf-8'));meta.update(source_revision='187ea34de87838911a42321fd52925d51da475b9',scope='Exact unchanged canonical Marathi native/online values; registered resource capture, no overlay.',registration_metadata_sha256=hashlib.sha256((root/'data/texts.incomplete.txt').read_bytes()).hexdigest());preview=base/'previews/mr';preview.mkdir(parents=True,exist_ok=True)
for surface in ['native','online']:shutil.copyfile(root/'artifacts/marathi-localization/integration-bc8/previews/mr'/(surface+'.json'),preview/(surface+'.json'))
(preview/'metadata.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');(base/'manifest.json').write_text(json.dumps({'revision':meta['source_revision'],'source_sha256':meta['source_sha256'],'scope':'New exact canonical-value input copy after metadata-only registration follow-up; original linguistic kit/code/font proof carried explicitly.'},indent=2)+'\n');(base/'registered-variant-source.json').write_text(json.dumps({'production_source':'tools/TranslationPreviewHarness.cpp','production_source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'variant':str(target.relative_to(root)),'variant_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'changes':['Early registered mode selects the actual already-loaded normal locale without writing/loading any overlay/list.','Assert actual MR completion/name and actual Player category selector accessibility label, then capture normal main/settings controls.','Record actual registration resource/catalog hashes and separate capture-variant source/hash.'],'scope':'Only capture code/input copies; no shipping/test/catalog changes.'},indent=2)+'\n')
