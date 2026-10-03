// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Match ticket verification for the relay (docs/multiplayer/relay.md). Tickets are
// EdDSA (Ed25519) JWTs the platform signs; the contract lives in
// platform/packages/protocol/src/ticket.ts and src/node/jwt.ts. The checks run in the
// same order as the platform's own verifier, so both report the same reason for the
// shared fixtures in test/fixtures/relay-tickets/.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Relay
{
	/// Why a ticket was refused. The names match JwtError.reason in the protocol
	/// package; Claims is the relay's own check of the claims' shape.
	enum class TicketError
	{
		None,
		Malformed,
		Algorithm,
		Type,
		Key,
		Signature,
		Expired,
		Audience,
		NotYetValid,
		Claims,
	};
	const char* ticketErrorName(TicketError error);

	constexpr const char* MATCH_TICKET_TYPE = "glob2-match+jwt";
	constexpr const char* MATCH_TICKET_AUDIENCE = "glob2-relay";
	constexpr std::int64_t DEFAULT_LEEWAY_SECONDS = 30;
	/// MatchTicketClaims.seat and humanSeats use SeatIndex (0..MAX_TEAMS-1).
	constexpr int MAX_TICKET_SEATS = 12;

	struct SimVersion
	{
		int versionMinor = 0;
		int netProtocol = 0;
		std::string dataHash;
		/// simVersionKey() in the protocol package: "<minor>-<net>-<dataHash>".
		std::string key() const;
		bool operator==(const SimVersion& o) const
		{
			return versionMinor == o.versionMinor && netProtocol == o.netProtocol && dataHash == o.dataHash;
		}
	};

	struct TicketClaims
	{
		std::string issuer;
		std::string subject;
		std::string ticketId;
		std::string matchId;
		std::string accountId;
		std::string relayUrl;
		int seat = -1;
		std::uint32_t humanSeatMask = 0;
		SimVersion simVersion;
		std::int64_t issuedAt = 0;
		std::int64_t expiresAt = 0;
		std::optional<std::int64_t> notBefore;
	};

	struct TicketResult
	{
		TicketError error = TicketError::None;
		std::string detail;
		std::string kid; ///< set when the header named a key
		TicketClaims claims;
		bool ok() const { return error == TicketError::None; }
	};

	/// An immutable set of Ed25519 verification keys, by kid.
	class KeySet
	{
	public:
		KeySet();
		~KeySet();
		KeySet(const KeySet&) = delete;
		KeySet& operator=(const KeySet&) = delete;

		/// Parses a JWKS document ({"keys": [...]}). Keys that are not OKP/Ed25519
		/// signing keys are skipped. Throws std::runtime_error if the document is not a
		/// JWKS or an Ed25519 key is malformed.
		static std::shared_ptr<const KeySet> parse(const std::string& json);

		bool has(const std::string& kid) const { return keys.count(kid) != 0; }
		std::size_t size() const { return keys.size(); }
		std::vector<std::string> kids() const;
		/// Verifies an Ed25519 signature with the named key.
		bool verify(const std::string& kid, const std::string& message, const std::vector<std::uint8_t>& signature) const;

	private:
		struct Key;
		std::map<std::string, std::unique_ptr<Key>> keys;
	};

	struct VerifyOptions
	{
		std::int64_t nowSeconds = 0;
		std::int64_t leewaySeconds = DEFAULT_LEEWAY_SECONDS;
		/// When not empty, the iss claim must equal it.
		std::string issuer;
	};

	/// Verifies signature, algorithm, type, audience, times and claim shape.
	TicketResult verifyTicket(const std::string& token, const KeySet& keys, const VerifyOptions& options);

	/// Reads the kid from a token's header without verifying anything; empty if the
	/// header cannot be decoded.
	std::string ticketKeyId(const std::string& token);

	/// RFC 4648 base64url without padding. decode returns false on any invalid input.
	bool base64UrlDecode(const std::string& in, std::vector<std::uint8_t>& out);
	std::string base64UrlEncode(const std::uint8_t* data, std::size_t size);

	/// Whether a ticket for an existing match agrees with the match's identity.
	enum class MatchAgreement
	{
		Agrees,
		SimVersionDiffers,
		HumanSeatsDiffer,
	};
	MatchAgreement checkAgreement(const SimVersion& matchVersion, std::uint32_t matchHumanSeats, const TicketClaims& claims);
}
