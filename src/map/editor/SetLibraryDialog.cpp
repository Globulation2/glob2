// SPDX-License-Identifier: GPL-3.0-or-later
#include "SetLibraryDialog.h"
#include "online/OnlineServices.h"
#include "online/PlatformClient.h"
#include "online/InstanceConfig.h"
#include "online/Sha256.h"
#include "MapAssetBundle.h"
#include "ResourceRegistry.h"
#include "TerrainRegistry.h"
#include "Version.h"
#include <FormatableString.h>
#include <iomanip>
#include <cctype>
#include <sstream>

namespace fe = Glob2UI;
namespace {
std::string escaped(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') out << c;
        else out << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
    }
    return out.str();
}
bool uuid(const std::string& value) {
    if (value.size() != 36) return false;
    for (unsigned i = 0; i < value.size(); ++i)
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (value[i] != '-') return false; }
        else if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    return true;
}
}
SetLibraryDialog::SetLibraryDialog(Map& map, std::function<void(std::string, std::vector<std::string>)> import, std::function<void()> changed)
    : InGameDialog(fe::Surface::Editor), map(map), changed(std::move(changed)), import(std::move(import)) {
    origin = Online::services().client.origin();
    if (origin.empty()) origin = Online::OFFICIAL_INSTANCE_ORIGIN;
    localEntries(); search();
}
void SetLibraryDialog::search() {
    HttpFetch::Request request;
    request.url = origin + "/api/v1/sets?limit=12&q=" + escaped(query) + "&cursor=" + escaped(cursor);
    if (!Online::services().client.accessToken().empty()) request.headers.emplace_back("Authorization", "Bearer " + Online::services().client.accessToken());
    request.responseLimit = 2 * 1024 * 1024;
    fetch = HttpFetch::start(std::move(request)); downloading = false;
    package = nullptr; bytes.clear(); status = fe::tr("[set library loading]"); invalidate();
}
void SetLibraryDialog::download(const nlohmann::json& set, const nlohmann::json& version) {
    HttpFetch::Request request;
    request.url = origin + "/api/v1/sets/" + set.at("id").get<std::string>() + "/versions/" + version.at("id").get<std::string>() + "/file";
    if (!Online::services().client.accessToken().empty()) request.headers.emplace_back("Authorization", "Bearer " + Online::services().client.accessToken());
    request.responseLimit = MapAssetBundle::MaximumBytes;
    expectedHash = version.at("hash").get<std::string>();
    fetch = HttpFetch::start(std::move(request)); downloading = true; status = fe::tr("[set library downloading]"); invalidate();
}
void SetLibraryDialog::onUpdate(Uint32) {
    if (!fetch || fetch->state() == HttpFetch::State::Pending) return;
    try {
        if (fetch->state() != HttpFetch::State::Done || fetch->response().status != 200)
            throw std::runtime_error(fe::tr("[set library connection error]"));
        const auto& body = fetch->response().body;
        if (downloading) {
            if (Online::Sha256::hex(body) != expectedHash) throw std::runtime_error(fe::tr("[set library hash error]"));
            inspected=std::make_unique<Map>(); inspected->setSize(5,5,GRASS); inspected->importSet(body);
            previews.bind(inspected->frozenTerrainRegistry(),inspected->frozenResourceRegistry(),inspected->frozenAssetBundle());
            package = MapAssetBundle::parseDocument(body);
            updateVersion.clear();
            if (!package.is_object() || !package.at("terrains").is_array() || !package.at("resources").is_array())
                throw std::runtime_error(fe::tr("[set library package error]"));
            selected.clear();
            for (const auto* entries : {&package.at("terrains"), &package.at("resources")})
                for (const auto& entry : *entries) selected.insert(entry.at("key").get<std::string>());
            bytes = body; status = fe::tr("[set library selection help]");
        } else {
            auto page = nlohmann::json::parse(body);
            if (!page.at("items").is_array() || page.at("items").size() > 12) throw std::runtime_error(fe::tr("[set library listing error]"));
            for (const auto& set : page.at("items")) {
                if (!uuid(set.at("id").get<std::string>()) || set.at("title").get<std::string>().size() > 512 ||
                    !set.at("versions").is_array() || set.at("versions").size() > 50) throw std::runtime_error(fe::tr("[set library listing error]"));
                for (const auto& version : set.at("versions"))
                    if (!uuid(version.at("id").get<std::string>()) || !Online::Sha256::isHexDigest(version.at("hash").get<std::string>()) ||
                        !version.at("minVersionMinor").is_number_integer() || version.at("label").get<std::string>().size() > 256)
                        throw std::runtime_error(fe::tr("[set library release error]"));
            }
            items = page.at("items"); cursor = page.value("nextCursor", std::string{});
            status = items.empty() ? fe::tr("[set library empty]") : fe::tr("[set library release help]");
        }
    } catch (const std::exception& error) { status = error.what(); package = nullptr; bytes.clear(); }
    fetch.reset(); invalidate();
}
fe::Element SetLibraryDialog::build(const fe::Presentation& p) {
    std::vector<fe::Element> body{fe::heading(fe::tr("[set library heading]")), fe::paragraph(status)};
    body.push_back(fe::button("set/local", local ? fe::tr("[set library browse]") : fe::tr("[set library local]"), [this]{local=!local;editKey.clear();localEntries();invalidate();}));
    if(local) {
        for(const auto& credit:map.frozenAssetBundle()->credits) {
            body.push_back(fe::heading(credit.at("title").get<std::string>()));
            body.push_back(fe::paragraph(credit.at("license").get<std::string>()+" · "+std::string(GAGCore::FormattableString(fe::tr("[set library release %0]")).arg(credit.at("versionId").get<std::string>()))));
            for(const auto& author:credit.at("authors")) body.push_back(fe::paragraph(author.at("author").get<std::string>()+" · "+author.at("license").get<std::string>()+(author.value("source",std::string{}).empty()?std::string{}:" · "+author.at("source").get<std::string>())));
        }
        for(const auto& entry:localDefinitions) {
            const auto key=entry.at("key").get<std::string>();
            const auto label=entry.contains("name")?entry.at("name").get<std::string>():entry.at("presentation").at("name").get<std::string>();
            body.push_back(fe::button("local/"+key,label,[this,key,entry]{editKey=key;editDefinition=entry.dump(2);editArtwork=map.frozenAssetBundle()->terrains.value(key,nlohmann::json(nullptr)).dump(2);invalidate();}));
        }
        if(!editKey.empty()) {
            body.push_back(fe::paragraph(fe::tr("[set library local help]")));
            fe::TextEditorOptions options;options.lines=12;
            body.push_back(fe::heading(fe::tr("[set library definition]")));
            body.push_back(fe::textEditor("local/definition",editDefinition,[this](const std::string& value){editDefinition=value;},options));
            if(map.terrainRegistry().find(editKey)) {
                body.push_back(fe::heading(fe::tr("[set library artwork]")));
                body.push_back(fe::textEditor("local/artwork",editArtwork,[this](const std::string& value){editArtwork=value;},options));
            }
            body.push_back(fe::button("local/apply",fe::tr("[set library apply]"),[this]{try{map.editCustomEntry(editKey,editDefinition,editArtwork);changed();localEntries();status=fe::tr("[set library updated]");invalidate();}catch(const std::exception& e){status=e.what();invalidate();}}));
        }
        return fe::footer(fe::scroll("set/local-list",fe::column(std::move(body),{p.pt(8)})),dialogActions({{"close",fe::tr("[Close]"),[this]{finish(0);}}},p));
    }
    fe::TextFieldOptions field; field.maxLength = 128; field.placeholder = fe::tr("[set library search hint]");
    body.push_back(fe::textField("set/search", query, [this](const std::string& value) { query = value; }, field));
    body.push_back(fe::button("set/search-button", fe::tr("[set library search]"), [this] { cursor.clear(); search(); }));
    if (!package.is_null()) {
        body.push_back(fe::heading(package.value("title", std::string(fe::tr("[set library set]")))));
        body.push_back(fe::paragraph(GAGCore::FormattableString(fe::tr("[set library license %0]")).arg(package.value("license", std::string{}))));
        for (const auto& credit : package.at("credits")) body.push_back(fe::paragraph(credit.at("author").get<std::string>()+" · "+credit.at("license").get<std::string>()+(credit.value("source",std::string{}).empty()?std::string{}:" · "+credit.at("source").get<std::string>())));
        body.push_back(fe::button("set/back", fe::tr("[set library back]"), [this] {
            package=nullptr;bytes.clear();selected.clear();updateVersion.clear();inspected.reset();
            previews.bind(nullptr,nullptr);
            status=items.empty()?fe::tr("[set library empty]"):fe::tr("[set library release help]");invalidate();
        }));
        unsigned previewCount=0;
        for (const auto* entries : {&package.at("terrains"), &package.at("resources")})
            for (const auto& entry : *entries) {
                const auto key = entry.at("key").get<std::string>();
                const auto label = entry.contains("name") ? entry.at("name").get<std::string>() : entry.at("presentation").at("name").get<std::string>();
                auto choice=fe::toggle("entry/" + key, label, selected.contains(key), [this,key](bool enable) {
                    if (enable) selected.insert(key); else selected.erase(key); invalidate();
                });
                GAGCore::DrawableSurface* image=nullptr;
                if(inspected && previewCount++<64) {
                    if(auto id=inspected->terrainRegistry().find(key))image=previews.terrain(*id,64);
                    else if(auto id=inspected->resourceRegistry().find(key))image=previews.resource(*id,GRASS,64);
                }
                // Keep each swatch beside its named control so its identity is
                // clear even when several entries share similar artwork.
                if(image)body.push_back(fe::row({fe::image(image),std::move(choice)},{p.pt(8)}));
                else body.push_back(std::move(choice));
            }
        if(package.at("terrains").size()+package.at("resources").size()>64)
            body.push_back(fe::paragraph(fe::tr("[set library preview limit]")));
        for(const auto& credit:map.frozenAssetBundle()->credits)if(credit.at("setId")==package.at("setId") && credit.at("versionId")!=package.at("versionId")) {
            const auto version=credit.at("versionId").get<std::string>();
            body.push_back(fe::toggle("update/"+version,GAGCore::FormattableString(fe::tr("[set library replace %0]")).arg(version),updateVersion==version,[this,version](bool enable){updateVersion=enable?version:std::string{};invalidate();}));
        }
        if(!updateVersion.empty())body.push_back(fe::paragraph(fe::tr("[set library update help]")));
        fe::ButtonOptions options; options.enabled = !selected.empty();
        body.push_back(fe::button("set/import", fe::tr("[set library import]"), [this] {
            try { if(updateVersion.empty())import(bytes, {selected.begin(), selected.end()});else {map.updateSet(bytes,updateVersion,{selected.begin(),selected.end()});changed();} finish(1); }
            catch (const std::exception& error) { status = error.what(); invalidate(); }
        }, options));
    } else for (const auto& set : items) {
        body.push_back(fe::heading(set.at("title").get<std::string>()));
        for (const auto& version : set.at("versions")) {
            fe::ButtonOptions options; options.enabled = !fetch && version.at("minVersionMinor").get<int>() <= VERSION_MINOR;
            body.push_back(fe::button("release/" + version.at("id").get<std::string>(), version.at("label").get<std::string>(),
                [this,set,version] { download(set,version); }, options));
        }
    }
    if (!cursor.empty() && package.is_null()) body.push_back(fe::button("set/next", fe::tr("[set library next]"), [this] { search(); }));
    return fe::footer(fe::scroll("set/list", fe::column(std::move(body), {p.pt(8)})),
        dialogActions({{"close", fe::tr("[Close]"), [this] { finish(0); }}}, p));
}

void SetLibraryDialog::localEntries() {
    localDefinitions=nlohmann::json::array();
    const auto terrains=nlohmann::json::parse(map.terrainRegistry().serialize());
    const auto resources=nlohmann::json::parse(map.resourceRegistry().serialize());
    for(auto entry:terrains.at("terrains")) {
        entry.erase("id");entry.erase("presentation");entry["base"]=entry.at("appearance");localDefinitions.push_back(entry);
    }
    for(const auto& entry:resources.at("resources"))
        if(entry.at("key").get<std::string>().find(':')!=std::string::npos)localDefinitions.push_back(entry);
}
