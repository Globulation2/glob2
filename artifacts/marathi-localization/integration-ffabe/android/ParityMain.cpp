// Ignored validation entry only: production MatchVerifier, no simulation replacement.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include "CommandLine.h"
#include "Headless.h"
#include <cstdio>
#include <exception>
int main(int argc, char **argv)
{
    try {
        const auto request = Cli::parse(argc, argv);
        if (request.command != "match verify") {
            std::fprintf(stderr, "This validation fixture accepts only match verify\n");
            return 2;
        }
        return runVerifyMatch(request);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Android parity fixture: %s\n", error.what());
        return 3;
    }
}
