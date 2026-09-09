// SPDX-License-Identifier: GPL-3.0-or-later
#include "../mobile/Documents.h"
#include <cassert>
#include <thread>

using namespace GAGCore::ApplicationHost;
namespace MobileDocuments {
Request opened = 0, cancelled = 0;
bool accepting = true;
bool platformOpen(Request request, const std::string&) { opened = request; return accepting; }
void platformCancel(Request request) { cancelled = request; }
bool platformExport(const std::string&, const std::vector<unsigned char>&, const std::string&) { return accepting; }
}
int main() {
    using namespace MobileDocuments;
    auto first = select("map");
    const auto old = opened;
    assert(first->state() == FileSelectionState::Pending);
    first.reset();
    assert(cancelled == old);
    auto second = select("game");
    const auto current = opened;
    std::thread late([&] { complete(old, FileSelectionState::Selected, {"old.map", {1,2}}); });
    late.join();
    assert(second->state() == FileSelectionState::Pending);
    std::thread callback([&] { complete(current, FileSelectionState::Selected, {"é😀.game", {3,4,5}}); });
    callback.join();
    complete(current, FileSelectionState::Failed); // First terminal callback wins.
    assert(second->state() == FileSelectionState::Selected);
    auto file = second->takeFile();
    assert(file.name == "é😀.game" && file.bytes == std::vector<unsigned char>({3,4,5}));
    assert(second->takeFile().bytes.empty());
    assert(second->state() == FileSelectionState::Cancelled);
    auto invalid = select("map");
    complete(opened, FileSelectionState::Selected, {"../bad.map", {1}});
    assert(invalid->state() == FileSelectionState::Failed);
    auto oversized = select("map");
    complete(opened, FileSelectionState::Selected, {"large.map", std::vector<unsigned char>(maximumBytes + 1)});
    assert(oversized->state() == FileSelectionState::Failed);
    auto cancellation = select("map");
    complete(opened, FileSelectionState::Cancelled);
    assert(cancellation->state() == FileSelectionState::Cancelled);
    accepting = false;
    auto refused = select("map");
    assert(refused->state() == FileSelectionState::Failed);
    assert(!exportFile("safe.map", {1}, "failed"));
    accepting = true;
    assert(!exportFile("../unsafe.map", {1}, "failed"));
    assert(exportFile("safe.map", {1}, "failed"));
    assert(select("../map")->state() == FileSelectionState::Failed);
}
