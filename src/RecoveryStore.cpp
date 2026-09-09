// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecoveryStore.h"
#include <BinaryStream.h>
#include <zlib.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>

using namespace GAGCore;
namespace {
constexpr size_t limit = 64u * 1024u * 1024u;
constexpr Uint32 magic = 0x47524331; // GRC1, recovery envelope version 1
class BoundedMemory : public MemoryStreamBackend {
    void write(const void* data, size_t size) override {
        const auto position = getPosition();
        if (position > limit || size > limit - position)
            throw std::length_error("Recovery checkpoint exceeds size limit");
        MemoryStreamBackend::write(data, size);
    }
};
std::vector<unsigned char> readFile(FileManager& files, const std::string& path, size_t maximum) {
    BinaryInputStream input(files.openInputStreamBackend(path));
    if (!input.isValid()) return {};
    BinaryInputStream::CheckedReads checked(&input);
    input.seekFromEnd(0);
    const auto size = input.getPosition();
    if (!size || size > maximum) return {};
    input.seekFromStart(0);
    std::vector<unsigned char> bytes(size);
    input.read(bytes.data(), size, "record");
    return bytes;
}
std::vector<unsigned char> serialize(const std::function<void(OutputStream&)>& writer) {
    auto* backend = new BoundedMemory;
    BinaryOutputStream output(backend);
    writer(output);
    const auto size = output.getPosition();
    if (size > limit) throw std::length_error("Recovery checkpoint exceeds size limit");
    const auto* begin = reinterpret_cast<const unsigned char*>(backend->getBuffer());
    return {begin, begin + size};
}
bool writeRecord(FileManager& files, const std::string& path, const std::vector<unsigned char>& bytes) {
    return files.writeAtomically(path, [&](OutputStream& output) {
        output.write(bytes.data(), bytes.size(), "record");
        output.writeUint32(crc32(0, bytes.data(), bytes.size()), "crc");
    });
}
std::vector<unsigned char> checkedRecord(FileManager& files, const std::string& path) {
    auto bytes = readFile(files, path, limit + 4);
    if (bytes.size() < 4) return {};
    BinaryInputStream input(new MemoryStreamBackend(bytes.data() + bytes.size() - 4, 4));
    input.seekFromStart(0);
    const auto checksum = input.readUint32("crc");
    bytes.resize(bytes.size() - 4);
    if (crc32(0, bytes.data(), bytes.size()) != checksum) return {};
    return bytes;
}
}
RecoveryStore::RecoveryStore(FileManager& files, std::string directory)
    : files(files), directory(std::move(directory)) {
    static std::atomic<unsigned long> sequence{0};
    identity = std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++);
}
bool RecoveryStore::enabled() {
#ifdef GLOB2_MOBILE
    return true;
#else
    const char* value = std::getenv("GLOB2_RECOVERY_TEST");
    return value && std::string(value) == "1";
#endif
}
RecoveryStore::Commit RecoveryStore::readCommit() const {
    try {
        const auto bytes = checkedRecord(files, directory + "/state");
        if (bytes.empty()) return {};
        BinaryInputStream input(new MemoryStreamBackend(bytes.data(), bytes.size()));
        input.seekFromStart(0);
        BinaryInputStream::CheckedReads checked(&input);
        if (input.readUint32("magic") != magic) return {};
        Commit commit;
        commit.identity = input.readText("identity");
        commit.slot = input.readUint32("slot");
        if (commit.identity.size() > 128 || commit.slot > 1 || input.getPosition() != bytes.size()) return {};
        return commit;
    } catch (const std::exception&) { return {}; }
}
bool RecoveryStore::pending() const { return !readCommit().identity.empty(); }
bool RecoveryStore::checkpoint(const std::function<void(OutputStream&)>& writer,
                                const std::string& campaign, const std::string& mission) {
    try {
        if (campaign.size() > 1024 || mission.size() > 1024) return false;
        files.addWriteSubdir(directory);
        const auto previous = readCommit();
        const unsigned slot = 1 - previous.slot;
        const auto game = serialize(writer);
        if (game.empty()) return false;
        const auto record = serialize([&](OutputStream& output) {
            output.writeUint32(magic, "magic");
            output.writeText(identity, "identity");
            output.writeText(campaign, "campaign");
            output.writeText(mission, "mission");
            output.writeUint32(game.size(), "size");
            output.write(game.data(), game.size(), "game");
        });
        if (!writeRecord(files, directory + "/slot" + std::to_string(slot), record)) return false;
        return writeRecord(files, directory + "/state", serialize([&](OutputStream& output) {
            output.writeUint32(magic, "magic");
            output.writeText(identity, "identity");
            output.writeUint32(slot, "slot");
        }));
    } catch (const std::exception& error) {
        std::cerr << "Recovery checkpoint failed: " << error.what() << '\n';
        return false;
    }
}
std::vector<RecoveryStore::Record> RecoveryStore::candidates() const {
    const auto commit = readCommit();
    std::vector<Record> records;
    if (commit.identity.empty()) return records;
    for (unsigned slot : {commit.slot, 1 - commit.slot}) {
        try {
            const auto bytes = checkedRecord(files, directory + "/slot" + std::to_string(slot));
            if (bytes.empty()) continue;
            BinaryInputStream input(new MemoryStreamBackend(bytes.data(), bytes.size()));
            input.seekFromStart(0);
            BinaryInputStream::CheckedReads checked(&input);
            if (input.readUint32("magic") != magic || input.readText("identity") != commit.identity) continue;
            Record record;
            record.campaign = input.readText("campaign");
            record.mission = input.readText("mission");
            const auto size = input.readUint32("size");
            if (record.campaign.size() > 1024 || record.mission.size() > 1024 || !size ||
                input.getPosition() > bytes.size() || size != bytes.size() - input.getPosition()) continue;
            record.game.resize(size);
            input.read(record.game.data(), size, "game");
            records.push_back(std::move(record));
        } catch (const std::exception&) {}
    }
    return records;
}
bool RecoveryStore::dismiss() {
    if (!pending()) return true;
    return writeRecord(files, directory + "/state", serialize([&](OutputStream& output) {
        output.writeUint32(magic, "magic"); output.writeText("", "identity"); output.writeUint32(0, "slot");
    }));
}
std::string RecoveryStore::materialize(const Record& record) {
    files.addWriteSubdir(directory);
    const auto path = directory + "/resume.game";
    if (record.game.empty() || record.game.size() > limit || !files.writeAtomically(path, [&](OutputStream& output) {
        output.write(record.game.data(), record.game.size(), "game");
    })) return {};
    return path;
}
