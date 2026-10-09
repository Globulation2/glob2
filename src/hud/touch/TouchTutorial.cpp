// SPDX-License-Identifier: GPL-3.0-or-later
#include "TouchTutorial.h"

namespace TouchTutorial
{
namespace
{
struct Source { std::uint64_t hash; std::size_t size; int chapter; };
struct Alias { std::uint64_t hash; std::size_t size; int chapter; int message; };
#include "TouchTutorialCatalog.inc"
}
std::uint64_t fingerprint(std::string_view source)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char c : source) hash = (hash ^ c) * UINT64_C(1099511628211);
    return hash;
}
int chapter(std::string_view source)
{
    const auto hash = fingerprint(source);
    for (const auto &entry : sources)
        if (entry.hash == hash && entry.size == source.size()) return entry.chapter;
    return 0;
}
const Message *find(int chapter, std::string_view original)
{
    if (!chapter || original.empty()) return nullptr;
    const auto hash = fingerprint(original);
    for (const auto &entry : aliases)
        if (entry.chapter == chapter && entry.hash == hash && entry.size == original.size())
            return &catalog[entry.message];
    return nullptr;
}
std::span<const Message> messages() { return catalog; }
bool matches(const Message *message, std::string_view original)
{
    if (!message) return false;
    const auto hash = fingerprint(original);
    for (const auto &entry : aliases)
        if (entry.chapter == message->chapter && entry.hash == hash && entry.size == original.size() &&
            &catalog[entry.message] == message) return true;
    return false;
}
}
