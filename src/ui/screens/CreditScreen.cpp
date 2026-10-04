// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "CreditScreen.h"
#include "GlobalContainer.h"
#include "render/UnitAnimation.h"
#include <FileManager.h>
#include <Stream.h>
#include <Toolkit.h>
#include <iostream>

using namespace Glob2UI;

namespace
{
// Credits decorations use worker walking direction 3.
constexpr int kWorkerWalkFrameBase = unitAnimationFrame(64, 3, 0);
constexpr int kWorkerWalkFrameCount = UNIT_ANIMATION_FRAMES_PER_DIRECTION;
} // namespace

CreditScreen::CreditScreen()
{
	// InputLineStream owns the backend and releases the file even if readLine() throws.
	GAGCore::InputLineStream input(
		GAGCore::Toolkit::getFileManager()->openInputStreamBackend("data/authors.txt"));
	if (input.isEndOfStream())
		std::cerr << "CreditScreen: can't open data/authors.txt" << std::endl;
	while (!input.isEndOfStream())
	{
		auto line = input.readLine();
		// Rip out e-mail addresses.
		const auto first = line.find('<'), last = line.rfind('>');
		if (first != std::string::npos && last != std::string::npos && last >= first)
			line.erase(first, last - first + 1);
		Line entry;
		entry.decoration = line.find('*') != std::string::npos;
		entry.text = entry.decoration ? std::string() : line;
		lines.push_back(std::move(entry));
	}
}

Element CreditScreen::build(const Presentation &p)
{
	auto *units = globalContainer ? globalContainer->units : nullptr;
	const bool decorations =
		units && units->getFrameCount() >= kWorkerWalkFrameBase + kWorkerWalkFrameCount;
	std::vector<Element> rows;
	// Start below the viewport so the text scrolls in, as the original did.
	rows.push_back(spacer(p.pt(240)));
	for (const auto &line : lines)
	{
		if (line.decoration)
		{
			if (!decorations)
				continue;
			rows.push_back(center(canvas("", {p.pt(32), p.pt(32)},
										 [this, units](Canvas &c, Rect r, const Frame &)
										 {
											 const int frame = kWorkerWalkFrameBase +
															   (walkFrame & 7) * UNIT_ANIMATION_FRAME_MULTIPLIER;
											 units->setBaseColor(128, 128, 128);
											 const int dx = (units->getW(frame) - 32) / 2, dy = (units->getH(frame) - 32) / 2;
											 c.drawSprite({r.x - dx, r.y - dy}, units, frame);
										 })));
		}
		else if (line.text.empty())
			rows.push_back(spacer(p.pt(6)));
		else
			rows.push_back(paragraph(line.text, {FontRole::Body, false, TextAlign::Center}));
	}
	rows.push_back(spacer(p.pt(120)));
	auto body = scroll("credits", column(std::move(rows), {p.pt(4)}), {false, false});
	auto content = column({expanded(body), actions({{"back", tr("[Back]"), [this] { endExecute(0); }, false, SDLK_ESCAPE}}, p)}, {p.pt(8)});
	if (p.touch)
		return content;
	// The desktop credits roll on a paper panel filling the window, as before.
	CardOptions panel;
	panel.radius = 0;
	panel.shadow = false;
	panel.padding = p.pt(12);
	return card(content, panel);
}

void CreditScreen::onTimer(Uint32 tick)
{
	if (tick - lastStep < 40)
		return;
	lastStep = tick;
	++walkFrame;
	if (!autoScroll)
		return;
	if (auto *credits = host().find("credits"))
	{
		if (credits->scrollOffset() >= credits->scrollMaximum())
			autoScroll = false;
		else
			credits->scrollBy(1, host());
	}
}

void CreditScreen::onEvent(const SDL_Event &event)
{
	if (event.type == SDL_EVENT_MOUSE_WHEEL || event.type == SDL_EVENT_FINGER_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
		autoScroll = false;
}
