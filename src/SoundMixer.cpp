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
#include <iostream>
#include <assert.h>
#include <iostream>

#define SAMPLE_COUNT_PER_SLICE 4096

static int interpolationTable[SAMPLE_COUNT_PER_SLICE];

static void initInterpolationTable(void)
{
	double l = static_cast<double>(SAMPLE_COUNT_PER_SLICE-1);
	double m = 65535;
	double a = - (2 * m * m) / (l * l * l);
	double b = (3 * m * m) / (l * l);
	for (unsigned i=0; i<SAMPLE_COUNT_PER_SLICE; i++)
	{
		double x = static_cast<double>(i);
		double v = a * (x * x * x) + b * (x* x);
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

	if (mixer->earlyChange)
	{
		Sint16 *track0 = reinterpret_cast<Sint16 *>(alloca(len));
		Sint16 *track1 = reinterpret_cast<Sint16 *>(alloca(len));
		long rest;
		char *p;

		// read first ogg
		rest = len;
		p = reinterpret_cast<char *>(track0);
		while(rest > 0)
		{
			int bs;
			long ret = ov_read(mixer->tracks[mixer->actTrack], p, rest, 0, 2, 1, &bs);
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
		ogg_int64_t firstPos = ov_pcm_tell(mixer->tracks[mixer->actTrack]);
		ov_pcm_seek(mixer->tracks[mixer->nextTrack], firstPos);
		rest = len;
		p = reinterpret_cast<char *>(track1);
		while(rest > 0)
		{
			int bs;
			long ret = ov_read(mixer->tracks[mixer->nextTrack], p, rest, 0, 2, 1, &bs);
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
			int intI = interpolationTable[i];
			int val = (intI*track0[i]+(65535-intI*track1[1]))>>16;
			mix[i] = (val * vol)>>8;
		}

		// clear change
		mixer->actTrack = mixer->nextTrack;
		mixer->earlyChange = false;
	}
	else
	{
		// read ogg
		long rest = len;
		char *p = reinterpret_cast<char *>(mix);
		while(rest > 0)
		{
			int bs;
			long ret = ov_read(mixer->tracks[mixer->actTrack], p, rest, 0, 2, 1, &bs);
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

		// volume
		for (unsigned i=0; i<nsamples; i++)
		{
			mix[i] = (mix[i] * vol)>>8;
		}
	}
}

SoundMixer::SoundMixer(unsigned volume)
{
	SDL_AudioSpec as;
	// Set 16-bit stereo audio at 44Khz
	as.freq = 44100;
	as.format = AUDIO_S16SYS;
	as.channels = 2;
	as.samples = SAMPLE_COUNT_PER_SLICE;
	as.callback = mixaudio;
	as.userdata = this;

	actTrack = -1;
	nextTrack = -1;
	earlyChange = false;
	this->volume = volume;

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
	}

	initInterpolationTable();
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
	if (!soundEnabled)
		return -1;

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

	tracks.push_back(oggFile);
	return tracks.size()-1;
}

void SoundMixer::setNextTrack(unsigned i, bool earlyChange)
{
	if ((soundEnabled) && (i<tracks.size()))
	{
		if (actTrack >= 0)
			nextTrack = i;
		else
			actTrack = i;

		this->earlyChange = earlyChange;

		if ((SDL_GetAudioStatus() != SDL_AUDIO_PLAYING) && (volume))
			SDL_PauseAudio(0);
	}
}

void SoundMixer::setVolume(unsigned volume)
{
	unsigned lastVolume = this->volume;
	this->volume = volume;
	if (lastVolume != volume)
	{
		if (lastVolume == 0)
			SDL_PauseAudio(0);
		if (volume == 0)
			SDL_PauseAudio(1);
	}
}
