// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <string>
namespace GAGCore { class FileManager; }
namespace Online {
class InstanceConfig;
struct ReplayAppearance {
    std::string origin, matchId;
};
// Optional presentation-only companion. Never changes replay bytes or version gates.
std::string replayAppearanceJson(const ReplayAppearance &, const std::string &replayHash);
std::optional<ReplayAppearance> parseReplayAppearance(const std::string &json,
    const std::string &replayHash, const InstanceConfig &);
bool writeReplayAppearance(GAGCore::FileManager &, const std::string &filename,
    const ReplayAppearance &);
std::optional<ReplayAppearance> readReplayAppearance(GAGCore::FileManager &,
    const std::string &filename, const InstanceConfig &);
}
