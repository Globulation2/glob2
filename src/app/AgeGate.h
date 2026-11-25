// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Age-based restriction of social features (docs/multiplayer/identity.md#age-bands).
// The player's age band comes from the platform's age signal where there is one
// (Apple Declared Age Range on iOS/iPadOS/macOS 26+, Google Play Age Signals when
// that build option is on), otherwise from a one-time question. No birthdate is
// kept. Chat and other social features need ThirteenPlus; Unknown (never asked,
// "Not now", or a declined platform request) is treated like Under13.
//
// Single-player, campaigns, the editor, LAN and online matches stay available to
// everyone. What the band turns off: in-match chat (sending, showing, and the MCP
// game_chat tool), LAN and online room chat. The online platform enforces the
// same rules for its account (room chat, match tickets, sharing, names, photos).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace AgeGate
{
enum class Band
{
	Unknown,
	Under13,
	ThirteenPlus,
};

// Who set the local band: the player's answer, a platform signal, or the online
// account (set on another device, the website, or by a moderator).
enum class Source
{
	None,
	Self,
	Apple,
	GooglePlay,
	Account,
};

// The wire names the platform uses: "unknown", "under_13", "13_plus".
const char *bandName(Band band);
std::optional<Band> parseBand(std::string_view name);
// "self", "apple", "google_play", "account"; "" for None.
const char *sourceName(Source source);
std::optional<Source> parseSource(std::string_view name);

// Whether this band may use chat and other social features.
inline bool socialFeatures(Band band)
{
	return band == Band::ThirteenPlus;
}

// The local band, as Settings stores it.
struct Local
{
	Band band = Band::Unknown;
	Source source = Source::None;
};

// What a platform age request reported.
enum class Signal
{
	Unavailable, // no such API here, too old an OS, or an error: ask the question
	Declined,    // the player or a parent chose not to share: ask the question
	Under13,
	ThirteenPlus,
};

// Applies a platform signal to the local band. A signal that reports an age is
// authoritative (it replaces a self-declared answer) except that it never lifts
// Under13 set by the player or the account; Declined and Unavailable change
// nothing. Returns the new local band.
Local applySignal(Local current, Signal signal, Source platform);

// Applies the player's own answer to the question. A self-declared or account
// Under13 is kept: the player cannot answer again to lift it.
Local applyAnswer(Local current, Band answer);

// Whether the age question should be shown before LAN or online play.
inline bool shouldAsk(const Local &local)
{
	return local.band == Band::Unknown;
}

// How the client lines up its local band with the online account's band.
struct Reconcile
{
	// PUT /api/v1/accounts/me/age-band with this band and source ("self",
	// "apple" or "google_play").
	std::optional<Band> push;
	Source pushSource = Source::None;
	// Store this band locally (Source::Account).
	std::optional<Band> adopt;
};
// accountBand is the account's ageBand. The account wins when a moderator set
// it (a parent's request) or when the device's band came from the account (it
// changed there since); otherwise under 13 on either side wins over a
// self-declared 13 plus, and the device fills in an account that knows nothing.
Reconcile reconcile(const Local &local, Band accountBand, bool accountByModerator = false);

// Whether the current match allows chat: the local band has social features and
// the online platform has not turned chat off for this match (a player in it is
// under 13 or undeclared). Single-player and LAN pass matchChatOn = true.
inline bool chatAllowed(const Local &local, bool matchChatOn)
{
	return socialFeatures(local.band) && matchChatOn;
}

// Process-wide state the game shares across screens.
// The local band store (Settings); installed by the Application. Without a
// store (tools, harnesses) the band is Unknown and nothing is saved.
struct Store
{
	std::function<Local()> load;
	std::function<bool(const Local &)> save;
};
void setStore(Store store);
Store installedStore();
Local local();
bool store(const Local &value);

// Online matches whose assignment says chat is off set this for their lifetime:
// beginMatchChat returns a token, and endMatchChat turns chat back on only if no
// later match has begun since (a rematch's new match is created before the old
// one is destroyed). setMatchChatOn sets it outright (tests).
std::uint64_t beginMatchChat(bool on);
void endMatchChat(std::uint64_t token);
void setMatchChatOn(bool on);
bool matchChatOn();
// chatAllowed(local(), matchChatOn()).
bool chatAllowedNow();

// The platform age request of this build. Calls done(signal) once, possibly
// later from the game thread's poll (pollPlatformSignal). Builds without a
// platform signal report Unavailable at once.
void requestPlatformSignal(std::function<void(Signal)> done);
// Delivers a pending platform answer on the calling (game) thread.
void pollPlatformSignal();
// The Source a platform signal of this build reports as (Apple, GooglePlay or None).
Source platformSource();
}
