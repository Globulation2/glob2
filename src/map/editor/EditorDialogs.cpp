// SPDX-License-Identifier: GPL-3.0-or-later
#include "EditorDialogs.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <stdexcept>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

EditorConfirmDialog::EditorConfirmDialog(std::string title, std::string message, std::vector<Choice> choices, int cancelChoice)
	: InGameDialog(Glob2UI::Surface::Editor), title(std::move(title)), text(std::move(message)), choices(std::move(choices)),
	  cancelIndex(cancelChoice)
{
	if (this->choices.empty() || cancelIndex < 0 || cancelIndex >= int(this->choices.size()))
		throw std::invalid_argument("Editor confirmations need a cancel choice");
}

void EditorConfirmDialog::choose(int index)
{
	if (index >= 0 && index < int(choices.size()))
		finish(index);
}

Element EditorConfirmDialog::build(const Presentation &p)
{
	std::vector<fe::MenuAction> actions;
	for (std::size_t i = 0; i < choices.size(); ++i)
	{
		const int index = int(i);
		fe::MenuAction action{"choice/" + std::to_string(i), choices[i].label, [this, index] { choose(index); }};
		action.primary = choices[i].primary;
		// Return picks the highlighted choice and Escape the cancel choice; the
		// destructive choices (discard, replace) have no shortcut.
		action.shortcut = index == cancelIndex ? SDLK_ESCAPE : choices[i].primary ? SDLK_RETURN : SDLK_UNKNOWN;
		actions.push_back(std::move(action));
	}
	std::vector<Element> body;
	if (!title.empty())
		body.push_back(fe::heading(title));
	body.push_back(fe::paragraph(text));
	return fe::footer(fe::column(std::move(body), {p.pt(8)}), dialogActions(std::move(actions), p));
}

EditorProgressDialog::EditorProgressDialog(Map &map, std::string caption)
	: InGameDialog(Glob2UI::Surface::Editor), job(map), caption(std::move(caption))
{
}

Element EditorProgressDialog::build(const Presentation &p)
{
	return fe::footer(fe::column({fe::heading(caption), fe::progress(permille, 1000)}, {p.pt(10)}),
					  dialogActions({{"cancel", fe::tr("[Cancel]"), [this] { cancel(); }, false, SDLK_ESCAPE}}, p));
}

void EditorProgressDialog::onUpdate(Uint32)
{
	// Present the card at 0 % before the first slice of work.
	if (!presented)
	{
		presented = true;
		return;
	}
	step(12);
}

void EditorProgressDialog::step(unsigned budgetMs)
{
	if (finished())
		return;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
	bool done = false;
	do
		done = job.advance(16384);
	while (!done && std::chrono::steady_clock::now() < deadline);
	if (done)
	{
		job.commit();
		finish(COMPLETED);
		return;
	}
	const int next = static_cast<int>(job.progress() * 1000);
	if (next != permille)
	{
		permille = next;
		invalidate();
	}
}
