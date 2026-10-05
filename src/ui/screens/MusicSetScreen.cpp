// SPDX-License-Identifier: GPL-3.0-or-later
#include "MusicUI.h"
#include "MusicSetScreen.h"
#include "SoundMixer.h"
#include "GlobalContainer.h"
#include "ui/OnlineUI.h"
#include <chrono>
using namespace Glob2UI;
MusicSetScreen::MusicSetScreen(Music::Metadata value, const std::array<std::string, 3> &paths,
							   std::string action)
	: info(std::move(value)), primaryAction(std::move(action)), paths(paths)
{
	if (!info.cover.empty())
		images.insert("cover", std::string(info.cover.begin(), info.cover.end()));
	if (globalContainer && globalContainer->mix)
		previewSession = globalContainer->mix->openPreview(paths);
	if (!previewSession)
		notice = musicText("Could not open this music set.");
}
MusicSetScreen::~MusicSetScreen()
{
	if (waveformDecoder)
		op_free(waveformDecoder);
	if (globalContainer && globalContainer->mix)
		globalContainer->mix->closePreview(previewSession);
}
void MusicSetScreen::control(Music::Control command, double value)
{
	if (globalContainer && globalContainer->mix)
		globalContainer->mix->previewControl(previewSession, command, value);
	invalidate();
}
void MusicSetScreen::onTimer(Uint32)
{
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3);
	while (waveformMood < 3 && std::chrono::steady_clock::now() < deadline)
	{
		if (!waveformDecoder)
		{
			int error = 0;
			waveformDecoder = op_open_file(paths[waveformMood].c_str(), &error);
			info.waveforms[waveformMood].assign(512, 0);
			waveformFrame = 0;
			if (!waveformDecoder)
			{
				++waveformMood;
				continue;
			}
		}
		std::array<std::int16_t, Music::Chunk * 2> pcm{};
		int n = op_read_stereo(waveformDecoder, pcm.data(), pcm.size());
		if (n <= 0)
		{
			op_free(waveformDecoder);
			waveformDecoder = nullptr;
			++waveformMood;
			continue;
		}
		auto total = std::max<std::int64_t>(1, info.frames);
		for (int i = 0; i < n; ++i)
		{
			auto bin = std::min<std::int64_t>(511, (waveformFrame + i) * 512 / total);
			float peak =
				std::max(std::abs(int(pcm[i * 2])), std::abs(int(pcm[i * 2 + 1]))) / 32768.0f;
			info.waveforms[waveformMood][bin] = std::max(info.waveforms[waveformMood][bin], peak);
		}
		waveformFrame += n;
	}
	invalidate();
}
bool MusicSetScreen::interceptEvent(const SDL_Event &event)
{
	if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_DID_ENTER_BACKGROUND)
		control(Music::Control::Play, 0);
	return false;
}
Element MusicSetScreen::build(const Presentation &p)
{
	const auto state = globalContainer && globalContainer->mix
						   ? globalContainer->mix->playbackSnapshot()
						   : Music::Snapshot{};
	const bool playing = state.playing, automatic = state.audition;
	const double position = state.position, duration = state.duration;
	const auto weights = state.weights;
	if (state.failed)
		notice = musicText("Music decoding failed.");
	std::vector<Element> body{paragraph(info.artist), paragraph(info.description),
							  caption(info.license + " · " + info.credits, false)};
	Element art = musicPlaceholder(p.pt(96));
	if (auto *cover = images.get(nullptr, "cover", [] {}))
		art = previewPicture(cover, p.pt(96));
	body = {
		row({art, expanded(column(std::move(body), {p.pt(6)}))}, {p.pt(12), CrossAlign::Start})};
	if (!info.sources.empty())
		body.push_back(paragraph(info.sources));
	if (info.aiGenerated)
		body.push_back(caption(musicText("Includes AI-generated audio"), false));
	if (!notice.empty())
		body.push_back(paragraph(notice));
	std::vector<Element> moods;
	const char *names[] = {"Calm", "Building", "Combat"};
	const std::array<GAGCore::Color, 3> moodColors{GAGCore::Color(155, 205, 170),
												   GAGCore::Color(237, 199, 120),
												   GAGCore::Color(234, 160, 153)};
	for (unsigned i = 0; i < 3; ++i)
		moods.push_back(expanded(column(
			{button("music.mood." + std::to_string(i), musicText(names[i]),
					[this, i] { control(Music::Control::Mood, i); }),
			 label(std::to_string(int(weights[i] * 100)) + "%", {.align = TextAlign::Center})},
			{p.pt(3)})));

	body.push_back(row(std::move(moods), {p.pt(8)}));
	auto waveforms = info.waveforms;
	body.push_back(canvas(
		"music.waveforms", {p.pt(600), p.pt(120)},
		[waveforms, position, duration, moodColors, labelWidth = p.pt(84)](Canvas &canvas,
																		   Rect rect, const Frame &)
		{
			canvas.fillRounded(rect, 8, GAGCore::Color(23, 40, 31));
			for (unsigned mood = 0; mood < 3; ++mood)
			{
				int middle = rect.y + (rect.h / 3) * mood + rect.h / 6;
				canvas.text(
					{rect.x + 6, middle - 8}, FontRole::Support,
					musicText(std::array<const char *, 3>{"Calm", "Building", "Combat"}[mood]),
					moodColors[mood]);
				for (size_t i = 0; i < waveforms[mood].size(); ++i)
				{
					int x = rect.x + labelWidth + int(i) * (rect.w - labelWidth) / 512;
					int h = int(waveforms[mood][i] * rect.h / 6);
					canvas.line({x, middle - h}, {x, middle + h}, moodColors[mood]);
				}
			}
			int x = rect.x + labelWidth +
					int(position / std::max(1.0, duration) * (rect.w - labelWidth));
			canvas.line({x, rect.y}, {x, rect.y + rect.h}, GAGCore::Color(230, 220, 180));
		}));
	body.push_back(slider(
		"music.position", int(position * 10), 0, std::max(1, int(duration * 10)),
		[this](int v) { control(Music::Control::Seek, v / 10.0); },
		{.valueText = std::to_string(int(position)) + " / " + std::to_string(int(duration)) + " s",
		 .caption = musicText("Playback position")}));
	body.push_back(button("music.play", musicText(playing ? "Pause" : "Play"),
						  [this, playing] { control(Music::Control::Play, !playing); }));
	body.push_back(heading(musicText("Test crossfades")));
	body.push_back(slider(
		"music.fade", fadeMs, 0, 10000,
		[this](int v)
		{
			fadeMs = v;
			control(Music::Control::Fade, v / 1000.0);
		},
		{.valueText = std::to_string(fadeMs) + " ms", .caption = musicText("Fade duration")}));
	body.push_back(slider("music.blend", blend, 0, 200,
						  [this](int v)
						  {
							  blend = v;
							  control(Music::Control::Blend, v / 100.0);
						  },
						  {.caption = musicText("Calm → Building → Combat")}));
	body.push_back(toggle("music.auto", musicText("Audition each mood for eight seconds"),
						  automatic,
						  [this](bool value) { control(Music::Control::Audition, value); }));
	body.push_back(button("music.reset", musicText("Reset to game behavior"),
						  [this]
						  {
							  fadeMs = 371;
							  blend = 0;
							  control(Music::Control::Reset);
						  }));
	OnlinePanel panel;
	panel.title = info.title;
	panel.body = scroll("music.detail", column(std::move(body), {p.pt(12)}));
	panel.actions = {{"back", musicText("Back"), [this] { endExecute(0); }, false, SDLK_ESCAPE}};
	if (!primaryAction.empty())
		panel.actions.insert(panel.actions.begin(),
							 {"music.primary", primaryAction, [this] { endExecute(1); }, true});
	return onlinePanel(panel, p);
}
