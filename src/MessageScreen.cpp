// SPDX-License-Identifier: GPL-3.0-or-later
#include "MessageScreen.h"
#include <GUITextArea.h>
#include <GUIButton.h>
#include <stdexcept>

MessageScreen::MessageScreen(const std::string& message, const std::vector<std::string>& captions)
{
    if (captions.empty() || captions.size() > 3) throw std::invalid_argument("Messages need one to three choices");
    addWidget(new GAGGUI::TextArea(20, 100, 600, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", true, message.c_str()));
    for (unsigned i = 0; i < captions.size(); ++i)
        addWidget(new GAGGUI::TextButton(20 + i * 210, 340, 180, 40,
            ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", captions[i], i,
            i == captions.size() - 1 ? 27 : 0));
}
void MessageScreen::onAction(GAGGUI::Widget*, GAGGUI::Action action, int choice, int)
{
    if (action == GAGGUI::BUTTON_RELEASED || action == GAGGUI::BUTTON_SHORTCUT) endExecute(choice);
}
