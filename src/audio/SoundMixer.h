// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "MusicTrack.h"
#include "MusicTypes.h"
#include <memory>
#include <string>
#include <vector>
class OrderVoiceData;
// Application-thread facade. Playback state belongs to the producer/consumer;
// callers send controls and inspect value snapshots, never lock the audio device.
// All public methods, including destruction, run on the application's owner thread.
class SoundMixer
{
  public:
	static constexpr unsigned FadeSampleCount = 2 * Music::GameFadeFrames;
	SoundMixer(unsigned musicvol = 255, unsigned voicevol = 255, bool mute = false);
	~SoundMixer();
	// Loading may wait for preparation, but a failure retains the installed track/set.
	// loadTrack returns its slot, -1 for an unreadable file, or -2 for invalid music.
	// loadTracks attempts every request and reports whether they all succeeded.
	int loadTrack(std::string name, int index = -1);
	int loadTrack(std::string name, MusicTrack index);
	bool loadTracks(const std::vector<std::pair<std::string, MusicTrack>> &requests);
	void setNextTrack(unsigned index, bool early = false);
	void setNextTrack(MusicTrack index, bool early = false);
	void stopMusic();
	void setVolume(unsigned music, unsigned voice, bool mute);
	bool selectMusicSet(const std::string &preference);
	const std::string &getMusicSet() const { return activeMusicSet; }
	static std::vector<std::string> getMusicSets();
	static std::string musicSetLabel(const std::string &name);
	// Zero means preparation failed. A token controls only the preview that issued it;
	// late controls and closes from older screens are ignored.
	unsigned openPreview(const std::array<std::string, 3> &paths);
	void closePreview(unsigned session);
	void previewControl(unsigned session, Music::Control command, double value = 0);
	// Consumer position excludes the producer's look-ahead. Snapshots may lag by a
	// block/report interval; they are never a mutable view of the decoder.
	Music::Snapshot playbackSnapshot();
	Music::Diagnostics diagnostics() const;
	bool enabled() const;
	bool isPlayerTransmittingVoice(int player);
	void addVoiceData(std::shared_ptr<OrderVoiceData> order);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl;
	std::string activeMusicSet = "original";
	unsigned previewSession = 0, nextPreviewSession = 0;
};
