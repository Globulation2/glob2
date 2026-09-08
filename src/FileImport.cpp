// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileImport.h"
#include "GameGUI.h"
#include "OrderMessages.h"
#include "Order.h"
#include "ReplayReader.h"
#include "Utilities.h"
#include "Version.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <algorithm>
#include <cctype>
#include <limits>

using namespace GAGCore;
namespace {
std::string lower(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
bool validName(const std::string& name, const std::string& extension) {
    if (name.empty() || name.size() > 512 || name.front() == '.' || name.back() == ' ' || name.back() == '.') return false;
    for (unsigned char c : name)
        if (c < 32 || c == 127 || std::string("/\\<>:\"|?*").find(c) != std::string::npos) return false;
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || lower(name.substr(dot + 1)) != extension) return false;
    const auto stem = lower(name.substr(0, name.find('.')));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return false;
    if (stem.size() == 4 && (stem.substr(0,3) == "com" || stem.substr(0,3) == "lpt") && stem[3] >= '1' && stem[3] <= '9') return false;
    return true;
}
struct RestoreRng {
    std::string previous = getSyncRandState();
    ~RestoreRng() { setSyncRandState(previous); }
};
}
FileImport::FileImport(ApplicationHost::SelectedFile file, std::string extension, Persist persist, CooperativeSlice slice)
    : file(std::move(file)), extension(std::move(extension)), persist(std::move(persist)), slice(std::move(slice)) {
    task.emplace(validate());
}
FileImport::~FileImport() {
    task.reset();
    // Imports always create a new name. An abandoned failed persistence must
    // not become a silently successful import during a later unrelated sync.
    if (!destination.empty() && current != State::Succeeded)
        Toolkit::getFileManager()->remove(destination);
}
CooperativeTask FileImport::validate() {
    if ((extension != "game" && extension != "map" && extension != "replay") ||
        !validName(file.name, extension) || file.bytes.empty() || file.bytes.size() > 64u*1024u*1024u)
        co_return false;
    BinaryInputStream input(new MemoryStreamBackend(file.bytes.data(), file.bytes.size()));
    input.seekFromStart(0);
    BinaryInputStream::CheckedReads checked(&input);
    MapHeader header;
    if (!header.load(&input) || header.getMapOffset() < input.getPosition() || header.getMapOffset() > file.bytes.size() - 4 ||
        header.getIsSavedGame() != (extension != "map")) co_return false;
    const auto mapStart = file.bytes.begin() + header.getMapOffset();
    if (!std::equal(mapStart, mapStart + 4, "MapB")) co_return false;
    input.seekFromStart(0);
    RestoreRng rng;
    GameGUI gui(false);
    if (!(co_await gui.loadTask(&input))) co_return false;
    if (extension != "map" && (gui.localPlayer < 0 || gui.localPlayer >= gui.game.gameHeader.getNumberOfPlayers() ||
        gui.localTeamNo < 0 || gui.localTeamNo >= gui.game.mapHeader.getNumberOfTeams())) co_return false;
    if (extension == "replay") {
        const auto major = input.readUint16("versionMajor");
        const auto minor = input.readUint16("versionMinor");
        if (major != VERSION_MAJOR || minor < REPLAY_MINIMUM_VERSION_MINOR || minor > VERSION_MINOR) co_return false;
        Uint64 ticks = 0;
        for (;;) {
            ticks += minor >= REPLAY_UINT32_STEP_COUNTER_VERSION_MINOR ? input.readUint32("steps") : input.readUint16("steps");
            if (ticks > std::numeric_limits<Uint32>::max()) co_return false;
            NetSendOrder message;
            message.setDecodeVersionMinor(minor);
            message.decodeData(&input);
            if (message.getOrder()->getOrderType() == ORDER_NULL) break;
            co_await CooperativeTask::checkpoint();
        }
    }
    // The complete supported format must be consumed, including the replay
    // terminator. Unlike playback recovery, importing never truncates corruption.
    co_return input.getPosition() == file.bytes.size();
}
void FileImport::advance() {
    try {
        if (current == State::Validating) {
            if (!slice.advance(*task)) return;
            const bool valid = task->result();
            task.reset();
            if (!valid || ApplicationHost::storageRestoreFailed()) { current = State::Failed; return; }
            auto& files = *Toolkit::getFileManager();
            const auto directory = extension == "game" ? "games" : extension == "map" ? "maps" : "replays";
            const auto name = file.name.substr(0, file.name.rfind('.'));
            for (unsigned suffix = 0; suffix < 10000; ++suffix) {
                const auto candidate = glob2NameToFilename(directory, name + (suffix ? " (" + std::to_string(suffix) + ")" : ""), extension);
                if (files.exists(candidate)) continue;
                if (!files.writeAtomically(candidate, [this](OutputStream& output) {
                    output.write(file.bytes.data(), file.bytes.size(), "import");
                })) { current = State::Failed; return; }
                destination = candidate;
                current = State::Failed;
                retryPersistence();
                return;
            }
            current = State::Failed;
        } else if (current == State::Persisting) {
            const auto result = persistence->state();
            if (result == ApplicationHost::PersistenceState::Pending) return;
            current = result == ApplicationHost::PersistenceState::Succeeded ? State::Succeeded : State::Failed;
        }
    } catch (const std::exception&) {
        task.reset();
        current = State::Failed;
    }
}
void FileImport::retryPersistence() {
    if (!canRetry()) return;
    try {
        persistence = persist();
        current = persistence ? State::Persisting : State::Failed;
    } catch (const std::exception&) { current = State::Failed; }
}
bool FileImport::exportFile() const { return ApplicationHost::exportFile(file.name, file.bytes); }
