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
			mix[i] = (intI*track0[i]+(65535-intI*track1[1]))>>16;
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
	}
}

SoundMixer::SoundMixer()
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

		if (SDL_GetAudioStatus() != SDL_AUDIO_PLAYING)
			SDL_PauseAudio(0);
	}
}
