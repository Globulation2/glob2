/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __SOUNDMIXER_H
#define __SOUNDMIXER_H

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL.h>
#include <SDL_audio.h>
#else
#include <Types.h>
#endif
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <vector>
#include <queue>
#include <map>
#include <boost/shared_ptr.hpp>

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
	unsigned volume;
	
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
	inline void handleVoiceInsertion(int *outputSample);
	
protected:
	void openAudio(void);

public:
	SoundMixer(unsigned volume = 255, bool mute = false);

	~SoundMixer();

	int loadTrack(const char *name);

	void setNextTrack(unsigned i, bool earlyChange=false);

	void setVolume(unsigned volume, bool mute);
	
	void stopMusic(void);
	
	//! Add voice data from order. Data should be copied as order will be destroyed after this call
	void addVoiceData(boost::shared_ptr<OrderVoiceData> order);
};

#endif



