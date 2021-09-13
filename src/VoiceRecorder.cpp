/*
  This file is part of Globulation 2, a free software real-time strategy game
  http://www.globulation2.org
  Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charriere and other contributors
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

#include "VoiceRecorder.h"
#include <assert.h>
#include <stdio.h>
#include <iostream>
#include "Order.h"
#include "Utilities.h"

#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif

#include <speex/speex.h>

#define MAX_VOICE_MULTI_FRAME_LENGTH 2048
#define MAX_VOICE_MULTI_FRAME_SAMPLE_COUNT 19200
#define SPEEX_FRAME_SIZE 160

#ifdef HAVE_PORTAUDIO
	#include "portaudio.h"
#else
	#ifdef WIN32
		// Windows audio input include
		#undef AUDIO_RECORDER_OSS
		#define STOP_RECORDING_TIMEOUT 3000
		#include <windows.h>
		///Voice recording on windows temporarily disables
		#undef WIN32
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
	#else
		#define STOP_RECORDI0G_TIMEOUT 3000
	#endif
#endif


#ifdef HAVE_PORTAUDIO
void PaFlushVoiceData(VoiceRecorder* recorder)
{
	SpeexBits& bits = recorder->bits;
	int byteLength = speex_bits_nbytes(&bits);
	
	boost::shared_ptr<OrderVoiceData> order(new OrderVoiceData(0, byteLength, recorder->frameCount, NULL));
	int nbBytes = speex_bits_write(&bits, (char *)order->getFramesData(), byteLength);
	assert(byteLength == nbBytes);
	
	recorder->frameCount=0;
	
	SDL_LockMutex(recorder->ordersMutex);
	recorder->orders.push(order);
	SDL_UnlockMutex(recorder->ordersMutex);
	
	// reset
	speex_bits_reset(&bits);
};


int PaSpeexEncodeCallback( const void *input, void *output, unsigned long frameCount, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData )
{
	VoiceRecorder* recorder = reinterpret_cast<VoiceRecorder*>(userData);
	SpeexBits& bits = recorder->bits;
	
	const short* inBuffer = reinterpret_cast<const short*>(input);
	short* buffer = recorder->buffer;
	for(unsigned long i=0; i<frameCount; ++i)
		buffer[i] = inBuffer[i];
	for(int i=frameCount; i<recorder->frameSize; ++i)
		buffer[i] = 0;
	speex_encode_int(recorder->speexEncoderState, buffer, &bits);

	recorder->frameCount+=1;
	int byteLength = speex_bits_nbytes(&bits);
	if (byteLength > MAX_VOICE_MULTI_FRAME_LENGTH || recorder->frameCount >(MAX_VOICE_MULTI_FRAME_SAMPLE_COUNT/recorder->frameSize))
	{
		PaFlushVoiceData(recorder);
	}
	
	return 0;	
}
void PaSpeexFinishedCallback(void *userData)
{
	VoiceRecorder* recorder = reinterpret_cast<VoiceRecorder*>(userData);
	SpeexBits& bits = recorder->bits;
	
	int byteLength = speex_bits_nbytes(&bits);
	if (byteLength > 0)
	{
		PaFlushVoiceData(recorder);
	}
	std::cout<<"called byteLength="<<byteLength<<std::endl;
};

#else
int record(void *pointer)
{
	VoiceRecorder *voiceRecorder = static_cast<VoiceRecorder *>(pointer);
	
	// bits buffer for compressed datas
	SpeexBits& bits = voiceRecorder->bits;
	
	while (voiceRecorder->recordThreadRun)
	{
		while (!voiceRecorder->recordingNow)
		{
			// TODO : ugly, use semaphore
			SDL_Delay(1);
			
			if (!voiceRecorder->recordThreadRun)
				goto abortRecordThread;
		}
		
		// Win32
		#ifdef WIN32
		// Create members
		HWAVEIN waveIn = 0;
		HANDLE event = CreateEvent(NULL, FALSE, FALSE, NULL);
		unsigned bufferCount = 2;
		signed short buffersData[bufferCount][SPEEX_FRAME_SIZE];
		WAVEHDR buffers[bufferCount];
		unsigned bufferPos = 0;
		
		// Setup parameters
		WAVEFORMATEX waveFormat;
		waveFormat.wFormatTag = WAVE_FORMAT_PCM;
		waveFormat.nChannels = 1;
		waveFormat.nSamplesPerSec = 8000;
		waveFormat.wBitsPerSample = 16;
		waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
		waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
		waveFormat.cbSize = 0;
		
		// Get the number of Digital Audio In devices in this computer
		unsigned long devCount = waveInGetNumDevs();
		if (!devCount)
		{
			std::cout << "No Win32 Input device found\n";
			break;
		}
		for (unsigned long i = 0; i < devCount; i++)
		{
			WAVEINCAPS wic;
			if (!waveInGetDevCaps(i, &wic, sizeof(WAVEINCAPS)))
				std::cerr << "Win32 Input device " << i << " : " << (const char *)wic.szPname << "\n";
		}
		// TODO: let the user choose it, for now, always take the first one
		unsigned sourceId = 0;
		
		// Open device
		MMRESULT openResult = waveInOpen(&waveIn, sourceId, &waveFormat, (DWORD)event, 0, CALLBACK_EVENT);
		if (openResult != MMSYSERR_NOERROR)
			break;
		
		// Prepare and add buffer
		bool addBufferError = false;
		for (unsigned i = 0; i < bufferCount; i++)
		{
			buffers[i].dwBufferLength = SPEEX_FRAME_SIZE * sizeof(signed short);
			buffers[i].lpData = (char *)buffersData[i];
			buffers[i].dwFlags = 0;
			if (waveInPrepareHeader(waveIn, &buffers[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
			{
				addBufferError = true;
				break;
			}
			if (waveInAddBuffer(waveIn, &buffers[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
			{
				addBufferError = true;
				break;
			}
		}
		
		// Start acquisition if no error, otherwise berak
		if (addBufferError || (waveInStart(waveIn) != MMSYSERR_NOERROR))
			break;
		#endif	
		
		#ifdef __APPLE__
		fprintf(stderr, "VoiceRecorder::record : no audio input support for Mac yet. Voice chat will be disabled. Contributions welcome\n");
		// TODO : Mac OS X code
		break; // while no code, break
		#endif
		
		
		// open sound device, plateforme dependant
		#ifdef AUDIO_RECORDER_OSS
		// bytes buffer for read
		short buffer[SPEEX_FRAME_SIZE];
		size_t toReadLength = 2*SPEEX_FRAME_SIZE;
	
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
			float floatBuffer[SPEEX_FRAME_SIZE];
			
			// read
			#ifdef WIN32
			// Wait for buffer ready
			WaitForSingleObject(event, INFINITE);
			
			// transforms samples to float
			for (size_t i = 0; i < SPEEX_FRAME_SIZE; i++)
				floatBuffer[i] = buffersData[bufferPos][i];
			
			 // Put back buffer
			waveInAddBuffer(waveIn, &buffers[bufferPos], sizeof(WAVEHDR));
			bufferPos = (bufferPos + 1) % bufferCount;
			voiceRecorder->stopRecordingTimeout -= SPEEX_FRAME_SIZE;
			totalRead += SPEEX_FRAME_SIZE;
			#endif
			
			#ifdef __APPLE__
			// TODO : Mac OS X code
			#endif
			
			#ifdef AUDIO_RECORDER_OSS
			// read
			Utilities::read(dsp, buffer, toReadLength);
			totalRead += toReadLength;
			voiceRecorder->stopRecordingTimeout -= toReadLength;
			// transforms samples to float
			for (size_t i=0; i<SPEEX_FRAME_SIZE; i++)
				floatBuffer[i] = buffer[i];
			#endif
			
			// encode
			speex_encode(voiceRecorder->speexEncoderState, floatBuffer, &bits);
			frameCount++;
			
			// if overflow conditions, send
			int byteLength = speex_bits_nbytes(&bits);
			if (byteLength > MAX_VOICE_MULTI_FRAME_LENGTH || totalRead > MAX_VOICE_MULTI_FRAME_SAMPLE_COUNT)
			{
				boost::shared_ptr<OrderVoiceData> order(new OrderVoiceData(0, byteLength, frameCount, NULL));
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
			boost::shared_ptr<OrderVoiceData> order(new OrderVoiceData(0, byteLength, frameCount, NULL));
			int nbBytes = speex_bits_write(&bits, (char *)order->getFramesData(), byteLength);
			assert(byteLength == nbBytes);
			
			SDL_LockMutex(voiceRecorder->ordersMutex);
			voiceRecorder->orders.push(order);
			SDL_UnlockMutex(voiceRecorder->ordersMutex);
		}
		
		// close sound device, plateforme dependant
		#ifdef WIN32
		if (waveIn != 0)
		{
			// Stop acquisition
			waveInReset(waveIn);
			// Destroy buffers
			for (unsigned i = 0; i < bufferCount; i++)
				waveInUnprepareHeader(waveIn, &buffers[i], sizeof(WAVEHDR));
			
			// Close device
			waveInClose(waveIn);
		}
		CloseHandle(event);
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
	
	// release buffers, no will be released in the descructor
	// speex_bits_destroy(&bits);
	return 0;
}
#endif

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
	
	speex_bits_init(&bits);
	speex_bits_reset(&bits);
		
	recordingNow = false;
	ordersMutex = SDL_CreateMutex();
	#ifdef HAVE_PORTAUDIO
		buffer = new short[frameSize];
		frameCount=0;
		stream = nullptr;
		PaError err = Pa_Initialize();
		if( err != paNoError )
			return;
		err = Pa_OpenDefaultStream( &stream, 1, 0, paInt16, 8000, frameSize, PaSpeexEncodeCallback, this);
		if( err != paNoError )
			return;
		err = Pa_SetStreamFinishedCallback( stream, PaSpeexFinishedCallback );
		if( err != paNoError)
			return;
	#else
		// create the thread that willl record and the mutex that will protect access to shared order list
		recordThreadRun = true;
		stopRecordingTimeout = 0;
		recordingThread = SDL_CreateThread(record, "VoiceRecorder", this);
	#endif
}

VoiceRecorder::~VoiceRecorder()
{
	recordingNow = false;
	#ifdef HAVE_PORTAUDIO
		PaError err = paNoError;
		if(stream)
			err = Pa_CloseStream( stream );
		if( err != paNoError )
			printf(  "PortAudio error: %s\n", Pa_GetErrorText( err ) );
		err = Pa_Terminate();
		if( err != paNoError )
			printf(  "PortAudio error: %s\n", Pa_GetErrorText( err ) );
		delete[] buffer;
	#else
		recordThreadRun = false;
		// wait that the record thead finish
		SDL_WaitThread(recordingThread, NULL);
		SDL_DestroyMutex(ordersMutex);
	#endif
	
	speex_bits_reset(&bits);
	// destroy the decoder
	speex_encoder_destroy(speexEncoderState);
}

void VoiceRecorder::startRecording(void)
{
	recordingNow = true;
	#ifdef HAVE_PORTAUDIO
		PaError err = Pa_StartStream( stream );
		if( err != paNoError )
			return;
	#endif
}

void VoiceRecorder::stopRecording(void)
{
	recordingNow = false;
	#ifdef HAVE_PORTAUDIO
		PaError err = Pa_StopStream( stream );
		if( err != paNoError )
			return;
	#else
		stopRecordingTimeout = STOP_RECORDING_TIMEOUT;
	#endif
}

boost::shared_ptr<OrderVoiceData> VoiceRecorder::getNextOrder(void)
{
	boost::shared_ptr<OrderVoiceData> order;
	SDL_LockMutex(ordersMutex);
	if (orders.empty())
	{
		order = boost::shared_ptr<OrderVoiceData>();
	}
	else
	{
		order = orders.front();
		orders.pop();
	}
	SDL_UnlockMutex(ordersMutex);
	return order;
}
