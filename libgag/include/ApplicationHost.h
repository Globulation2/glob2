// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <SDL.h>
#include <functional>
#include <memory>
#include <vector>

namespace GAGCore { struct ViewportMetrics; struct InputCapabilities; }
namespace GAGCore::ApplicationHost
{
class Loop
{
  public:
	virtual ~Loop() = default;
	virtual bool frame(std::uint32_t tick, const std::vector<SDL_Event> &events) = 0;
	virtual std::uint32_t delay(std::uint32_t now) = 0;
};
// Own the loop until it completes; destroy it before the completion callback.
// Native hosts return after completion; browser hosts return after scheduling.
void run(std::unique_ptr<Loop> loop, std::function<void()> complete);

// Compatibility wait for native-only modal loops. Browser-reachable code must
// use scheduled screens or cooperative tasks; the browser implementation rejects it.
void wait(std::uint32_t milliseconds);

// Consume the newest host viewport request at an application frame boundary.
bool takeViewportSize(int &width, int &height);
// Read current host points, safe areas, keyboard occlusion and input capabilities.
bool presentationMetrics(ViewportMetrics& metrics, InputCapabilities& input);
// Visibility edges are retained even when no frame ran while hidden.
bool takeVisibilityChange(bool &hidden);

enum class FileSelectionState
{
	Pending,
	Selected,
	Cancelled,
	Failed
};
struct SelectedFile
{
	std::string name;
	std::vector<unsigned char> bytes;
};
class FileSelection
{
  public:
	virtual ~FileSelection() = default;
	virtual FileSelectionState state() const = 0;
	virtual SelectedFile takeFile() = 0;
};
bool canImportFiles();
std::unique_ptr<FileSelection> selectFile(const std::string &extension);

bool storageRestoreFailed();

// Staged game data. The browser host installs some data packages (game sprites,
// the CJK font, menu music) after the main menu is up; see scons/web_assets.py.
// Hosts that ship all data with the application answer true for every package
// and never report an installation.
bool assetPackageReady(const char *name);
// Packages installed since the previous call, oldest first.
std::vector<std::string> takeInstalledAssetPackages();
bool canExportFiles();
bool exportLocalFile(const std::string &path);
bool exportFile(const std::string &name, const std::vector<unsigned char> &bytes);

// Persistence completion is owned by the caller; releasing it is safe while pending.
enum class PersistenceState
{
	Pending,
	Succeeded,
	Failed
};
class Persistence
{
  public:
	virtual ~Persistence() = default;
	virtual PersistenceState state() const = 0;
};
std::unique_ptr<Persistence> persistStorage();

// Opens an http(s) URL in the system browser (a new tab on the web). Returns
// false when the host cannot, or a popup blocker refused it; browsers allow it
// reliably only while handling a click.
bool openUrl(const std::string &url);

// Read-only diagnostics; hosts decide whether and how to publish them.
void screenChanged(const char *name);
void importChanged(const char *state);
void simulationAdvanced(std::uint32_t tick);
void matchFrame(bool paused);
// Whether the torus overview replaced the flat map on the latest match frame.
void overviewDrawn(bool drawn);
// Read-only presentation diagnostic for the active multiplayer room.
void roomReady(bool canStart);
// Whether the custom-game lobby can launch its current map.
void customGameReady(bool canStart);
// Read-only presentation diagnostic: the interactive controls of one element
// host (a screen or dialog) after layout, as JSON keyed by control key with
// logical-pixel bounds, or null when the host goes away. Tests drive the real
// controls through their keys instead of hard-coded coordinates.
void controlsChanged(const void *owner, const char *json);
// Whether anything observes controlsChanged; hosts skip building the JSON
// otherwise.
bool controlsObserved();
void exited(int result);
} // namespace GAGCore::ApplicationHost
