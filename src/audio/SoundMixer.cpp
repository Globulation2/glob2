// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <GameplayRecording.h>
#include "SoundMixer.h"
#include "Order.h"
#include <Toolkit.h>
#include <FileManager.h>
using namespace GAGCore;
#include <iostream>
#include <assert.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>

#ifdef HAVE_CONFIG_H
	#include <glob2/BuildConfig.h>
#endif

#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_NO_VOICE)
#include <speex/speex.h>
#endif

#include <SDL3/SDL_endian.h>

#ifdef WIN32
#include <malloc.h>
#endif

//! Length of a music fade in Sint16 samples, both channels interleaved.
//! Independent of the device buffer size: a fade spans as many callbacks as
//! it takes to cover this many samples.
#define FADE_SAMPLE_COUNT SoundMixer::FadeSampleCount
//! Maximum frames per mixing chunk, independent of the playback device buffer.
#define DEVICE_FRAME_COUNT 1024
#define INTERPOLATION_RANGE 65535
#define INTERPOLATION_BITS 16
#define SPEEX_FRAME_SIZE 160
//! Voice samples queued per player beyond which new packets are dropped: about ten
//! seconds (2000 samples are 200 ms). One speaker talking in real time stays far
//! below it; a client flooding voice orders would otherwise grow the queue forever.
#define MAX_VOICE_BACKLOG_SAMPLES 100000

static int interpolationTable[FADE_SAMPLE_COUNT];

static void initInterpolationTable(void)
{
	double l = static_cast<double>(FADE_SAMPLE_COUNT-1);
	double m = INTERPOLATION_RANGE;
	double a = - (2) / (l * l * l);
	double b = (3) / (l * l);
	for (unsigned i=0; i<FADE_SAMPLE_COUNT; i++)
	{
		double x = static_cast<double>(i);
		double v = m * (a * (x * x * x) + b * (x* x));
		interpolationTable[i] = static_cast<int>(v);
	}
}

// Callbacks live in the application CRT, including on Windows. A successful
// open owns fp; failures close it here (opusfile does not close on open failure).
static OggOpusFile *openMusicFile(FILE *fp)
{
	static const OpusFileCallbacks callbacks = {
		[](void *source, unsigned char *data, int size) -> int {
			auto *file = static_cast<FILE *>(source);
			const auto count = fread(data, 1, size, file);
			return ferror(file) ? -1 : static_cast<int>(count);
		},
		[](void *source, opus_int64 offset, int origin) -> int {
#ifdef _WIN32
			return _fseeki64(static_cast<FILE *>(source), offset, origin);
#else
			return fseeko(static_cast<FILE *>(source), offset, origin);
#endif
		},
		[](void *source) -> opus_int64 {
#ifdef _WIN32
			return _ftelli64(static_cast<FILE *>(source));
#else
			return ftello(static_cast<FILE *>(source));
#endif
		},
		[](void *source) -> int { return fclose(static_cast<FILE *>(source)); }
	};
	int error = 0;
	auto *track = op_open_callbacks(fp, &callbacks, nullptr, 0, &error);
	if (!track)
	{
		fclose(fp);
		return nullptr;
	}
	if (!op_seekable(track) || op_link_count(track) != 1 ||
		op_channel_count(track, 0) != 2 || op_pcm_total(track, -1) <= 0)
	{
		op_free(track);
		return nullptr;
	}
	return track;
}

// opusfile 0.12's short forward seek can retain PCM buffered before the seek.
// Reset that buffer first; then PCM seek retains pre-roll and trimmed positions.
// This is necessary when switching into a mood previously played in this loop.
static bool seekMusic(OggOpusFile *track, ogg_int64_t frame)
{
    return track && frame >= 0 && op_raw_seek(track, 0) == 0 && op_pcm_seek(track, frame) == 0;
}

// Fill native-endian, interleaved stereo samples. Decoder returns frames, not
// interleaved sample counts. Bound holes and EOFs without progress so malformed
// streams cannot spin indefinitely on the audio thread.
static void readMusic(SoundMixer &mixer, Sint16 *output, int count, int &index, bool advance)
{
	int failures = 0;
	while (count > 0)
	{
		auto *track = index >= 0 && static_cast<size_t>(index) < mixer.tracks.size()
			? mixer.tracks[index] : nullptr;
		if (!track) break;
		const int frames = op_read_stereo(track, output, count);
		if (frames > 0)
		{
			output += frames * 2;
			count -= frames * 2;
			failures = 0;
			continue;
		}
		if (++failures > 4) break;
		if (frames == OP_HOLE) continue;
		if (frames < 0) break;
		if (advance) index = mixer.nextTrack;
		track = index >= 0 && static_cast<size_t>(index) < mixer.tracks.size()
			? mixer.tracks[index] : nullptr;
		if (!seekMusic(track, 0)) break;
	}
	if (count > 0)
	{
		std::fill_n(output, count, 0);
		std::cerr << "SoundMixer: unable to decode/loop music track " << index << std::endl;
	}
}

//! Ramp value for the i-th sample of a callback starting at fadePos.
static inline int fadeValue(unsigned fadePos, unsigned i)
{
	unsigned p = fadePos + i;
	return interpolationTable[p < FADE_SAMPLE_COUNT ? p : FADE_SAMPLE_COUNT-1];
}

void SoundMixer::handleVoiceInsertion(int *outputSample, int voicevol)
{
	// if no more voice
	if (voices.empty())
		return;
	
	float value = 0;
	for (std::map<int, PlayerVoice>::iterator i = voices.begin(); i != voices.end();)
	{
		bool exhausted = false;
		value += i->second.advanceOutputSample(exhausted);
		if (exhausted)
			i = voices.erase(i);
		else
			++i;
	}
	// saturate
	value = std::min(value, 32767.0f);
	value = std::max(value, -32767.0f);
	value = (value * voicevol)/256;
	// write sample
	*outputSample = (static_cast<int>(3.0f * (value)) + (*outputSample)) / 4;
}

void mixaudio(void *voidMixer, Uint8 *stream, int len)
{
	SoundMixer *mixer = static_cast<SoundMixer *>(voidMixer);
	unsigned nsamples = static_cast<unsigned>(len) >> 1;
	Sint16 *mix = reinterpret_cast<Sint16 *>(stream);
	int musicvol = static_cast<int>(mixer->musicVolume);
	int voicevol = static_cast<int>(mixer->voiceVolume);

	assert(mixer->actTrack >= 0);
	assert(mixer->mode != SoundMixer::MODE_STOPPED);
	assert(nsamples);

	if (mixer->mode == SoundMixer::MODE_EARLY_CHANGE)
	{
		Sint16 *track0 = reinterpret_cast<Sint16 *>(alloca(len));
		Sint16 *track1 = reinterpret_cast<Sint16 *>(alloca(len));
		// Align moods once at fade start, using Opus's trimmed 48 kHz timeline.
		bool aligned = true;
		if (mixer->fadePos == 0)
			aligned = seekMusic(mixer->tracks[mixer->nextTrack],
				op_pcm_tell(mixer->tracks[mixer->actTrack]));
		readMusic(*mixer, track0, nsamples, mixer->actTrack, false);
		if (aligned)
			readMusic(*mixer, track1, nsamples, mixer->nextTrack, false);
		else
		{
			std::fill_n(track1, nsamples, 0);
			std::cerr << "SoundMixer: unable to align incoming music track" << std::endl;
		}

		// mix
		for (unsigned i=0; i<nsamples; i++)
		{
			int t0 = track0[i];
			int t1 = track1[i];
			int intI = fadeValue(mixer->fadePos, i);
			int val = (intI*t1+((INTERPOLATION_RANGE-intI)*t0))>>INTERPOLATION_BITS;
			val = (val * musicvol)>>8;
			mixer->handleVoiceInsertion(&val, voicevol);
			mix[i] = val;
		}

		// clear change
		mixer->fadePos += nsamples;
		if (mixer->fadePos >= FADE_SAMPLE_COUNT)
		{
			mixer->fadePos = 0;
			mixer->actTrack = mixer->nextTrack;
			if (mixer->pendingTrack >= 0)
			{
				// a change asked for while this fade was running: cross into it
				// from the track that just landed, so the mix stays continuous.
				// Staying in MODE_EARLY_CHANGE with fadePos == 0 re-aligns the
				// incoming track on the next callback.
				mixer->nextTrack = mixer->pendingTrack;
				mixer->pendingTrack = -1;
			}
			else
				mixer->mode = SoundMixer::MODE_NORMAL;
		}
	}
	else
	{
		readMusic(*mixer, mix, nsamples, mixer->actTrack, true);

		// volume & fading
		if (mixer->mode == SoundMixer::MODE_NORMAL)
		{
			if (musicvol != 255)
				for (unsigned i=0; i<nsamples; i++)
				{
					int t = mix[i];
					t = (t * musicvol) >> 8;
					mixer->handleVoiceInsertion(&t, voicevol);
					mix[i] = t;
				}
		}
		else if (mixer->mode == SoundMixer::MODE_START)
		{
			for (unsigned i=0; i<nsamples; i++)
			{
				int t = mix[i];
				t = (fadeValue(mixer->fadePos, i)*t) >> INTERPOLATION_BITS;
				t = (t * musicvol) >> 8;
				mixer->handleVoiceInsertion(&t, voicevol);
				mix[i] = t;
			}
			mixer->fadePos += nsamples;
			if (mixer->fadePos >= FADE_SAMPLE_COUNT)
			{
				mixer->fadePos = 0;
				mixer->mode = SoundMixer::MODE_NORMAL;
			}
		}
		else if (mixer->mode == SoundMixer::MODE_STOP)
		{
			for (unsigned i=0; i<nsamples; i++)
			{
				int t = mix[i];
				int intI = fadeValue(mixer->fadePos, i);
				t = ((INTERPOLATION_RANGE-intI)*t) >> INTERPOLATION_BITS;
				t = (t * musicvol) >> 8;
				mixer->handleVoiceInsertion(&t, voicevol);
				mix[i] = t;
			}
			mixer->fadePos += nsamples;
			if (mixer->fadePos >= FADE_SAMPLE_COUNT)
			{
				mixer->fadePos = 0;
				mixer->mode = SoundMixer::MODE_STOPPED;
				// The stream callback emits silence once the fade reaches MODE_STOPPED.
			}
		}
	}
}

static void SDLCALL streamAudio(void *userdata, SDL_AudioStream *stream, int additional, int)
{
    auto *mixer = static_cast<SoundMixer *>(userdata);
    // Bound stack and decoding work; SDL3 may ask for several device buffers.
    alignas(Sint16) std::array<Uint8, DEVICE_FRAME_COUNT * 4> buffer;
	// New PCM follows any input already queued for playback. One refill may span
	// several chunks; timestamp it once and advance by samples, never by wall time.
	constexpr auto bytesPerSecond =
		GAGCore::Recording::AudioSampleRate * GAGCore::Recording::AudioChannels * sizeof(Sint16);
	const auto queued = std::max(0, SDL_GetAudioStreamQueued(stream));
	const auto refillStart =
		GAGCore::Recording::timestamp() + std::int64_t(queued) * 1000000 / bytesPerSecond;
	std::int64_t refillFrames = 0;
	while (additional > 0) {
        const int count = std::min(additional, static_cast<int>(buffer.size()));
        const int aligned = (count + 3) & ~3;
        if (mixer->mode == SoundMixer::MODE_STOPPED || mixer->actTrack < 0)
            std::fill(buffer.begin(), buffer.begin() + aligned, 0);
        else
            mixaudio(mixer, buffer.data(), aligned);
		GAGCore::Recording::recorder().audio(
			reinterpret_cast<const std::int16_t *>(buffer.data()), unsigned(aligned) / 2,
			refillStart + refillFrames * 1000000 / GAGCore::Recording::AudioSampleRate);
		refillFrames += aligned / (GAGCore::Recording::AudioChannels * sizeof(Sint16));
		if (!SDL_PutAudioStreamData(stream, buffer.data(), aligned)) return;
        additional -= aligned;
    }
}

void SoundMixer::openAudio(void)
{
	// Initialize recording callback storage before the audio device starts.
	GAGCore::Recording::recorder();
	const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, GAGCore::AudioSampleRate};
    audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, streamAudio, this);
    soundEnabled = audioStream != nullptr;
    if (!soundEnabled) {
        std::cerr << "SoundMixer: Unable to open audio: " << SDL_GetError() << std::endl;
        return;
    }
    mode = MODE_STOPPED;

#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_NO_VOICE)
	// Open Speex decoder
#ifdef _MSC_VER
	// workaround for vcpkg bug #2292 which seems to be broken again.
	const SpeexMode *speex_nb_mode = speex_lib_get_mode(SPEEX_MODEID_NB);
	speexDecoderState = speex_decoder_init(speex_nb_mode);
#else
	speexDecoderState = speex_decoder_init(&speex_nb_mode);
#endif
	int tmp = 1;
	speex_decoder_ctl(speexDecoderState, SPEEX_SET_ENH, &tmp);
#endif
	
}

SoundMixer::SoundMixer(unsigned musicvol, unsigned voicevol, bool mute)
{
	actTrack = -1;
	nextTrack = -1;
	this->musicVolume = musicvol;
	this->voiceVolume = voicevol;
	mode = MODE_STOPPED;
	fadePos = 0;
	pendingTrack = -1;
	soundEnabled = false;
	speexDecoderState = NULL;
	
	initInterpolationTable();
	
	// While muted there is nothing to play, so leave the device closed; the
	// audio thread and its Opus decoding never start, and there is nothing for
	// SDL_DestroyAudioStream to wait for at exit. setVolume() opens it on unmute.
	if (mute)
	{
		this->musicVolume = 0;
		this->voiceVolume = 0;
		return;
	}
	openAudio();
}

SoundMixer::~SoundMixer()
{
	if (soundEnabled)
	{
		SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(audioStream));
		SDL_DestroyAudioStream(audioStream);
        audioStream = nullptr;
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_NO_VOICE)
		speex_decoder_destroy(speexDecoderState);
#endif
	}
	
	for (size_t i=0; i<tracks.size(); i++)
	{
		if (!tracks[i])
			continue;
		op_free(tracks[i]);
	}
}

int SoundMixer::loadTrack(const std::string name, int index)
{
	FILE* fp = Toolkit::getFileManager()->openFP(name);
	if (!fp)
	{
		std::cerr << "SoundMixer : File " << name << " can't be opened for reading." << std::endl;
		return -1;
	}

	auto *track = openMusicFile(fp);
	if (!track)
	{
		std::cerr << "SoundMixer: File " << name << " is not a usable stereo Ogg Opus stream." << std::endl;
		return -2;
	}

	if (audioStream) SDL_LockAudioStream(audioStream);
	if (index >= 0 && index< (int)tracks.size())
	{
		if (tracks[index])
		{
			op_free(tracks[index]);
		}
		tracks[index] = track;
	}
	else if (index >= 0)
	{
		// A slot whose earlier tracks are missing (the browser installs the
		// menu music after startup) keeps its index; empty slots never play.
		tracks.resize(index + 1, nullptr);
		tracks[index] = track;
	}
	else
	{
		tracks.push_back(track);
		index = (int)tracks.size()-1;
	}
	if (audioStream) SDL_UnlockAudioStream(audioStream);
	
	return index;
}

// The track selection is kept even while the device is closed, so setVolume()
// can resume it when the user unmutes.
void SoundMixer::setNextTrack(unsigned i, bool earlyChange)
{
	if (i >= tracks.size() || !tracks[i])
		return;

	bool resume = false;
	if (audioStream) SDL_LockAudioStream(audioStream);

	// A fade now spans many callbacks, so a track change can be asked for while
	// one is still running — GameMusicController can emit on consecutive 40 ms
	// ticks. Restarting the fade would cut the incoming track off mid-mix, so
	// queue the request and let mixaudio() start it when this fade lands.
	if (soundEnabled && mode == MODE_EARLY_CHANGE)
	{
		pendingTrack = static_cast<int>(i);
		if (audioStream) SDL_UnlockAudioStream(audioStream);
		return;
	}

	// Select next tracks. While the device is closed nothing is playing, so the
	// selection is both the current and the next track: leaving actTrack at the
	// first track ever selected would make setVolume() resume that one on
	// unmute, whatever was asked for since.
	if (soundEnabled && actTrack >= 0)
		nextTrack = i;
	else
		nextTrack = actTrack = i;

	// Select mode
	if (soundEnabled)
	{
		if (mode == MODE_STOPPED)
		{
			fadePos = 0;
			pendingTrack = -1;
			resume = true;
			mode = MODE_START;
		}
		else if (earlyChange)
		{
			fadePos = 0;
			pendingTrack = -1;
			mode = MODE_EARLY_CHANGE;
		}
	}

	if (audioStream) SDL_UnlockAudioStream(audioStream);
	if (resume) SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(audioStream));
}

int SoundMixer::loadTrack(const std::string name, MusicTrack track)
{
	return loadTrack(name, static_cast<int>(track));
}

std::vector<std::string> SoundMixer::getMusicSets()
{
	auto *files = Toolkit::getFileManager();
	files->initDirectoryListing("data/zik/", "", true);
	std::vector<std::string> result;
	std::string name;
	while (!(name = files->getNextDirectoryEntry()).empty())
	{
		if (name == "." || name == ".." || name.find_first_of("/\\\r\n=") != std::string::npos)
			continue;
		const std::string directory = "data/zik/" + name;
		if (!files->isDir(directory))
			continue;
		bool complete = true;
		for (int i = 1; i <= 3; ++i)
		{
			FILE *file = files->openFP(directory + "/a" + std::to_string(i) + ".opus");
			if (file)
				fclose(file);
			else
				complete = false;
		}
		if (complete)
			result.push_back(name);
	}
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

std::string SoundMixer::musicSetLabel(const std::string& name)
{
	std::string label = name;
	bool capital = true;
	for (char& c : label)
	{
		if (c == '-' || c == '_')
			c = ' ';
		else if (capital)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		capital = (c == ' ');
	}
	return label;
}

namespace
{
	struct OggTrackCloser
	{
		void operator()(OggOpusFile *track) const { op_free(track); }
	};
	using OggTrack = std::unique_ptr<OggOpusFile, OggTrackCloser>;
	using MusicSetTrio = std::array<OggTrack, 3>;

	//! Open the three in-game tracks of `name`. They must all be 48 kHz decoded stereo,
	//! one logical stream, and of equal length, so the mixer can cross over between
	//! moods at the same position. On failure `trio` holds no usable set.
	bool openMusicSet(const std::string& name, MusicSetTrio& trio)
	{
		ogg_int64_t frames = 0;
		for (unsigned i = 0; i < trio.size(); ++i)
		{
			const std::string path = "data/zik/" + name + "/a" + std::to_string(i + 1) + ".opus";
			FILE *file = Toolkit::getFileManager()->openFP(path);
			if (!file)
				return false;
			trio[i].reset(openMusicFile(file));
			if (!trio[i])
			{
				std::cerr << "SoundMixer: music set " << name << " has an unusable " << path << std::endl;
				return false;
			}
			const auto length = op_pcm_total(trio[i].get(), -1);
			if (i > 0 && length != frames)
			{
				std::cerr << "SoundMixer: music set " << name << " has mismatched lengths" << std::endl;
				return false;
			}
			frames = length;
		}
		return true;
	}
}

bool SoundMixer::selectMusicSet(const std::string& preference)
{
	auto candidates = getMusicSets();
	if (preference.empty())
	{
		// Discovery only checks that the files exist, so a random pick tries the
		// sets in a random order until one opens. This is menu randomness (rand()),
		// never the synchronized simulation generator.
		for (size_t i = candidates.size(); i > 1; --i)
			std::swap(candidates[i - 1], candidates[static_cast<size_t>(rand()) % i]);
	}
	else if (std::find(candidates.begin(), candidates.end(), preference) != candidates.end())
		candidates.assign(1, preference);
	else
		return false;

	MusicSetTrio replacement;
	std::string name;
	for (const auto& candidate : candidates)
	{
		if (candidate == activeMusicSet && tracks.size() >= static_cast<unsigned>(MusicTrack::Count))
			return true;
		MusicSetTrio opened;
		if (openMusicSet(candidate, opened))
		{
			replacement = std::move(opened);
			name = candidate;
			break;
		}
	}
	if (name.empty())
		return false;

	if (audioStream) SDL_LockAudioStream(audioStream);
	const unsigned first = static_cast<unsigned>(MusicTrack::InGameDefault);
	if (tracks.size() < static_cast<unsigned>(MusicTrack::Count))
		tracks.resize(static_cast<unsigned>(MusicTrack::Count), nullptr);
	for (unsigned i = 0; i < replacement.size(); ++i)
	{
		auto *old = tracks[first + i];
		tracks[first + i] = replacement[i].release();
		replacement[i].reset(old);
	}
	activeMusicSet = name;
	if (actTrack >= static_cast<int>(first))
	{
		// Different sets need not share tempo or length. Start the same mood anew.
		if (pendingTrack >= static_cast<int>(first))
			actTrack = pendingTrack;
		else if (mode == MODE_EARLY_CHANGE && nextTrack >= static_cast<int>(first))
			actTrack = nextTrack;
		nextTrack = actTrack;
		if (mode != MODE_STOPPED)
			mode = MODE_START;
	}
	fadePos = 0;
	pendingTrack = -1;
	if (audioStream) SDL_UnlockAudioStream(audioStream);
	std::cerr << "selecting music dir " << name << std::endl;
	return true;
}

void SoundMixer::setNextTrack(MusicTrack track, bool earlyChange)
{
	setNextTrack(static_cast<unsigned>(track), earlyChange);
}

// All writes to musicVolume/voiceVolume must hold SDL_LockAudioStream — mixaudio()
// reads them on the audio thread. openAudio() is called *before* taking the
// lock: SDL_OpenAudioDeviceStream opens the device in the paused state, so the callback
// cannot fire until SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(audioStream)) is called from setNextTrack().
void SoundMixer::setVolume(unsigned musicVolume, unsigned voiceVolume, bool mute)
{
	bool justOpened = false;
	if (!soundEnabled)
	{
		if (mute)
			return;
		openAudio();
		justOpened = soundEnabled;
	}

	if (audioStream) SDL_LockAudioStream(audioStream);
	if (mute)
	{
		this->musicVolume = 0;
		this->voiceVolume = 0;
	}
	else
	{
		this->musicVolume = musicVolume;
		this->voiceVolume = voiceVolume;
	}
	if (audioStream) SDL_UnlockAudioStream(audioStream);

	// start the track that was selected while the device was closed, once the
	// volumes are in place so the fade-in is not silent
	if (justOpened && actTrack >= 0)
		setNextTrack(static_cast<unsigned>(actTrack));
}

// mode is read by mixaudio() on the audio thread; the write must hold the lock.
void SoundMixer::stopMusic(void)
{
	if (audioStream) SDL_LockAudioStream(audioStream);
	fadePos = 0;
	pendingTrack = -1;
	mode = MODE_STOP;
	if (audioStream) SDL_UnlockAudioStream(audioStream);
}



bool SoundMixer::isPlayerTransmittingVoice(int player)
{
	if (audioStream) SDL_LockAudioStream(audioStream);
	if(voices.find(player) != voices.end())
	{
		if (audioStream) SDL_UnlockAudioStream(audioStream);
		return true;
	}
	if (audioStream) SDL_UnlockAudioStream(audioStream);
	return false;
}


void SoundMixer::addVoiceData(std::shared_ptr<OrderVoiceData> order)
{
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_NO_VOICE)
	if (soundEnabled)
	{
		if (audioStream) SDL_LockAudioStream(audioStream);
		// get or create the voice
		PlayerVoice &pv = voices[order->sender];
		if (pv.voiceData.size() >= MAX_VOICE_BACKLOG_SAMPLES)
		{
			if (audioStream) SDL_UnlockAudioStream(audioStream);
			return;
		}
		// insert 200 ms silence to let packets come if we aer the first
		if (pv.voiceData.empty())
		{
			for (size_t j=0; j<2000; j++)
				pv.voiceData.push(0);
			pv.voiceVal0 = pv.voiceVal1 = 0;
			pv.voiceSubIndex = 0;
		}
		
		SpeexBits bits;
		speex_bits_init(&bits);
		speex_bits_read_from(&bits, (char *)order->getFramesData(), order->framesDataLength);
		// read each frame
		for (size_t i=0; i<order->frameCount; i++)
		{
			float floatBuffer[SPEEX_FRAME_SIZE];
			speex_decode(speexDecoderState, &bits, floatBuffer);
			
			for (size_t j=0; j<SPEEX_FRAME_SIZE; j++)
				pv.voiceData.push(floatBuffer[j]);
		}
		speex_bits_destroy(&bits);
		
		if (audioStream) SDL_UnlockAudioStream(audioStream);
	}
#endif
}
