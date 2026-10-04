// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GAGCore
{
class DrawableSurface;
}
namespace Online
{
class PlatformClient;
class PlatformScope;
}

// Shared pieces of the online screens (quick match, profile, maps): the wide
// paper panel with a header and a footer note, server preview images, small
// data painters (sparkline, countdown ring) and text helpers.
namespace Glob2UI
{
struct OnlinePanel
{
	std::string title, subtitle;
	// Right of the title on wide layouts (tabs, account chip, web link).
	Element headerRight;
	Element body;
	// Muted line at the left of the desktop footer.
	std::string note;
	// Footer actions; the last carries Escape. On phones they form the thumb block.
	std::vector<MenuAction> actions;
	// Phones: replaces the action row as the thumb block when set.
	Element thumbBlock;
};
// Desktop: a paper card filling the window inside a margin, title and header
// at the top, body, then a divider and the note beside the actions. Touch: a
// full-height sheet whose actions (or thumb block) stay at the bottom.
Element onlinePanel(OnlinePanel spec, const Presentation &p);

// A square picture area: the surface scaled to fit, or a placeholder.
Element previewPicture(GAGCore::DrawableSurface *surface, int pixels);
// Rating trend: a polyline over the values, dashed when provisional.
Element sparkline(std::vector<double> values, bool provisional, Size size);
// Countdown ring with the remaining whole seconds in the middle.
Element countdownRing(std::int64_t remainingMs, std::int64_t totalMs, int pixels);
// A small rounded label such as "provisional 5/10" or "Rejected".
Element badge(const std::string &text, GAGCore::Color color);
// What an empty list says: an icon, why it is empty and what to do instead
// (actions, usually one or two buttons; may be empty).
Element emptyState(IconRef glyph, const std::string &text, std::vector<Element> actions, const Presentation &p);

// Decoded PNG previews from the instance, by URL. Downloads go through the
// platform client (authenticated, so private maps work); failures are kept
// so a broken preview is not fetched every frame.
class PreviewImages
{
  public:
	PreviewImages();
	~PreviewImages();
	PreviewImages(const PreviewImages &) = delete;
	PreviewImages &operator=(const PreviewImages &) = delete;
	// The decoded image, starting the download on first request. changed is
	// called when it arrives.
	GAGCore::DrawableSurface *get(Online::PlatformClient *client, const std::string &url,
								  std::function<void()> changed);
	// PNG bytes from elsewhere (tests, a local file).
	bool insert(const std::string &url, const std::string &png);
	bool insertFile(const std::string &url, const std::string &path);

  private:
	struct Entry
	{
		std::unique_ptr<GAGCore::DrawableSurface> surface;
		bool pending = false, failed = false;
	};
	std::map<std::string, Entry> entries;
	// Downloads in flight, cancelled with the cache (its screen).
	std::unique_ptr<Online::PlatformScope> scope;
	Online::PlatformScope &scopeFor(Online::PlatformClient &client);
};

// "0:42", "12:05", "1:02:03": running timers and countdowns.
std::string clockText(std::int64_t seconds);
// An estimate in words: "under a minute", "about 2 min".
std::string aboutText(std::int64_t seconds);
// How long a match lasted: "45 s", "12 min".
std::string durationText(std::int64_t seconds);
// A queue as players know it: its configured name, else a readable form of the id
// ("casual-1v1" -> "Casual 1v1"); never the raw id.
std::string queueDisplayName(const std::string &id, const std::string &name);
// A relay region as players read it ("ca-central" -> "Canada, central",
// "eu-west" -> "Europe, west"); unknown ids become readable words.
std::string regionDisplayName(const std::string &id);
// The certificate fingerprint in a LAN pairing link ("wss://…#sha256=ab12cd34…") as a
// short code people can compare ("AB12 CD34"); empty when the link has none.
std::string pairingCode(const std::string &pairing);
// A translated title for a generator id ("even-ground" -> "Even Ground").
std::string generatorTitle(const std::string &generatorId);
// The display name of an AI id ("cortex" -> "Cortex").
std::string aiTitle(const std::string &aiId);
// The custom-game lobby's profile line for an AI id; empty when unknown.
std::string aiProfile(const std::string &aiId);
// "+15", "−11" (true minus sign), "0".
std::string signedText(double value);
// "1.2k", "860".
std::string compactCount(std::int64_t value);
// Relative age from a Unix-epoch millisecond time: "today 14:02", "yesterday", "Mon", "3 weeks ago".
std::string ageText(std::int64_t at, std::int64_t now);
// Instance origin without the scheme, for status lines.
std::string originHost(const std::string &origin);
// Milliseconds since the Unix epoch.
std::int64_t wallClockMs();
// The shared platform client, started on the selected instance if it was not.
Online::PlatformClient &onlineClient();
// Opens a web page of the instance in the system browser.
bool openInstancePage(const std::string &origin, const std::string &path);
} // namespace Glob2UI
