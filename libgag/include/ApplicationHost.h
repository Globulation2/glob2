// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <SDL.h>
#include <functional>
#include <memory>
#include <vector>

namespace GAGCore::ApplicationHost
{
class Loop
{
public:
    virtual ~Loop() = default;
    virtual bool frame(std::uint32_t tick, const std::vector<SDL_Event>& events) = 0;
    virtual std::uint32_t delay(std::uint32_t now) = 0;
};
// Own the loop until it completes; destroy it before the completion callback.
// Native hosts return after completion; browser hosts return after scheduling.
void run(std::unique_ptr<Loop> loop, std::function<void()> complete);

// Compatibility wait for native-only modal loops. Browser-reachable code must
// use scheduled screens or cooperative tasks; the browser implementation rejects it.
void wait(std::uint32_t milliseconds);

// Consume the newest host viewport request at an application frame boundary.
bool takeViewportSize(int& width, int& height);
// Visibility edges are retained even when no frame ran while hidden.
bool takeVisibilityChange(bool& hidden);

enum class FileSelectionState { Pending, Selected, Cancelled, Failed };
struct SelectedFile { std::string name; std::vector<unsigned char> bytes; };
class FileSelection {
public:
    virtual ~FileSelection() = default;
    virtual FileSelectionState state() const = 0;
    virtual SelectedFile takeFile() = 0;
};
bool canImportFiles();
std::unique_ptr<FileSelection> selectFile(const std::string& extension);

bool storageRestoreFailed();
bool canExportFiles();
bool exportLocalFile(const std::string& path);
bool exportFile(const std::string& name, const std::vector<unsigned char>& bytes);

// Persistence completion is owned by the caller; releasing it is safe while pending.
enum class PersistenceState { Pending, Succeeded, Failed };
class Persistence {
public:
    virtual ~Persistence() = default;
    virtual PersistenceState state() const = 0;
};
std::unique_ptr<Persistence> persistStorage();

// Read-only diagnostics; hosts decide whether and how to publish them.
void screenChanged(const char* name);
void importChanged(const char* state);
void simulationAdvanced(std::uint32_t tick);
void matchFrame(bool paused);
// Whether the torus overview replaced the flat map on the latest match frame.
void overviewDrawn(bool drawn);
// Read-only presentation diagnostic for the active multiplayer room.
void roomReady(bool canStart);
void exited(int result);
}
