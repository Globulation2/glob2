/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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
#include <Toolkit.h>
#include <FileManager.h>
using namespace GAGCore;
#include <iostream>
#include <assert.h>
#include <iostream>

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_endian.h>
#else
#include <Types.h>
#endif


#define SAMPLE_COUNT_PER_SLICE 8192*8
#define INTERPOLATION_RANGE 65535
#define INTERPOLATION_BITS 16

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
			mix[i] = (val * vol)>>8;
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
					mix[i] = (t * vol) >> 8;
				}
		}
		else if (mixer->mode == SoundMixer::MODE_START)
		{
			for (unsigned i=0; i<nsamples; i++)
			{
				int t = mix[i];
				t = (interpolationTable[i]*t) >> INTERPOLATION_BITS;
				mix[i] = (t * vol) >> 8;
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
				mix[i] = (t * vol) >> 8;
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
}

SoundMixer::SoundMixer(unsigned volume)
{
	actTrack = -1;
	nextTrack = -1;
	this->volume = volume;
	mode = MODE_STOPPED;
	
	initInterpolationTable();
	
	if (volume == 0)
	{
		soundEnabled = false;
		std::cout << "SoundMixer : No volume, audio has been disabled !" << std::endl;
		return;
	}
	else
	{
		openAudio();
	}
}

SoundMixer::~SoundMixer()
{
	if (soundEnabled)
	{
		SDL_PauseAudio(1);

		for(unsigned i=0; i<tracks.size(); i++)
		{
			ov_clear(tracks[i]);
			delete tracks[i];
		}
		
		SDL_CloseAudio();
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
	if ((soundEnabled) && (volume) && (i<tracks.size()))
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

void SoundMixer::setVolume(unsigned volume)
{
	unsigned lastVolume = this->volume;
	this->volume = volume;
	if (lastVolume != volume)
	{
		if (lastVolume == 0)
		{
			if (!soundEnabled)
				openAudio();
			if (actTrack>=0)
			{
				SDL_PauseAudio(0);
				mode = MODE_START;
			}
		}
		if (volume == 0)
			stopMusic();
	}
}

void SoundMixer::stopMusic(void)
{
	mode = MODE_STOP;
}


