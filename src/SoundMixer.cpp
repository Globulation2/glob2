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

#include "SoundMixer.h"
#include "Order.h"
#include <Toolkit.h>
#include <FileManager.h>
using namespace GAGCore;
#include <iostream>
#include <assert.h>

#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif

#include <speex/speex.h>

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_endian.h>
#else
#include <Types.h>
#endif

#ifdef WIN32
#include <malloc.h>
#endif

#define SAMPLE_COUNT_PER_SLICE 4096*8
#define INTERPOLATION_RANGE 65535
#define INTERPOLATION_BITS 16
#define SPEEX_FRAME_SIZE 160

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define OGG_BYTEORDER 0
#else
#define OGG_BYTEORDER 1
#endif

static int interpolationTable[SAMPLE_COUNT_PER_SLICE];

static void initInterpolationTable(void)
{
	double l = static_cast<double>(SAMPLE_COUNT_PER_SLICE-1);
	double m = INTERPOLATION_RANGE;
	double a = - (2) / (l * l * l);
	double b = (3) / (l * l);
	for (unsigned i=0; i<SAMPLE_COUNT_PER_SLICE; i++)
	{
		double x = static_cast<double>(i);
		double v = m * (a * (x * x * x) + b * (x* x));
		interpolationTable[i] = static_cast<int>(v);
	}
}

void SoundMixer::handleVoiceInsertion(int *outputSample)
{
	// if no more voice
	if (voices.empty())
		return;
	
	float value = 0;
	for (std::map<int, PlayerVoice>::iterator i = voices.begin(); i != voices.end();)
	{
		struct PlayerVoice &pv = i->second;
		value += (1-pv.voiceSubIndex) * pv.voiceVal0 + pv.voiceSubIndex * pv.voiceVal1;
	
		// increment index, keep track of stereo
		pv.voiceSubIndex += (8000.0f/44100.0f)*0.5f;
		if (pv.voiceSubIndex > 1)
		{
			pv.voiceSubIndex -= 1;
			pv.voiceVal0 = pv.voiceVal1;
			pv.voiceDatas.pop();
			pv.voiceVal1 = pv.voiceDatas.front();
			
			// if there is no more data in this voice, remove it
			if (pv.voiceDatas.empty())
			{
				// go to next voice
				std::map<int, PlayerVoice>::iterator j = i;
				++i;
				voices.erase(j);
				continue;
			}
		}
		
		// go to next voice
		++i;
	}
	// saturate
	value = std::min(value, 32767.0f);
	value = std::max(value, -32767.0f);
	// write sample
	*outputSample = (static_cast<int>(3.0f * (value)) + (*outputSample)) / 4;
}

void mixaudio(void *voidMixer, Uint8 *stream, int len)
{
	SoundMixer *mixer = static_cast<SoundMixer *>(voidMixer);
	unsigned nsamples = static_cast<unsigned>(len) >> 1;
	Sint16 *mix = reinterpret_cast<Sint16 *>(stream);
	int vol = static_cast<int>(mixer->volume);

	assert(mixer->actTrack >= 0);
	assert(mixer->mode != SoundMixer::MODE_STOPPED);
	// Dejan: this is supposed to fix reported problem on Gentoo
	// assert(nsamples == SAMPLE_COUNT_PER_SLICE);
	assert(nsamples);

	if (mixer->mode == SoundMixer::MODE_EARLY_CHANGE)
	{
		Sint16 *track0 = reinterpret_cast<Sint16 *>(alloca(len));
		Sint16 *track1 = reinterpret_cast<Sint16 *>(alloca(len));
		long rest;
		char *p;

		// read first ogg
		rest = len;
		p = reinterpret_cast<char *>(track0);
		ogg_int64_t firstPos = ov_pcm_tell(mixer->tracks[mixer->actTrack]);
		while(rest > 0)
		{
			int bs;
			long ret = ov_read(mixer->tracks[mixer->actTrack], p, rest, OGG_BYTEORDER, 2, 1, &bs);
			if (ret == 0) // EOF
			{
				ov_pcm_seek(mixer->tracks[mixer->actTrack], 0);
			}
			else if (ret < 0) // stream error
			{
			}
			else
			{
				rest -= ret;
				p += ret;
			}
		}

		// read second ogg
		ov_pcm_seek(mixer->tracks[mixer->nextTrack], firstPos);
		rest = len;
		p = reinterpret_cast<char *>(track1);
		while(rest > 0)
		{
			int bs;
			long ret = ov_read(mixer->tracks[mixer->nextTrack], p, rest, OGG_BYTEORDER, 2, 1, &bs);
			if (ret == 0) // EOF
			{
				ov_pcm_seek(mixer->tracks[mixer->nextTrack], 0);
			}
			else if (ret < 0) // stream error
			{
			}
			else
			{
				rest -= ret;
				p += ret;
			}
		}

		// mix
		for (unsigned i=0; i<nsamples; i++)
		{
			int t0 = track0[i];
			int t1 = track1[i];
			int intI = interpolationTable[i];
			int val = (intI*t1+((INTERPOLATION_RANGE-intI)*t0))>>INTERPOLATION_BITS;
			val = (val * vol)>>8;
			mixer->handleVoiceInsertion(&val);
			mix[i] = val;
		}

		// clear change
		mixer->actTrack = mixer->nextTrack;
		mixer->mode = SoundMixer::MODE_NORMAL;
	}
	else
	{
		// read ogg
		long rest = len;
		char *p = reinterpret_cast<char *>(mix);
		while(rest > 0)
		{
			int bs;
			long ret = ov_read(mixer->tracks[mixer->actTrack], p, rest, OGG_BYTEORDER, 2, 1, &bs);
			if (ret == 0) // EOF
			{
				mixer->actTrack = mixer->nextTrack;
				ov_pcm_seek(mixer->tracks[mixer->actTrack], 0);
			}
			else if (ret < 0) // stream error
			{
			}
			else
			{
				rest -= ret;
				p += ret;
			}
		}

		// volume & fading
		if (mixer->mode == SoundMixer::MODE_NORMAL)
		{
			if (vol != 255)
				for (unsigned i=0; i<nsamples; i++)
				{
					int t = mix[i];
					t = (t * vol) >> 8;
					mixer->handleVoiceInsertion(&t);
					mix[i] = t;
				}
		}
		else if (mixer->mode == SoundMixer::MODE_START)
		{
			for (unsigned i=0; i<nsamples; i++)
			{
				int t = mix[i];
				t = (interpolationTable[i]*t) >> INTERPOLATION_BITS;
				t = (t * vol) >> 8;
				mixer->handleVoiceInsertion(&t);
				mix[i] = t;
			}
			mixer->mode = SoundMixer::MODE_NORMAL;
		}
		else if (mixer->mode == SoundMixer::MODE_STOP)
		{
			for (unsigned i=0; i<nsamples; i++)
			{
				int t = mix[i];
				int intI = interpolationTable[i];
				t = ((INTERPOLATION_RANGE-intI)*t) >> INTERPOLATION_BITS;
				t = (t * vol) >> 8;
				mixer->handleVoiceInsertion(&t);
				mix[i] = t;
			}
			mixer->mode = SoundMixer::MODE_STOPPED;
			SDL_PauseAudio(1);
		}
	}
}

void SoundMixer::openAudio(void)
{
	SDL_AudioSpec as;
	// Set 16-bit stereo audio at 44Khz
	as.freq = 44100;
	as.format = AUDIO_S16SYS;
	as.channels = 2;
	as.samples = SAMPLE_COUNT_PER_SLICE>>1;
	as.callback = mixaudio;
	as.userdata = this;
	
	// Open the audio device and start playing sound!
	if (SDL_OpenAudio(&as, NULL) < 0)
	{
		soundEnabled = false;
		std::cerr << "SoundMixer : Unable to open audio: " << SDL_GetError() << std::endl;
		return;
	}
	else
	{
		soundEnabled = true;
		mode = MODE_STOPPED;
	}
	
	// Open Speex decoder
	speexDecoderState = speex_decoder_init(&speex_nb_mode);
	int tmp = 1;
	speex_decoder_ctl(speexDecoderState, SPEEX_SET_ENH, &tmp);
	
}

SoundMixer::SoundMixer(unsigned volume, bool mute)
{
	actTrack = -1;
	nextTrack = -1;
	this->volume = volume;
	mode = MODE_STOPPED;
	speexDecoderState = NULL;
	
	initInterpolationTable();
		
	if (mute)
	{
		this->volume = 0;
	}
	openAudio();
/*
	if (mute)
	{
		soundEnabled = false;
		std::cout << "SoundMixer : No volume, audio has been disabled !" << std::endl;
		return;
	}
	else
	{
		openAudio();
	}
*/
}

SoundMixer::~SoundMixer()
{
	if (soundEnabled)
	{
		SDL_PauseAudio(1);
		SDL_CloseAudio();
		speex_decoder_destroy(speexDecoderState);
	}
	
	for (size_t i=0; i<tracks.size(); i++)
	{
		ov_clear(tracks[i]);
		delete tracks[i];
	}
}

int SoundMixer::loadTrack(const char *name)
{
	FILE* fp = Toolkit::getFileManager()->openFP(name);
	if (!fp)
	{
		std::cerr << "SoundMixer : File " << name << " can't be opened for reading." << std::endl;
		return -1;
	}

	OggVorbis_File *oggFile = new OggVorbis_File;
	if (ov_open(fp, oggFile, NULL, 0) < 0)
	{
		std::cerr << "SoundMixer : File " << name << " does not appear to be an Ogg bitstream." << std::endl;
		fclose(fp);
		return -2;
	}

	SDL_LockAudio();
	tracks.push_back(oggFile);
	SDL_UnlockAudio();
	
	return (int)tracks.size()-1;
}

void SoundMixer::setNextTrack(unsigned i, bool earlyChange)
{
	if ((soundEnabled) && (i<tracks.size()))
	{
		SDL_LockAudio();
		
		// Select next tracks
		if (actTrack >= 0)
			nextTrack = i;
		else
			nextTrack = actTrack = i;
		
		// Select mode
		if (mode == MODE_STOPPED)
		{
			SDL_PauseAudio(0);
			mode = MODE_START;
		}
		else if (earlyChange)
		{
			mode = MODE_EARLY_CHANGE;
		}
		
		SDL_UnlockAudio();
	}
}

void SoundMixer::setVolume(unsigned volume, bool mute)
{
	if (!soundEnabled)
	{
		if (!mute)
		{
			openAudio();
			this->volume = volume;
		}
		else
			return;
	}
	else if (mute)
	{
		this->volume = 0;
	}
	else
		this->volume = volume;
}

void SoundMixer::stopMusic(void)
{
	mode = MODE_STOP;
}

void SoundMixer::addVoiceData(boost::shared_ptr<OrderVoiceData> order)
{
	if (soundEnabled)
	{
		SDL_LockAudio();
		// get or create the voice
		PlayerVoice &pv = voices[order->sender];
		// insert 200 ms silence to let packets come if we aer the first
		if (pv.voiceDatas.empty())
		{
			for (size_t j=0; j<2000; j++)
				pv.voiceDatas.push(0);
			pv.voiceVal0 = pv.voiceVal1 = 0;
			pv.voiceSubIndex = 0;
		}
		
		SpeexBits bits;
		speex_bits_init(&bits);
		speex_bits_read_from(&bits, (char *)order->getFramesData(), order->framesDatasLength);
		// read each frame
		for (size_t i=0; i<order->frameCount; i++)
		{
			float floatBuffer[SPEEX_FRAME_SIZE];
			speex_decode(speexDecoderState, &bits, floatBuffer);
			
			for (size_t j=0; j<SPEEX_FRAME_SIZE; j++)
				pv.voiceDatas.push(floatBuffer[j]);
		}
		speex_bits_destroy(&bits);
		
		SDL_UnlockAudio();
	}
}
