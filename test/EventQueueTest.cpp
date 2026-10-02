// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <EventQueue.h>
#include <SDL3/SDL.h>

TEST_SUITE("EventQueue")
{
TEST_CASE("deferred UTF-8 and composition survive source reuse and queue growth")
{
    GAGCore::EventQueue queue;
    std::string input = "\xC3\xA9 \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    const auto expected = input;
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = input.c_str();
    queue.push_back(event);
    input.assign(400, 'x');
    event.type = SDL_EVENT_TEXT_EDITING;
    event.edit.text = input.c_str();
    event.edit.start = 2;
    event.edit.length = 3;
    queue.push_back(event);
    for (int i = 0; i < 1024; ++i) {
        input = std::to_string(i);
        event.edit.text = input.c_str();
        queue.push_back(event);
    }
    CHECK(std::string(queue.events()[0].text.text) == expected);
    CHECK(std::string(queue.events()[1].edit.text) == std::string(400, 'x'));
    CHECK(queue.events()[1].edit.start == 2);
    CHECK(queue.events()[1].edit.length == 3);
    queue.clear();
    CHECK(queue.events().empty());
    event.edit.text = nullptr;
    queue.push_back(event);
    CHECK(queue.events()[0].edit.text == nullptr);
}
#ifndef SDL_PLATFORM_ANDROID
TEST_CASE("queued SDL text survives the next OS event pump")
{
    REQUIRE(SDL_InitSubSystem(SDL_INIT_EVENTS));
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = "deferred \xC3\xA9";
    REQUIRE(SDL_PushEvent(&event));
    REQUIRE(SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_TEXT_INPUT, SDL_EVENT_TEXT_INPUT) == 1);
    GAGCore::EventQueue queue;
    queue.push_back(event);
    SDL_PumpEvents();
    CHECK(std::string(queue.events()[0].text.text) == "deferred \xC3\xA9");
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
}
#endif
}
