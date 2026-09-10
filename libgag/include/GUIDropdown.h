// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>
#include <string>
#include <vector>

namespace GAGGUI
{
// Floating native choice list. The host draws it after its content and routes
// events here first while open; it never changes the host's layout or focus.
// Coordinates use the same logical pixels as DrawableSurface and SDL UI events.
class Dropdown
{
public:
    void open(SDL_Rect anchor, SDL_Rect available, const std::vector<std::string>& options,
              int selected, GAGCore::Font* font);
    void close() { opened=false; dragging=false; }
    bool isOpen() const { return opened; }
    SDL_Rect bounds() const { return box; }
    SDL_Rect itemBounds(int index) const;
    int highlighted() const { return active; }
    // Returns a committed option index, or -1. Navigation never commits.
    int handleEvent(const SDL_Event& event);
    void paint(GAGCore::DrawableSurface* surface, GAGCore::Color text,
               GAGCore::Color background, GAGCore::Color selection, GAGCore::Color border);
private:
    struct Item { std::vector<std::string> lines; int top=0,height=0; };
    std::vector<Item> items;
    GAGCore::Font* font=nullptr;
    SDL_Rect box{}, list{};
    int active=0, offset=0, total=0, lineHeight=0, grab=0;
    bool opened=false, dragging=false;
    void reveal();
    void scrollTo(int value);
    SDL_Rect thumb() const;
    int hit(int x,int y) const;
};
}
