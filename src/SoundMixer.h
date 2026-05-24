// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL.h>
#include <SDL_audio.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <vector>
#include <queue>
#include <map>
#include <memory>

class OrderVoiceData;

class SoundMixer
{
public:
	enum MusicMode
	{
		MODE_STOPPED = 0,
		MODE_NORMAL,
		MODE_EARLY_CHANGE,
		MODE_STOP,
		MODE_START
	} mode;
	std::vector<OggVorbis_File *> tracks;
	int actTrack, nextTrack;
	bool earlyChange;
	bool soundEnabled;
	unsigned musicVolume;
	unsigned voiceVolume;
	
	//! Voice for one player
	struct PlayerVoice
	{
		//! float sample from speex decoder
		std::queue<float> voiceDatas;
		//! subsample precision for voice (8Khz instead of 44.1Khz)
		float voiceSubIndex;
		//! value used for interpolation and optimisation. Linear interpolation is done on the 8Khz audio datas
		float voiceVal0;
		float voiceVal1;
	};
	//! Map of voices to players
	std::map<int, PlayerVoice> voices;
	//! pointer to the structure holding the speex decoder
	void *speexDecoderState;
	
	//! if voice data is available, insert it to output
	inline void handleVoiceInsertion(int *outputSample, int voicevol);
	
protected:
	void openAudio(void);

public:
	SoundMixer(unsigned musicvol = 255, unsigned voicevol = 255, bool mute = false);

	~SoundMixer();

	//! Load an ogg file and add (or replace at `index`) into the track list.
	//! Returns the resulting track index on success, -1 if the file cannot be
	//! opened, or -2 if it is not a valid ogg bitstream. On success the
	//! OggVorbis_File takes ownership of the underlying FILE* and closes it via
	//! ov_clear in ~SoundMixer.
	int loadTrack(const std::string name, int index = -1);

	void setNextTrack(unsigned i, bool earlyChange=false);

	void setVolume(unsigned musicVolume, unsigned voiceVolume, bool mute);
	
	void stopMusic(void);
	
	//! Tells whether the given player is being heard in voip
	bool isPlayerTransmittingVoice(int player);
	
	//! Add voice data from order. Data should be copied as order will be destroyed after this call
	void addVoiceData(std::shared_ptr<OrderVoiceData> order);
};




