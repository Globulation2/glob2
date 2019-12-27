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

#ifndef __VOICE_RECORDER_H
#define __VOICE_RECORDER_H

#include <queue>
#include <SDL.h>
#include <SDL_thread.h>
#include <boost/shared_ptr.hpp>
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
	std::queue<boost::shared_ptr<OrderVoiceData> > orders;
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
	boost::shared_ptr<OrderVoiceData> getNextOrder(void);
};
#endif
