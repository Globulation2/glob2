// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ApplicationHost.h>
#include <CooperativeSlice.h>
#include <optional>

// A menu-owned import. No active game may share its simulation RNG while the
// validation job runs. Cancellation destroys the job and restores that RNG.
class FileImport {
public:
    enum class State { Validating, Persisting, Succeeded, Failed };
    using Persist = std::function<std::unique_ptr<GAGCore::ApplicationHost::Persistence>()>;
    FileImport(GAGCore::ApplicationHost::SelectedFile file, std::string extension,
               Persist persist = GAGCore::ApplicationHost::persistStorage,
               GAGCore::CooperativeSlice slice = GAGCore::CooperativeSlice());
    ~FileImport();
    void advance();
    void retryPersistence();
    bool exportFile() const;
    State state() const { return current; }
    bool canRetry() const { return current == State::Failed && !destination.empty(); }
    const std::string& path() const { return destination; }
private:
    GAGCore::CooperativeTask validate();
    GAGCore::ApplicationHost::SelectedFile file;
    std::string extension, destination;
    Persist persist;
    State current = State::Validating;
    GAGCore::CooperativeSlice slice;
    std::optional<GAGCore::CooperativeTask> task;
    std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
};
