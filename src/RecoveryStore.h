// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <FileManager.h>
#include <string>
#include <vector>

// Two independently checked generations, published by an atomic commit record.
// Game bytes remain in the established .game format inside the envelope.
class RecoveryStore
{
public:
    struct Record {
        std::string campaign, mission;
        std::vector<unsigned char> game;
    };
    explicit RecoveryStore(GAGCore::FileManager& files, std::string directory = "recovery");
    static bool enabled();
    bool checkpoint(const std::function<void(GAGCore::OutputStream&)>& writer,
                    const std::string& campaign = {}, const std::string& mission = {});
    std::vector<Record> candidates() const;
    bool pending() const;
    bool dismiss();
    std::string materialize(const Record& record);
private:
    GAGCore::FileManager& files;
    std::string directory, identity;
    struct Commit { std::string identity; unsigned slot = 0; };
    Commit readCommit() const;
};
