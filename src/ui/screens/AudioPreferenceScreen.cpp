// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioPreferenceScreen.h"
#include "GlobalContainer.h"
#include "SoundMixer.h"
#include <Toolkit.h>
#include <algorithm>

void AudioPreferenceScreen::choose(bool sound)
{
	auto &settings = globalContainer->settings;
	const int previousMute = settings.mute;
	const bool previouslyChosen = settings.audioPreferenceChosen;
	settings.mute = sound ? 0 : 1;
	settings.audioPreferenceChosen = true;
	// Persist before unmuting: a failed write must never emit a sound preview.
	if (!settings.save())
	{
		settings.mute = previousMute;
		settings.audioPreferenceChosen = previouslyChosen;
		saveFailed = true;
		invalidate();
		return;
	}
	if (globalContainer->mix)
		globalContainer->mix->setVolume(settings.musicVolume, settings.voiceVolume, settings.mute);
	endExecute(sound ? 1 : 0);
}

Glob2UI::Element AudioPreferenceScreen::build(const Glob2UI::Presentation &p)
{
	using namespace Glob2UI;
	const int pictureHeight = p.landscape() ? 112 : p.safe.h < p.pt(600) ? 140 : 200;
	auto *icons = GAGCore::Toolkit::getSprite("data/gfx/audiochoice");
	std::vector<Element> body;
	body.push_back(paragraph(tr("[audio preference explanation]"), {FontRole::Body, false, TextAlign::Center}));
	body.push_back(stack({
		row({expanded(button("audio.picture.silence", "", [this] { choose(false); },
			{.flat = true, .minHeight = double(pictureHeight), .accessibleLabel = tr("[Silence]")})),
			expanded(button("audio.picture.sound", "", [this] { choose(true); },
			{.flat = true, .minHeight = double(pictureHeight), .accessibleLabel = tr("[Sound]")}))}, {0}),
		canvas("audio.preview", {p.pt(340), p.pt(pictureHeight)},
			[icons](Canvas &c, Rect r, const Frame &f) {
				const auto &palette = frontendTheme().palette;
				const int gap = std::max(4, r.w / 40), width = (r.w - gap) / 2;
				for (int side = 0; side < 2; ++side)
				{
					const std::string key = side ? "audio.picture.sound" : "audio.picture.silence";
					const Rect card{r.x + side * (width + gap), r.y + 2, width, r.h - 4};
					c.fillRounded(card, 12, f.focused(key) ? palette.focus : palette.line);
					const Rect inside{card.x + 2, card.y + 2, card.w - 4, card.h - 4};
					c.fillRounded(inside, 10, f.pressed(key) ? palette.field : palette.paper);
					const int size = std::max(1, std::min(inside.w, inside.h) - 16);
					const Rect icon{inside.x + (inside.w - size) / 2, inside.y + (inside.h - size) / 2, size, size};
					c.transformed(double(size) / icons->getW(side), {icon.x, icon.y}, icon,
						[&] { c.drawSprite({0, 0}, icons, side); });
				}
			}, {.accessibleText = tr("[audio choices description]")})}));
	body.push_back(hint(tr("[audio preference change later]")));
	if (saveFailed) body.push_back(paragraph(tr("[audio preference save failed]")));
	return page(tr("[Sound or silence?]"), scroll("audio.body", column(std::move(body))),
		row({expanded(button("audio.silence", tr("[Silence]"), [this] { choose(false); })),
			expanded(button("audio.sound", tr("[Sound]"), [this] { choose(true); }))}, {p.pt(8)}), p, 520);
}
