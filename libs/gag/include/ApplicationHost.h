// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <SDL3/SDL.h>
#include <functional>
#include <memory>
#include <vector>

namespace GAGCore { struct ViewportMetrics; struct InputCapabilities; }
namespace GAGCore::ApplicationHost
{
// Browser loops may request the next display callback instead of a timer.
// Native loops return millisecond delays. Zero remains a cooperative timer.
inline constexpr std::uint32_t AnimationFrameDelay = UINT32_MAX;

class Loop
{
  public:
	virtual ~Loop() = default;
	virtual bool frame(std::uint32_t tick, const std::vector<SDL_Event> &events) = 0;
	virtual std::uint32_t delay(std::uint32_t now) = 0;
    // Browser host calls painting separately on animation frames. Native hosts
    // retain the combined frame callback. Timer callbacks never block on paint.
    virtual void draw() {}
};
// Own the loop until it completes; destroy it before the completion callback.
// Native hosts return after completion; browser hosts return after scheduling.
void run(std::unique_ptr<Loop> loop, std::function<void()> complete);

// Compatibility wait for native-only modal loops. Browser-reachable code must
// use scheduled screens or cooperative tasks; the browser implementation rejects it.
void wait(std::uint32_t milliseconds);

// Initialize host compatibility state after a new OpenGL context is current.
void initializeOpenGLContext();

// Optional host acceleration for tightly packed BGRA pixels. Returning false
// leaves texture preparation to the shared integer CPU implementation.
bool prepareOpenGLMipmaps(unsigned texture, const void* pixels, int width, int height);

// Whether a bounded application heap (the browser's Wasm memory) has grown past
// four fifths of its maximum. Hosts whose memory the operating system manages
// return false.
bool heapNearlyFull();

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
	// Desktop only; browser/mobile imports deliberately expose no external path.
	std::string externalPath;
};
class FileSelection
{
  public:
	virtual ~FileSelection() = default;
	virtual FileSelectionState state() const = 0;
	virtual SelectedFile takeFile() = 0;
};
// True while a desktop OS file picker is open; game events cannot operate it.
#ifndef __EMSCRIPTEN__
bool nativeDialogOpen();
#else
inline bool nativeDialogOpen() { return false; }
#endif
bool canImportFiles();
std::unique_ptr<FileSelection> selectFile(const std::string &extension);

bool storageRestoreFailed();

// Staged game data. The browser host installs some data packages (game sprites,
// the CJK font, menu music) after the main menu is up; see buildsys/web_assets.py.
// Hosts that ship all data with the application answer true for every package
// and never report an installation.
#ifdef __EMSCRIPTEN__
// One in-flight group. Poll returns zero while pending, a negative value on
// failure, or the decoded byte count. Consume or discard the reply before reuse.
int startAssetVideo(const uint8_t *bytes, size_t length, unsigned width, unsigned height, unsigned count);
int assetVideoStatus();
void takeAssetVideo(uint8_t *pixels, size_t size);
#endif
bool assetPackageReady(const char *name);
// Start an optional package without blocking the game; readiness is polled.
void requestAssetPackage(const char *name);
// Packages installed since the previous call, oldest first.
std::vector<std::string> takeInstalledAssetPackages();
bool canExportFiles();
bool exportLocalFile(const std::string &path);
// File-backed export, without loading a potentially large recording into RAM.
bool exportFilePath(const std::string &path);
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

// How a web game portal that embeds the browser build restricts the game: the
// JSON text of the page's window.GLOB2_PORTAL, which the portal packager writes
// into index.html. Empty when there is none (our own site, itch, desktop,
// mobile). Native hosts read the GLOB2_PORTAL environment variable instead, so
// portal mode can be tried outside a browser. src/app/PortalPolicy.h parses it.
std::string portalDescription();

// Natural breaks in play, for web portal packages that show ads between
// matches (platforms/web/portal-core.js, docs/releases/web-portals.md#ad-breaks).
// The game reports every local match: Starting before its first tick, Ended
// when its results show, Left when the player leaves it for the menu. Within a
// match it also reports Paused when play stops for the in-game menu or a pause
// and Resumed when play continues; those mark gameplay only and never run an ad.
// The host decides whether a break runs an ad. matchBreak returns true when the
// host now holds the game for one; a caller about to start a match then waits
// until hostHeld() is false. Native hosts, and pages without a portal, return false.
enum class MatchBreak
{
	Starting,
	Ended,
	Left,
	Paused,
	Resumed,
};
bool matchBreak(MatchBreak point);
// Whether the host holds the game: an ad break, or a portal SDK's own pause.
// The application then runs no screen frames, so the simulation and input stop.
bool hostHeld();

// What an openUrl call opens. A portal that forbids external links still lets
// the online sign-in page open when it allows sign-in (src/app/PortalPolicy.h).
enum class UrlPurpose
{
	Link,
	SignIn,
};
// Off in portal builds that forbid links out of the game: openUrl then opens
// nothing for UrlPurpose::Link and returns false. On by default.
void setExternalLinksAllowed(bool allowed);
bool externalLinksAllowed();
// Off in portal mode, where the portal's page offers its own fullscreen button
// and some portals (CrazyGames) forbid the game's: F11 then does nothing and
// Settings offers no window mode. On by default.
void setFullscreenControlAllowed(bool allowed);
bool fullscreenControlAllowed();

// Opens an http(s) URL in the system browser (a new tab on the web). Returns
// false when the host cannot, or a popup blocker refused it; browsers allow it
// reliably only while handling a click.
bool openUrl(const std::string &url, UrlPurpose purpose = UrlPurpose::Link);
// Call while handling a click whose page URL only arrives later (from a server
// reply): the browser opens an empty tab now, while the click still allows it,
// and the next openUrl loads the page there. Does nothing on native hosts.
void prepareUrlWindow();
// Desktop hosts show a local folder in the system file manager; phones and the
// browser have no folder to show and return false from both.
bool canOpenFolder();
bool openFolder(const std::string &path);
// file:// URL of an absolute local path, percent-encoded per RFC 8089; Windows
// drive paths become file:///C:/...
std::string fileUrl(const std::string &path);
// Puts text on the system clipboard. In the browser this is the asynchronous
// Clipboard API (with a fallback for browsers without it), which needs the
// transient activation of a recent click; SDL's own clipboard does not reach
// the page there. Returns false when the host certainly could not copy.
bool copyText(const std::string &text);

// Read-only diagnostics; hosts decide whether and how to publish them.
void screenChanged(const char *name);
// Also told every screenChanged name, on every host (Report a Problem keeps
// them as breadcrumbs in its log tail). Empty removes it.
void setScreenObserver(std::function<void(const char *)> observer);
void importChanged(const char *state);
void simulationAdvanced(std::uint32_t tick);
void matchFrame(bool paused);
// Whether the torus overview replaced the flat map on the latest match frame.
void overviewDrawn(bool drawn, bool settled);
// What the in-game sidebar drew on the latest match frame: its panel (a view
// such as "construction" or "flags", or the selected entity's panel) and the
// selection mode ("none", "tool", ...). Lets tests wait for interface state
// rather than for exact pixels, which texture preparation may refine later.
void hudDrawn(const char *panel, const char *selection);
// Read-only presentation diagnostic for the active multiplayer room.
void roomReady(bool canStart);
// Whether the custom-game lobby can launch its current map.
void customGameReady(bool canStart);
// Embedded Studio feedback; native hosts ignore it. Call progress only while
// the simulation is parked; the browser host bounds and throttles publication.
// tick is the completed simulation step. ended means the match/session stopped;
// disabled/diagnostic describe the user's controller. won/lost describe its team.
// The browser bridge supplies the launch's run and source-revision identifiers.
void studioProgress(std::uint32_t tick, bool ended, bool disabled,
                    const std::string &diagnostic, bool won, bool lost);
void studioError(const std::string &message);
void studioGenerated(const std::string &report);
bool studioWatchRequested();
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
