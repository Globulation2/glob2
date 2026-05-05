// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charriere and other contributors

// This file is part of Globulation 2, a free software real-time strategy game

#pragma once

#include <queue>
#include <SDL.h>
#include <SDL_thread.h>
#include <memory>
#include "config.h"

#ifdef HAVE_PORTAUDIO
#include "portaudio.h"
#endif

#include <speex/speex.h>

class OrderVoiceData;

//! Record voice at 8Khz 16bits from microphone and create OrderVoiceData packets. Uses Speex (http://www.speex.org/)
class VoiceRecorder
{
public:
	// Those variables are public because of C thread API. do not access them ouside VoiceRecorder.cpp
	//! pointer to the structure holding the speex encoder
	void *speexEncoderState;
	// Bits for speex encoding
	SpeexBits bits;
	//! Size of one frame of encoding
	int frameSize;
	//! thread used for recording
	SDL_Thread *recordingThread;
	//! Mutex for orders
	SDL_mutex *ordersMutex;
	//! Queue of orders to be sent through the network
	std::queue<std::shared_ptr<OrderVoiceData> > orders;
	//! True when recording
	bool recordingNow;
	
	#ifdef HAVE_PORTAUDIO
	PaStream *stream;
	int frameCount;
	short* buffer;
	#else
	//! True when record thread is running
	bool recordThreadRun;
	//! When recordingNow is set to false, get decrement
	int stopRecordingTimeout;
	#endif
	
	
	
public:
	//! Constructor
	VoiceRecorder();
	
	//! Destructor
	virtual ~VoiceRecorder();
	
	//! Start recording
	void startRecording(void);
	//! Stop recording
	void stopRecording(void);
	//! Return the next voice data order from the internal queue
	std::shared_ptr<OrderVoiceData> getNextOrder(void);
};
