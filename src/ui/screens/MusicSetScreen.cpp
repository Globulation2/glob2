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
	if (!preview.open(paths))
		notice = musicText("Could not open this music set.");
	if (globalContainer && globalContainer->mix)
		globalContainer->mix->setPreview(&preview);
}
MusicSetScreen::~MusicSetScreen()
{
	if (waveformDecoder)
		op_free(waveformDecoder);
	if (globalContainer && globalContainer->mix)
		globalContainer->mix->setPreview(nullptr);
}
void MusicSetScreen::control(const std::function<void()> &fn)
{
	auto *stream =
		globalContainer && globalContainer->mix ? globalContainer->mix->audioStream : nullptr;
	if (stream)
		SDL_LockAudioStream(stream);
	fn();
	if (stream)
		SDL_UnlockAudioStream(stream);
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
		control(
			[this]
			{
				suspended = preview.playing;
				preview.playing = false;
			});
	return false;
}
Element MusicSetScreen::build(const Presentation &p)
{
	bool playing = false, automatic = false;
	double position = 0, duration = 0;
	std::array<double, 3> weights{};
	auto *snapshotStream =
		globalContainer && globalContainer->mix ? globalContainer->mix->audioStream : nullptr;
	if (snapshotStream)
		SDL_LockAudioStream(snapshotStream);
	[&]
	{
		playing = preview.playing;
		automatic = preview.audition;
		position = preview.position();
		duration = preview.duration();
		weights = preview.weights();
		if (preview.failed())
			notice = musicText("Music decoding failed.");
	}();
	if (snapshotStream)
		SDL_UnlockAudioStream(snapshotStream);
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
	for (unsigned i = 0; i < 3; ++i)
		moods.push_back(expanded(column(
			{button("music.mood." + std::to_string(i), musicText(names[i]),
					[this, i] { control([&] { preview.setMood(i); }); }),
			 label(std::to_string(int(weights[i] * 100)) + "%", {.align = TextAlign::Center})},
			{p.pt(3)})));

	body.push_back(row(std::move(moods), {p.pt(8)}));
	auto waveforms = info.waveforms;
	body.push_back(canvas("music.waveforms", {p.pt(600), p.pt(120)},
						  [waveforms, position, duration](Canvas &canvas, Rect rect, const Frame &)
						  {
							  canvas.fillRounded(rect, 8, GAGCore::Color(23, 40, 31));
							  const GAGCore::Color colors[] = {GAGCore::Color(155, 205, 170),
															   GAGCore::Color(237, 199, 120),
															   GAGCore::Color(234, 160, 153)};
							  for (unsigned mood = 0; mood < 3; ++mood)
							  {
								  int middle = rect.y + (rect.h / 3) * mood + rect.h / 6;
								  for (size_t i = 0; i < waveforms[mood].size(); ++i)
								  {
									  int x = rect.x + int(i) * rect.w / 512;
									  int h = int(waveforms[mood][i] * rect.h / 6);
									  canvas.line({x, middle - h}, {x, middle + h}, colors[mood]);
								  }
							  }
							  int x = rect.x + int(position / std::max(1.0, duration) * rect.w);
							  canvas.line({x, rect.y}, {x, rect.y + rect.h},
										  GAGCore::Color(230, 220, 180));
						  }));
	body.push_back(slider(
		"music.position", int(position * 10), 0, std::max(1, int(duration * 10)),
		[this](int v) { control([&] { preview.seekTo(v / 10.0); }); },
		{.valueText = std::to_string(int(position)) + " / " + std::to_string(int(duration)) + " s",
		 .caption = musicText("Playback position")}));
	body.push_back(button("music.play", musicText(playing ? "Pause" : "Play"),
						  [this] { control([&] { preview.playing = !preview.playing; }); }));
	body.push_back(heading(musicText("Test crossfades")));
	body.push_back(slider(
		"music.fade", fadeMs, 0, 10000,
		[this](int v)
		{
			fadeMs = v;
			control([&] { preview.setFade(v / 1000.0); });
		},
		{.valueText = std::to_string(fadeMs) + " ms", .caption = musicText("Fade duration")}));
	body.push_back(slider("music.blend", blend, 0, 200,
						  [this](int v)
						  {
							  blend = v;
							  control([&] { preview.setBlend(v / 100.0); });
						  },
						  {.caption = musicText("Calm → Building → Combat")}));
	body.push_back(toggle("music.auto", musicText("Audition each mood for eight seconds"),
						  automatic,
						  [this](bool value) { control([&] { preview.audition = value; }); }));
	body.push_back(button("music.reset", musicText("Reset to game behavior"),
						  [this]
						  {
							  fadeMs = 371;
							  blend = 0;
							  control([&] { preview.reset(); });
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
