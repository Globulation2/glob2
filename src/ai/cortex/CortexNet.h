// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include <SDL_stdinc.h>
#include <string>
#include <vector>

// Deterministic fixed-point (I16F16) inference for the Cortex swarm worker-tuning
// net (effort B of the Cortex ML pilot — see docs/AI/cortex/PILOT.md and
// ML_CONTRACT.md). Train in f32 in Python, quantize to a versioned I16F16 blob
// (tools/cortex-ml-infer/FORMAT.md), then run an INTEGER forward pass here so the
// AI emits bit-identical orders in lockstep on every client.
//
// I16F16 == a signed 32-bit integer holding value * 2^16. A fixed-point multiply
// is (int64(a) * int64(b)) >> 16 (arithmetic shift). RAW integer features are
// promoted to I16F16 (x << 16) so the whole matmul is one uniform dot product.
// The numpy reference (tools/cortex-ml-infer/int_ref.py) implements the SAME
// arithmetic and inference rule; the parity test proves they agree bit-for-bit.
//
// This module is standalone and NOT wired into CortexPolicy/AICortex yet (that is
// a later integration step). It only depends on CortexConstants.h for the
// inference-rule constants — no engine headers.

namespace Cortex
{
	/// Loaded I16F16 net. Holds the architecture plus quantized weights/biases.
	/// Weights are static after load() (no lockstep save/load state). The forward
	/// pass and inference rule are pure functions of their inputs.
	class CortexNet
	{
	public:
		CortexNet() : loaded_(false) {}

		/// Load a cortex-i16f16-v1 blob from disk. Returns true on success. On
		/// failure leaves the net unloaded and writes one diagnostic to std::cerr
		/// (never fprintf — LogFileManager.h no-ops it). The architecture is
		/// validated against the contract (16 -> 32 -> 32 -> 20).
		bool load(const std::string& path);

		/// Load from an in-memory blob (used by the parity runner / tests).
		bool loadFromMemory(const Uint8* data, size_t size);

		bool isLoaded() const { return loaded_; }

		/// The 16 input features in ML_CONTRACT.md order. Number of features.
		static const int NUM_FEATURES = 16;
		/// The 20-way categorical output (action = class index + 1).
		static const int NUM_LOGITS = 20;

		/// Run the full inference rule from ML_CONTRACT.md and return the chosen
		/// swarm worker cap (maxUnitWorking target):
		///   1. wheat-starved hard clamp (bypass the net),
		///   2. integer forward pass -> 20 logits,
		///   3. mask classes outside [WORKER_MIN .. swarmWorkerCap],
		///   4. argmax (ties -> lowest index), action = idx + 1.
		/// `features` holds the NUM_FEATURES raw integer inputs. The three extra
		/// args are the masking / clamp inputs (also present in `features`, passed
		/// explicitly so the rule reads them by name and the caller controls them).
		int chooseSwarmWorkers(const int features[NUM_FEATURES],
		                       int maxBuildLevel, int freeWorkers,
		                       int harvestableWheatNearby) const;

		/// Integer forward pass: RAW int features -> NUM_LOGITS I16F16 logits.
		/// Exposed for the parity test; the inference rule calls it internally.
		void forward(const int features[NUM_FEATURES], Sint32 logits[NUM_LOGITS]) const;

	private:
		struct Layer
		{
			int inDim;
			int outDim;
			std::vector<Sint32> W; ///< row-major, out-major: W[o*inDim + i] (I16F16)
			std::vector<Sint32> b; ///< I16F16 bias per output
		};

		bool parse(const Uint8* data, size_t size);

		/// Integer forward pass keeping full I16F16 precision in Sint64. Both the
		/// public forward() (narrows to Sint32) and chooseSwarmWorkers() (argmax on
		/// the wide values) call this so the arithmetic lives in one place.
		void forwardWide(const int features[NUM_FEATURES], Sint64 logits[NUM_LOGITS]) const;

		bool loaded_;
		std::vector<int> arch_;
		std::vector<Layer> layers_;
	};
}
