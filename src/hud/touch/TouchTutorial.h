// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <string_view>

// Client-only guidance for the unchanged, bundled SGSL tutorial. No tutorial
// cursor, text or fingerprint is part of the simulation or save format.
namespace TouchTutorial
{
enum class Target { None, Menu, Build, Flags, Tools, History, Inspector,
    Workers, Production, Range, Upgrade, Statistics, Brush, Palette };
struct Page
{
    const char *key;
    const char *rowsKey;
    Target target;
    const char *choice;
};
struct Message
{
    const char *id;
    int chapter;
    std::span<const Page> pages;
};
std::uint64_t fingerprint(std::string_view source);
int chapter(std::string_view source);
const Message *find(int chapter, std::string_view original);
bool matches(const Message *message, std::string_view original);
std::span<const Message> messages();
} // namespace TouchTutorial
