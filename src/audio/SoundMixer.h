// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <opusfile.h>
#include <AudioFormat.h>
#include <vector>
#include <queue>
#include <map>
#include <memory>
#include <string>

#include "MusicTrack.h"
#include "PlayerVoice.h"

namespace Music { class Preview; }
class OrderVoiceData;

class SoundMixer
{
public:
	//! Preserve the original fade duration (16384 frames at 44100 Hz).
	static constexpr unsigned FadeSampleCount = 2 * ((16384 * GAGCore::AudioSampleRate + 22050) / 44100);
	enum MusicMode
	{
		MODE_STOPPED = 0,
		MODE_NORMAL,
		MODE_EARLY_CHANGE,
		MODE_STOP,
		MODE_START
	} mode;
	std::vector<OggOpusFile *> tracks;
	int actTrack, nextTrack;
	//! How far the current fade has advanced, in Sint16 samples. Carried
	//! across callbacks so a fade lasts the same time whatever the device
	//! buffer size. Read and written on the audio thread.
	unsigned fadePos;
	//! Track asked for while a fade was already running, or -1 for none. A
	//! fade spans many callbacks, so the request is held here and started by
	//! mixaudio() once the fade lands, rather than cutting it off mid-mix.
	//! Guarded by SDL_LockAudioStream, like mode and fadePos.
	int pendingTrack;
	bool soundEnabled;
    SDL_AudioStream *audioStream = nullptr;
	unsigned musicVolume;
	unsigned voiceVolume;
	
	//! Map of voices to players. PlayerVoice (the SDL-free resampling state
	//! machine) lives in PlayerVoice.h.
	std::map<int, PlayerVoice> voices;
	//! pointer to the structure holding the speex decoder
	void *speexDecoderState;
	
	//! if voice data is available, insert it to output
	inline void handleVoiceInsertion(int *outputSample, int voicevol);
	
protected:
	void openAudio(void);
	std::string activeMusicSet = "original";

public:
	SoundMixer(unsigned musicvol = 255, unsigned voicevol = 255, bool mute = false);

	~SoundMixer();

	//! Load an Ogg Opus file and add (or replace at `index`) into the track list.
	//! Returns the resulting track index on success, -1 if the file cannot be
	//! opened, or -2 if it is not a usable Ogg Opus stream. On success the
	//! OggOpusFile takes ownership of the underlying FILE* and closes it via
	//! op_free in ~SoundMixer.
	int loadTrack(const std::string name, int index = -1);

	//! Load `name` into the slot for the given enum track. Convenience wrapper
	//! over the int-indexed overload so callers don't hard-code track numbers.
	int loadTrack(const std::string name, MusicTrack track);

	void setNextTrack(unsigned i, bool earlyChange=false);

	//! Enum-typed overload of setNextTrack. Prefer this in new code so call
	//! sites read as `setNextTrack(MusicTrack::WarEvent, true)` rather than
	//! `setNextTrack(4, true)`.
	void setNextTrack(MusicTrack track, bool earlyChange=false);

	static std::vector<std::string> getMusicSets();
	static std::string musicSetLabel(const std::string& name);
	//! Empty preference chooses randomly. Replaces the complete trio atomically.
	bool selectMusicSet(const std::string& preference);
	const std::string& getMusicSet() const { return activeMusicSet; }

	void setVolume(unsigned musicVolume, unsigned voiceVolume, bool mute);
	
	void stopMusic(void);
    // A screen-owned preview temporarily replaces music while retaining gameplay
    // cursor/mood state. Guard lifetime and control changes with the stream lock.
    Music::Preview* preview = nullptr;
    void setPreview(Music::Preview* value);
	
	//! Tells whether the given player is being heard in voip
	bool isPlayerTransmittingVoice(int player);
	
	//! Add voice data from order. Data should be copied as order will be destroyed after this call
	void addVoiceData(std::shared_ptr<OrderVoiceData> order);
};




