// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "PlayerVoice.h"

namespace
{
	//! Speex narrowband decoder output rate (Hz).
	constexpr float VOICE_INPUT_RATE_HZ = 8000.0f;
	//! SDL audio device output rate (Hz).
	constexpr float VOICE_OUTPUT_RATE_HZ = 44100.0f;
	//! The mixer callback runs once per output *channel* sample (stereo = two
	//! channels per frame), so each voice advances by half the input-to-output
	//! sample-rate ratio per call. Kept as the original literal expression so
	//! the produced audio is bit-identical to the pre-extraction mixer.
	constexpr float VOICE_INTERPOLATION_STEP =
		(VOICE_INPUT_RATE_HZ / VOICE_OUTPUT_RATE_HZ) * 0.5f;
}

float PlayerVoice::advanceOutputSample(bool &exhausted)
{
	exhausted = false;

	// Contribution is taken from the pre-advance state, matching the original.
	const float contribution =
		(1.0f - voiceSubIndex) * voiceVal0 + voiceSubIndex * voiceVal1;

	voiceSubIndex += VOICE_INTERPOLATION_STEP;
	if (voiceSubIndex > 1.0f)
	{
		voiceSubIndex -= 1.0f;
		voiceVal0 = voiceVal1;
		voiceDatas.pop();

		// If that pop drained the queue, the voice is finished. Report it and
		// return WITHOUT reading front() — the previous code read front() on the
		// now-empty queue (undefined behavior) before this check.
		if (voiceDatas.empty())
		{
			exhausted = true;
			return contribution;
		}
		voiceVal1 = voiceDatas.front();
	}

	return contribution;
}
