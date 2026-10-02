// SPDX-License-Identifier: GPL-3.0-or-later
#include "MessageScreen.h"
#include <stdexcept>

using namespace Glob2UI;

MessageScreen::MessageScreen(const std::string &message, const std::vector<std::string> &captions)
	: message(message), captions(captions)
{
	if (captions.empty() || captions.size() > 3)
		throw std::invalid_argument("Messages need one to three choices");
}

MessageScreen::MessageScreen(const std::string &title, const std::string &message, const std::vector<std::string> &captions)
	: MessageScreen(message, captions)
{
	this->title = title;
}

Element MessageScreen::build(const Presentation &p)
{
	std::vector<MenuAction> choices;
	for (std::size_t i = 0; i < captions.size(); ++i)
	{
		MenuAction action{"choice/" + std::to_string(i), captions[i], [this, i] { endExecute(int(i)); }};
		action.primary = i == 0;
		action.shortcut = i == 0 ? SDLK_RETURN : i == captions.size() - 1 ? SDLK_ESCAPE : SDLK_UNKNOWN;
		choices.push_back(std::move(action));
	}
	if (!title.empty())
	{
		CardOptions options;
		options.padding = p.pt(18);
		return center(maxWidth(p.pt(460), card(column({heading(title), paragraph(message), actions(std::move(choices), p)}, {p.pt(12)}), options)));
	}
	return page("", scroll("message", paragraph(message)), actions(std::move(choices), p), p);
}
