// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <queue>

//! Resampling/interpolation state for one player's incoming voice stream.
//!
//! The speex narrowband decoder produces 8 kHz float samples; the SDL mixer
//! consumes them at the 44.1 kHz stereo output rate via linear interpolation
//! between two consecutive input samples (voiceVal0, voiceVal1) using the
//! fractional cursor voiceSubIndex.
//!
//! This type is deliberately SDL-free (only <queue>) so the resampling logic
//! can be unit-tested without an audio device — see test/PlayerVoiceDrainTest.cpp.
//! It was previously a struct nested inside SoundMixer.
struct PlayerVoice
{
	//! Decoded 8 kHz float samples awaiting playback.
	std::queue<float> voiceDatas;
	//! Fractional playback position in [0,1) between voiceVal0 and voiceVal1.
	float voiceSubIndex = 0.0f;
	//! Left/right endpoints of the current linear-interpolation segment.
	float voiceVal0 = 0.0f;
	float voiceVal1 = 0.0f;

	//! Advance the interpolation cursor by one output-channel sample and return
	//! this voice's contribution to the mixed value for that sample.
	//!
	//! Sets `exhausted` to true when advancing drained the queue to empty; the
	//! caller must then discard this voice and must not call this again on it.
	//! Precondition: the queue is non-empty on entry (a voice with an empty
	//! queue must already have been removed).
	//!
	//! The returned contribution is computed from the state on entry (before
	//! the cursor advances), matching the original mixer loop.
	float advanceOutputSample(bool &exhausted);
};
