/*
  This file is part of Globulation 2, a free software real-time strategy game
  http://www.globulation2.org
  Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charriere and other contributors
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include "VoiceRecorder.h"
#include <assert.h>
#include <stdio.h>
#include "Order.h"
#include "Utilities.h"

#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif

#ifdef SPEEX_DIR
#include <speex/speex.h>
#else
#include <speex.h>
#endif

#ifdef WIN32
	// Windows audio input include
	#undef AUDIO_RECORDER_OSS
	#define STOP_RECORDING_TIMEOUT 3000
#endif

#ifdef __APPLE__
	// Mac OS X stuff here
	#undef AUDIO_RECORDER_OSS
	#define STOP_RECORDING_TIMEOUT 3000
#endif

#ifdef AUDIO_RECORDER_OSS
	#include <sys/time.h>
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <sys/ioctl.h>
	#include <sys/soundcard.h>
	#include <unistd.h>
	#include <fcntl.h>
	#define STOP_RECORDING_TIMEOUT 3000
#endif

#define MAX_VOICE_MULTI_FRAME_LENGTH 256
#define MAX_VOICE_MULTI_FRAME_SAMPLE_COUNT 2400
#define SPEEX_FRAME_SIZE 160

int record(void *pointer)
{
	VoiceRecorder *voiceRecorder = static_cast<VoiceRecorder *>(pointer);
	// bytes buffer for read
	short buffer[SPEEX_FRAME_SIZE];
	size_t toReadLength = 2*SPEEX_FRAME_SIZE;
	// bits buffer for compressed datas
	SpeexBits bits;
	speex_bits_init(&bits);
	
	while (voiceRecorder->recordThreadRun)
	{
		while (!voiceRecorder->recordingNow)
		{
			// TODO : ugly, use semaphore
			SDL_Delay(1);
			
			if (!voiceRecorder->recordThreadRun)
				goto abortRecordThread;
		}
		
		// open sound device, plateforme dependant
		#ifdef WIN32
		fprintf(stderr, "VoiceRecorder::record : no audio input support for Win32 yet. Voice chat will be disabled. Contributions welcome\n");
		// TODO : windows code
		break; // while no code, break
		#endif
		#ifdef __APPLE__
		fprintf(stderr, "VoiceRecorder::record : no audio input support for Mac yet. Voice chat will be disabled. Contributions welcome\n");
		// TODO : Mac OS X code
		break; // while no code, break
		#endif
		#ifdef AUDIO_RECORDER_OSS
		// OSS, Open Sound System
		int dsp;
		int rate = 8000;
		int channels = 1;
		
		#if SDL_BYTEORDER == SDL_LIL_ENDIAN
		int format = AFMT_S16_LE;
		#else
		int format = AFMT_S16_BE;
		#endif
		
		dsp = open("/dev/dsp", O_RDONLY);
		if (dsp == -1)
		{
			fprintf(stderr, "VoiceRecorder::record : can't open /dev/dsp. Voice chat will be disabled.\n");
			break;
		}
		
		ioctl(dsp, SNDCTL_DSP_SPEED , &rate);
		ioctl(dsp, SNDCTL_DSP_CHANNELS , &channels);
		ioctl(dsp, SOUND_PCM_READ_RATE , &rate);
		ioctl(dsp, SNDCTL_DSP_SETFMT , &format);
		#endif
		#if !defined(AUDIO_RECORDER_OSS) && !defined(WIN32) && !defined(__APPLE__)
		fprintf(stderr, "VoiceRecorder::record : no audio input support for your system and system unknown. Voice chat will be disabled.\n");
		break;
		#endif
		
		size_t totalRead = 0;
		size_t frameCount = 0;
		
		while (voiceRecorder->recordingNow || (voiceRecorder->stopRecordingTimeout > 0))
		{
			// read
			#ifdef WIN32
			// TODO : windows code
			#endif
			#ifdef __APPLE__
			// TODO : Mac OS X code
			#endif
			#ifdef AUDIO_RECORDER_OSS
			Utilities::read(dsp, buffer, toReadLength);
			totalRead += toReadLength;
			voiceRecorder->stopRecordingTimeout -= toReadLength;
			#endif
			
			// transforms samples to float
			float floatBuffer[SPEEX_FRAME_SIZE];
			for (size_t i=0; i<SPEEX_FRAME_SIZE; i++)
         		floatBuffer[i] = buffer[i];
			
			// encode
			speex_encode(voiceRecorder->speexEncoderState, floatBuffer, &bits);
			frameCount++;
			
			// if overflow conditions, send
			int byteLength = speex_bits_nbytes(&bits);
			if (byteLength > MAX_VOICE_MULTI_FRAME_LENGTH || totalRead > MAX_VOICE_MULTI_FRAME_SAMPLE_COUNT)
			{
				OrderVoiceData *order = new OrderVoiceData(0, byteLength, frameCount, NULL);
				int nbBytes = speex_bits_write(&bits, (char *)order->getFramesData(), byteLength);
				assert(byteLength == nbBytes);
				
				SDL_LockMutex(voiceRecorder->ordersMutex);
				voiceRecorder->orders.push(order);
				SDL_UnlockMutex(voiceRecorder->ordersMutex);
				
				// reset
				totalRead = 0;
				frameCount = 0;
				speex_bits_reset(&bits);
			}
		}
		
		// create order with resting bits
		int byteLength = speex_bits_nbytes(&bits);
		if (byteLength > 0)
		{
			OrderVoiceData *order = new OrderVoiceData(0, byteLength, frameCount, NULL);
			int nbBytes = speex_bits_write(&bits, (char *)order->getFramesData(), byteLength);
			assert(byteLength == nbBytes);
			
			SDL_LockMutex(voiceRecorder->ordersMutex);
			voiceRecorder->orders.push(order);
			SDL_UnlockMutex(voiceRecorder->ordersMutex);
			
			speex_bits_reset(&bits);
		}
		
		// close sound device, plateforme dependant
		#ifdef WIN32
		// TODO : windows code
		#endif
		#ifdef __APPLE__
		// TODO : Mac OS X code
		#endif
		#ifdef AUDIO_RECORDER_OSS
		close(dsp);
		#endif
	}
	
abortRecordThread:
	
	// release buffers
	speex_bits_destroy(&bits);
	return 0;
}

VoiceRecorder::VoiceRecorder()
{
	// create the decoder
	speexEncoderState = speex_encoder_init(&speex_nb_mode);
	assert(speexEncoderState);
	
	// get some parameters
	speex_encoder_ctl(speexEncoderState, SPEEX_GET_FRAME_SIZE, &frameSize);
	assert(SPEEX_FRAME_SIZE == frameSize);
	
	// can be reduced to one to spare bandwiodth
	int quality = 2;
	speex_encoder_ctl(speexEncoderState, SPEEX_SET_QUALITY, &quality);
	
	// create the thread that willl record and the mutex that will protect access to shared order list
	recordThreadRun = true;
	recordingNow = false;
	stopRecordingTimeout = 0;
	recordingThread = SDL_CreateThread(record, this);
	ordersMutex = SDL_CreateMutex();
}

VoiceRecorder::~VoiceRecorder()
{
	// wait that the record thead finish
	recordingNow = false;
	recordThreadRun = false;
	SDL_WaitThread(recordingThread, NULL);
	SDL_DestroyMutex(ordersMutex);
	
	// destroy the decoder
	speex_encoder_destroy(speexEncoderState);
}

void VoiceRecorder::startRecording(void)
{
	recordingNow = true;
}

void VoiceRecorder::stopRecording(void)
{
	stopRecordingTimeout = STOP_RECORDING_TIMEOUT;
	recordingNow = false;
}

OrderVoiceData *VoiceRecorder::getNextOrder(void)
{
	OrderVoiceData *order;
	SDL_LockMutex(ordersMutex);
	if (orders.empty())
	{
		order = NULL;
	}
	else
	{
		order = orders.front();
		orders.pop();
	}
	SDL_UnlockMutex(ordersMutex);
	return order;
}
