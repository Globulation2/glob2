// SPDX-License-Identifier: GPL-3.0-or-later
#include "Documents.h"
#include <atomic>
#include <map>
#include <mutex>

namespace MobileDocuments {
using namespace GAGCore::ApplicationHost;
namespace {
struct State {
    std::mutex mutex;
    FileSelectionState status = FileSelectionState::Pending;
    SelectedFile file;
};
std::mutex registryMutex;
std::map<Request, std::weak_ptr<State>> registry;
std::atomic<Request> nextRequest{0};
class Selection final : public FileSelection {
    Request request;
    std::shared_ptr<State> value;
public:
    Selection(Request request, std::shared_ptr<State> value) : request(request), value(std::move(value)) {}
    ~Selection() override {
        { std::lock_guard lock(registryMutex); registry.erase(request); }
        platformCancel(request);
    }
    FileSelectionState state() const override {
        std::lock_guard lock(value->mutex); return value->status;
    }
    SelectedFile takeFile() override {
        std::lock_guard lock(value->mutex);
        if (value->status != FileSelectionState::Selected) return {};
        value->status = FileSelectionState::Cancelled;
        return std::move(value->file);
    }
};
bool validName(const std::string& name) {
    return !name.empty() && name.size() <= 1024 && name != "." && name != ".." &&
        name.find_first_of("/\\") == std::string::npos && name.find('\0') == std::string::npos;
}
}
std::unique_ptr<FileSelection> select(const std::string& extension) {
    auto request = ++nextRequest;
    auto state = std::make_shared<State>();
    { std::lock_guard lock(registryMutex); registry.emplace(request, state); }
    auto selection = std::make_unique<Selection>(request, state);
    if (extension.empty() || extension.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789") != std::string::npos ||
        !platformOpen(request, extension)) complete(request, FileSelectionState::Failed);
    return selection;
}
void complete(Request request, FileSelectionState status, SelectedFile file) {
    std::shared_ptr<State> state;
    { std::lock_guard lock(registryMutex);
      auto found = registry.find(request);
      if (found == registry.end()) return;
      state = found->second.lock(); }
    if (!state || status == FileSelectionState::Pending) return;
    if (status == FileSelectionState::Selected && (!validName(file.name) || file.bytes.size() > maximumBytes))
        status = FileSelectionState::Failed;
    std::lock_guard lock(state->mutex);
    if (state->status != FileSelectionState::Pending) return;
    if (status == FileSelectionState::Selected) state->file = std::move(file);
    state->status = status;
}
bool exportFile(const std::string& name, const std::vector<unsigned char>& bytes, const std::string& error) {
    return validName(name) && bytes.size() <= maximumBytes && platformExport(name, bytes, error);
}
}
