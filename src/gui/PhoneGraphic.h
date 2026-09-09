// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Optional phone presentation for a widget whose content is graphical. Coordinates
// are local to the panel; the presenter owns scaling and clipping.
class PhoneGraphic {
public:
    virtual ~PhoneGraphic() = default;
    virtual void paintPhone(int width, int height) = 0;
    virtual void inspectPhone(int x, int y) = 0;
};
