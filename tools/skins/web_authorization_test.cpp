// SPDX-License-Identifier: GPL-3.0-or-later
// Browser-only integration test of the production WebCrypto authorization path.
#include "SkinAuthorization.h"
#include <nlohmann/json.hpp>
#include "../../browser/SkinSignature.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
struct Case
{
    std::string name, token;
    bool accepted;
};
nlohmann::json fixture;
std::vector<Case> cases;
std::unique_ptr<Online::SkinAuthorization> pending;
unsigned checked = 0, failed = 0;
double cancellationDeadline = 0;

// Poll from the browser event loop, just as the production host does. This
// keeps WebCrypto asynchronous without requiring Asyncify or changing the
// game's WebAssembly exception configuration for the test executable.
void step()
{
    if (checked < cases.size())
    {
        const auto &test = cases[checked];
        if (!pending)
            pending = std::make_unique<Online::SkinAuthorization>(test.token,
                fixture["jwks"].dump(), "https://example.test",
                fixture["claims"]["matchId"], 2, 1700000000);
        if (pending->state() == Online::SkinAuthorization::State::Pending)
            return;
        if ((pending->skin() != nullptr) != test.accepted)
        {
            std::cerr << "Failed " << test.name << '\n';
            ++failed;
        }
        pending.reset();
        ++checked;
        return;
    }
    if (!cancellationDeadline)
    {
        // A destroyed pending verifier must not resurrect its JS entry.
        Online::SkinAuthorization cancelled(fixture["valid"], fixture["jwks"].dump(),
            "https://example.test", fixture["claims"]["matchId"], 2, 1700000000);
        cancellationDeadline = emscripten_get_now() + 50;
        return;
    }
    if (emscripten_get_now() < cancellationDeadline)
        return;
    const auto outstanding = EM_ASM_INT({return Module.glob2SkinSignatures.entries.size;});
    if (outstanding) ++failed;
    EM_ASM({window.authorizationResult = ({checked: $0, failed: $1, outstanding: $2});},
        checked, failed, outstanding);
    emscripten_cancel_main_loop();
}
}

int main()
{
    std::ifstream input("/fixture.json");
    fixture = nlohmann::json::parse(input);
    cases.push_back({"valid signature", fixture["valid"], true});
    for (const auto &entry : fixture["swarm"].items())
        cases.push_back({entry.key(), entry.value(), true});
    for (const auto &entry : fixture["invalid"].items())
        cases.push_back({entry.key(), entry.value(), false});
    emscripten_set_main_loop(step, 0, 1);
}
