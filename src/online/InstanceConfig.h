// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>

// Which platform instance the client uses, and what it remembers per
// instance. Stored as online/instances.json in the user directory (browser:
// the site's persistent storage), never in preferences.txt, so credentials
// stay out of the settings file players share when reporting bugs.
namespace Online
{
class OnlineStorage;

// The official instance. Self-hosted instances are chosen in settings or by
// following an invite link; they are equally first-class. The origin is one
// build-time setting (scons/official_instance.py, or official_instance=...),
// shared with the Android App Link host and the iOS associated domain.
#ifndef GLOB2_OFFICIAL_INSTANCE_ORIGIN
#error "GLOB2_OFFICIAL_INSTANCE_ORIGIN is defined by the build (scons/official_instance.py)"
#endif
inline constexpr const char *OFFICIAL_INSTANCE_ORIGIN = GLOB2_OFFICIAL_INSTANCE_ORIGIN;
// Whether the trust prompt's "remember this instance" box starts ticked.
inline constexpr bool REMEMBER_TRUST_BY_DEFAULT = true;

// Canonical origin: lowercase scheme and host, no default port, no path.
// Accepts https:// and, for loopback hosts only (development), http://. A
// trailing slash is allowed. Empty for anything else, including user info,
// paths, queries and fragments.
std::optional<std::string> normalizeOrigin(const std::string &origin);
// wss://host[:port]/realtime (ws:// for a loopback http:// origin).
std::string realtimeUrl(const std::string &origin);
// origin + path, path starting with '/'.
std::string apiUrl(const std::string &origin, const std::string &path);

// Remembered per instance origin. Credentials of one instance are never sent
// to another.
struct InstanceRecord
{
	// Guest device credential (returned once by POST /api/v1/auth/guest).
	// Keeps signing in the same account, also after it is upgraded, so it is
	// kept across sign-outs.
	std::string deviceCredential;
	// Current refresh token; rotated on every refresh.
	std::string refreshToken;
	// The player trusted this instance and asked to remember it.
	bool trusted = false;
	// Sign in automatically on connect (guest or refresh token). Cleared by
	// signing out, set again by any explicit sign-in.
	bool autoSignIn = true;
	// Display name of the last account, for showing before connecting.
	std::string lastDisplayName;
};

class InstanceConfig
{
  public:
	static constexpr const char *FILE_NAME = "online/instances.json";
	static constexpr int FORMAT_VERSION = 1;

	explicit InstanceConfig(OnlineStorage &storage);

	// Reads the stored configuration; a missing or unreadable file leaves the
	// defaults (official instance, nothing remembered). Returns whether a file
	// was read.
	bool load();
	// Writes the configuration and asks the host to persist it.
	bool save();

	// The instance chosen in settings (the official one by default).
	const std::string &selectedOrigin() const
	{
		return selected;
	}
	// Returns false (and changes nothing) for an invalid origin.
	bool selectInstance(const std::string &origin);

	// The record of an instance, created when missing. origin must be
	// normalized.
	InstanceRecord &record(const std::string &origin);
	const InstanceRecord *find(const std::string &origin) const;
	// Drops everything remembered about an instance (credentials included).
	void forget(const std::string &origin);
	const std::map<std::string, InstanceRecord> &instances() const
	{
		return records;
	}

	// Trust for invite links: the official and the selected instance are
	// always trusted; any other needs the player's confirmation, remembered
	// in the file when `remember` is set, otherwise until the client exits.
	bool isTrusted(const std::string &origin) const;
	void trust(const std::string &origin, bool remember = REMEMBER_TRUST_BY_DEFAULT);
	void untrust(const std::string &origin);

	std::string toJson() const;
	// Replaces the configuration from JSON; false (configuration unchanged)
	// when it is malformed. Unknown fields are ignored and invalid origins are
	// skipped, so newer files still load.
	bool fromJson(const std::string &text);

  private:
	OnlineStorage &storage;
	std::string selected;
	std::map<std::string, InstanceRecord> records;
	std::set<std::string> sessionTrust;
};
} // namespace Online
