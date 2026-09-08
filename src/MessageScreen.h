// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Glob2Screen.h"
#include <string>
#include <vector>

// An in-game decision with explicit completion (caption index, or app quit).
class MessageScreen : public Glob2Screen
{
public:
    MessageScreen(const std::string& message, const std::vector<std::string>& captions);
    void onAction(GAGGUI::Widget*, GAGGUI::Action action, int choice, int) override;
};
