// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "online/HttpFetch.h"
#include <nlohmann/json.hpp>
#include <set>
#include "Map.h"
#include "BrushSwatches.h"

class SetLibraryDialog : public Glob2UI::InGameDialog {
public:
    SetLibraryDialog(Map& map, std::function<void(std::string, std::vector<std::string>)> import, std::function<void()> changed);
    const char* recordingId() const override { return "set_library"; }
    Glob2UI::Element build(const Glob2UI::Presentation&) override;
protected:
    void onUpdate(Uint32) override;
    void onEscape() override { finish(0); }
    double maxWidth() const override { return 760; }
private:
    void search();
    void localEntries();
    Map& map;
    std::function<void()> changed;
    BrushSwatches previews;
    bool local = false;
    nlohmann::json localDefinitions = nlohmann::json::array();
    std::string editKey, editDefinition, editArtwork, updateVersion;
    std::unique_ptr<Map> inspected;
    void download(const nlohmann::json& set, const nlohmann::json& version);
    std::function<void(std::string, std::vector<std::string>)> import;
    std::string origin, query, status, bytes, expectedHash, cursor;
    std::unique_ptr<HttpFetch::Fetch> fetch;
    nlohmann::json items = nlohmann::json::array(), package;
    std::set<std::string> selected;
    bool downloading = false;
};
