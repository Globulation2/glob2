// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace Online
{
struct AuthorizedSkin
{
    std::string versionId, skinId, textureHash, manifestHash, accountId;
    std::int64_t expiresAt = 0;
    int team = -1;
    std::uint32_t buildingColor = 0;
    int swarmMesh = 0; // index into SWARM_MESHES
};
// Native OpenSSL and browser WebCrypto verify the same Ed25519 message. Polling
// never blocks on a browser promise; no appearance is exposed before success.
class SkinAuthorization
{
public:
    enum class State { Pending, Verified, Rejected };
    SkinAuthorization(const std::string &token, const std::string &jwks,
                      const std::string &issuer, const std::string &matchId,
                      int team, std::int64_t nowSeconds);
    ~SkinAuthorization();
    State state();
    const AuthorizedSkin *skin();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
