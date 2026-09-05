// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

#include <SDL_stdinc.h>
#include <string>
#include <vector>

// Deterministic fixed-point (I16F16) inference for the Cortex ML pilot nets.
// Train in f32 in Python, quantize to a versioned I16F16 blob
// (tools/cortex-ml-infer/FORMAT.md), then run an INTEGER forward pass here so the
// AI emits bit-identical orders in lockstep on every client.
//
// One class, two nets (same integer arithmetic, same blob layout, different
// dims + inference rule):
//   * worker-cap net  16 -> 32 -> 32 -> 20  (ML_CONTRACT.md):
//       load() + chooseSwarmWorkers() + forward().
//   * decision net     48 -> 64 -> 64 -> 18 (DECIDE_CONTRACT.md):
//       loadDecide() + scoreDecision() + forwardDecide().
// The integer core (forwardWide) is dim-agnostic, driven entirely by the blob's
// architecture header; only the two public load() siblings differ — each pins the
// architecture endpoints its caller depends on.
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
	/// Weights are static after load (no lockstep save/load state). The forward
	/// pass and inference rules are pure functions of their inputs.
	class CortexNet
	{
	public:
		CortexNet() : loaded_(false) {}

		/// Load a cortex-i16f16-v1 blob for the WORKER-CAP net. Returns true on
		/// success. On failure leaves the net unloaded and writes one diagnostic to
		/// std::cerr. The architecture
		/// is validated against the worker-cap contract (16 -> 32 -> 32 -> 20).
		bool load(const std::string& path);

		/// Load a cortex-decide-i16f16-v1 blob for the DECISION net. Same blob
		/// layout/arithmetic as load(); validates the decision-net architecture
		/// endpoints (48 -> ... -> 18) instead. Used by scoreDecision()/forwardDecide().
		bool loadDecide(const std::string& path);

		/// Load from an in-memory blob (used by the parity runner / tests). The
		/// `expectIn`/`expectOut` are the contract's architecture endpoints to
		/// validate against (the blob carries the full arch; only the endpoints are
		/// pinned per net so a wrong-net blob is rejected at load).
		bool loadFromMemory(const Uint8* data, size_t size,
		                    int expectIn, int expectOut);

		bool isLoaded() const { return loaded_; }

		// --- worker-cap net (ML_CONTRACT.md) -------------------------------------
		/// The 16 input features in ML_CONTRACT.md order.
		static const int NUM_FEATURES = 16;
		/// The 20-way categorical output (action = class index + 1).
		static const int NUM_LOGITS = 20;

		// --- decision net (DECIDE_CONTRACT.md) -----------------------------------
		/// The 48 input features in DECIDE_CONTRACT.md order. Mirrors
		/// CortexPolicy::NUM_DECIDE_FEATURES (kept consistent by contract; this
		/// header must not include CortexPolicy.h, hence a local mirror).
		static const int NUM_DECIDE_FEATURES = 48;
		/// The 18-way categorical output: one utility score per decide() candidate.
		static const int NUM_DECIDE_LOGITS = 18;

		/// Run the full WORKER-CAP inference rule from ML_CONTRACT.md and return the
		/// chosen swarm worker cap (maxUnitWorking target):
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

		/// Worker-cap integer forward pass: RAW int features -> NUM_LOGITS I16F16
		/// logits. Exposed for the parity test; chooseSwarmWorkers calls it internally.
		void forward(const int features[NUM_FEATURES], Sint32 logits[NUM_LOGITS]) const;

		/// Run the DECISION inference rule from DECIDE_CONTRACT.md and return the
		/// chosen class index (= decide() candidate index):
		///   1. integer forward pass -> 18 logits,
		///   2. mask every class k whose `eligibleMask` bit k is 0,
		///   3. argmax over unmasked logits, ties -> lowest class index.
		/// Returns -1 if `eligibleMask == 0` (nothing eligible -> NoOp). NO softmax,
		/// NO floats. `features` holds the NUM_DECIDE_FEATURES raw integer inputs.
		int scoreDecision(const int features[NUM_DECIDE_FEATURES],
		                  Uint32 eligibleMask) const;

		/// Decision integer forward pass: RAW int features -> NUM_DECIDE_LOGITS
		/// I16F16 logits. Exposed for the parity test; scoreDecision calls it
		/// internally (mirror of forward()).
		void forwardDecide(const int features[NUM_DECIDE_FEATURES],
		                   Sint32 logits[NUM_DECIDE_LOGITS]) const;

	private:
		struct Layer
		{
			int inDim;
			int outDim;
			std::vector<Sint32> W; ///< row-major, out-major: W[o*inDim + i] (I16F16)
			std::vector<Sint32> b; ///< I16F16 bias per output
		};

		bool parse(const Uint8* data, size_t size, int expectIn, int expectOut);

		/// Dim-agnostic integer forward pass keeping full I16F16 precision in Sint64.
		/// Reads the input width from arch_.front() and writes arch_.back() logits.
		/// `features` must hold arch_.front() raw ints; `logits` arch_.back() Sint64.
		/// Both nets' public entry points funnel through here so the arithmetic
		/// (I16F16 fxmul, ReLU between layers, no transcendentals) lives in one place.
		void forwardWide(const int* features, Sint64* logits) const;

		bool loaded_;
		std::vector<int> arch_;
		std::vector<Layer> layers_;
	};
}
