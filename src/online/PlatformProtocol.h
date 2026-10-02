// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// JSON shapes of the platform's realtime socket and REST API, as defined by
// platform/packages/protocol (realtime.ts, resources.ts, common.ts). Only the
// fields the client acts on are typed here; everything else stays available as
// raw JSON so screens can read new fields without a client change.
namespace Online
{
using Json = nlohmann::json;

// REALTIME_PROTOCOL_VERSION in platform/packages/protocol/src/realtime.ts.
inline constexpr int REALTIME_PROTOCOL_VERSION = 1;
// The server closes sockets that send larger frames (MAX_FRAME_BYTES).
inline constexpr std::size_t REALTIME_FRAME_LIMIT = 64 * 1024;

// ErrorBody. Codes from the server are ErrorCode values (bad_request,
// unauthenticated, forbidden, not_found, conflict, rate_limited,
// update_required, ...). Failures detected by the client use their own codes:
// "timeout" (no response in time), "disconnected" (the socket closed while
// the request was in flight), "cancelled" (stop() or cancelRequest()),
// "network" (an HTTP request got no response) and "http_<status>" (an HTTP
// error without an ErrorBody).
struct ApiError
{
	std::string code;
	std::string message;
	Json details;
};

// One server -> client frame.
struct ServerMessage
{
	enum class Kind
	{
		Response,
		Event,
		Invalid
	};
	Kind kind = Kind::Invalid;
	// Response
	std::string id;
	bool ok = false;
	Json result;
	ApiError error;
	// Event
	std::string event;
	Json data;
	// Invalid: why the frame was rejected.
	std::string invalid;
};

// {"type":"request","id":id,"method":method,"params":params}. params must be
// an object (null becomes {}).
std::string encodeRequest(const std::string &id, const std::string &method, const Json &params);
ServerMessage decodeServerMessage(std::string_view text);
ApiError errorFromJson(const Json &body, const std::string &fallbackCode);

// SimVersion: the build's deterministic simulation identity.
struct SimVersion
{
	int versionMinor = 0;
	int netProtocol = 0;
	std::string dataHash; // 64 lowercase hex digits
	Json toJson() const;
	// VERSION_MINOR and NET_PROTOCOL_VERSION of this build. The data hash is
	// not computed by the engine yet (see docs/multiplayer/client.md); until it
	// is, this build reports 64 zeros and the platform answers
	// simSupported=false, which still allows sign-in.
	static SimVersion local();
};

// ClientPlatform: desktop, android, ios or browser, from the build target.
const char *clientPlatform();

// AuthTokens.
struct AuthTokens
{
	std::string accessToken;
	std::string accessTokenExpiresAt;
	std::string refreshToken;
	std::string refreshTokenExpiresAt;
	static std::optional<AuthTokens> fromJson(const Json &json);
};

// The fields of SelfAccount the client shows; raw keeps the rest.
struct Account
{
	std::string id;
	std::string displayName;
	std::string kind; // guest | registered
	std::string role; // user | moderator | admin
	Json raw;
	static std::optional<Account> fromJson(const Json &json);
};

// RFC 3339 timestamp (as the platform writes them) to milliseconds since the
// Unix epoch, UTC. Empty for malformed input.
std::optional<std::int64_t> parseTimestamp(std::string_view text);

// Decodes (without verifying) the claims of a JWT. Clients only read iat/exp
// to schedule refreshes; the platform verifies every token it receives.
std::optional<Json> jwtClaims(std::string_view token);
// exp - iat of an access token in milliseconds, or empty when the claims are
// missing. Independent of the local clock, unlike accessTokenExpiresAt.
std::optional<std::int64_t> tokenLifetimeMs(std::string_view token);

// Delay after receiving an access token of this lifetime before refreshing
// it: early enough to absorb clock and network delays (at least a minute or
// a fifth of the lifetime before expiry), never sooner than one second.
std::int64_t refreshDelayMs(std::int64_t lifetimeMs);

// Exponential reconnect backoff with jitter. Each delay is
// min(maxMs, initialMs * factor^attempt), reduced by up to `jitter` of itself
// at random so many clients dropped together do not reconnect together.
class Backoff
{
  public:
	Backoff(std::int64_t initialMs = 1000, std::int64_t maxMs = 60000, double factor = 2.0,
			double jitter = 0.5);
	// random is uniform in [0, 1).
	std::int64_t next(double random);
	void reset()
	{
		attempt = 0;
	}
	int attempts() const
	{
		return attempt;
	}

  private:
	std::int64_t initialMs, maxMs;
	double factor, jitter;
	int attempt = 0;
};
} // namespace Online
