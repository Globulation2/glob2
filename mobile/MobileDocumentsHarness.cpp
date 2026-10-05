// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <vector>
#include <string>
#include "../mobile/Documents.h"
#include <thread>

using namespace GAGCore::ApplicationHost;
namespace MobileDocuments {
bool platformExportPath(const std::string&, const std::string&) { return false; }
Request opened = 0, cancelled = 0;
bool accepting = true;
bool platformOpen(Request request, const std::string&) { opened = request; return accepting; }
void platformCancel(Request request) { cancelled = request; }
bool platformExport(const std::string&, const std::vector<unsigned char>&, const std::string&) { return accepting; }
}
TEST_SUITE("MobileDocuments")
{
TEST_CASE("selection requests; terminal callbacks; validation and export")
{
    using namespace MobileDocuments;
    auto first = select("map");
    const auto old = opened;
    REQUIRE(first->state() == FileSelectionState::Pending);
    first.reset();
    REQUIRE(cancelled == old);
    auto second = select("game");
    const auto current = opened;
    std::thread late([&] { complete(old, FileSelectionState::Selected, {"old.map", {1,2}}); });
    late.join();
    REQUIRE(second->state() == FileSelectionState::Pending);
    std::thread callback([&] { complete(current, FileSelectionState::Selected, {"é😀.game", {3,4,5}}); });
    callback.join();
    complete(current, FileSelectionState::Failed); // First terminal callback wins.
    REQUIRE(second->state() == FileSelectionState::Selected);
    auto file = second->takeFile();
    REQUIRE((file.name == "é😀.game" && file.bytes == std::vector<unsigned char>({3,4,5})));
    REQUIRE(second->takeFile().bytes.empty());
    REQUIRE(second->state() == FileSelectionState::Cancelled);
    auto invalid = select("map");
    complete(opened, FileSelectionState::Selected, {"../bad.map", {1}});
    REQUIRE(invalid->state() == FileSelectionState::Failed);
    auto oversized = select("map");
    complete(opened, FileSelectionState::Selected, {"large.map", std::vector<unsigned char>(maximumBytes + 1)});
    REQUIRE(oversized->state() == FileSelectionState::Failed);
    auto cancellation = select("map");
    complete(opened, FileSelectionState::Cancelled);
    REQUIRE(cancellation->state() == FileSelectionState::Cancelled);
    accepting = false;
    auto refused = select("map");
    REQUIRE(refused->state() == FileSelectionState::Failed);
    REQUIRE(!exportFile("safe.map", {1}, "failed"));
    accepting = true;
    REQUIRE(!exportFile("../unsafe.map", {1}, "failed"));
    REQUIRE(exportFile("safe.map", {1}, "failed"));
    REQUIRE(select("../map")->state() == FileSelectionState::Failed);
}
}
