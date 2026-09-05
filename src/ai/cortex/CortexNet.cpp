// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "CortexNet.h"
#include "CortexConstants.h"

#include <cstdio>
#include <cstring>
#include <iostream>

// Integer I16F16 inference for the Cortex swarm worker-tuning net. See
// CortexNet.h and tools/cortex-ml-infer/FORMAT.md for the contract.

namespace Cortex
{
	namespace
	{
		const Uint32 BLOB_MAGIC = 0x434E5831u; // 'CNX1' little-endian
		const Uint32 BLOB_VERSION = 1u;
		const int FRAC_BITS = 16;

		// Read a little-endian uint32 / int32 from a byte cursor (endian-neutral:
		// reconstructs the value from individual bytes, so it is correct on both
		// little- and big-endian hosts — determinism does not depend on host
		// byte order). Advances `off`.
		bool readU32(const Uint8* data, size_t size, size_t& off, Uint32& out)
		{
			if (off + 4 > size)
				return false;
			out = static_cast<Uint32>(data[off])
			    | (static_cast<Uint32>(data[off + 1]) << 8)
			    | (static_cast<Uint32>(data[off + 2]) << 16)
			    | (static_cast<Uint32>(data[off + 3]) << 24);
			off += 4;
			return true;
		}

		bool readI32(const Uint8* data, size_t size, size_t& off, Sint32& out)
		{
			Uint32 u;
			if (!readU32(data, size, off, u))
				return false;
			out = static_cast<Sint32>(u); // two's complement round-trip
			return true;
		}

		// I16F16 fixed-point multiply: (int64(a) * int64(b)) >> 16, arithmetic
		// shift (sign-preserving). int64 intermediate prevents overflow.
		inline Sint64 fxmul(Sint64 a, Sint64 b)
		{
			return (a * b) >> FRAC_BITS;
		}

		int swarmWorkerCap(int maxBuildLevel, int freeWorkers)
		{
			if (maxBuildLevel >= CORTEX_SWARM_CAP_LIFT_BUILDLEVEL && freeWorkers > 0)
				return CORTEX_SWARM_WORKER_CAP_LATE;
			return CORTEX_SWARM_WORKER_CAP;
		}
	}

	namespace
	{
		// Slurp a blob from disk into `buf`. Returns true on success; writes one
		// std::cerr diagnostic and returns false otherwise. Shared by both loaders.
		bool readBlobFile(const std::string& path, std::vector<Uint8>& buf)
		{
			FILE* fp = std::fopen(path.c_str(), "rb");
			if (!fp)
			{
				std::cerr << "CortexNet: cannot open blob '" << path << "'\n";
				return false;
			}
			std::fseek(fp, 0, SEEK_END);
			long len = std::ftell(fp);
			std::fseek(fp, 0, SEEK_SET);
			if (len <= 0)
			{
				std::cerr << "CortexNet: empty/invalid blob '" << path << "'\n";
				std::fclose(fp);
				return false;
			}
			buf.resize(static_cast<size_t>(len));
			size_t got = std::fread(buf.data(), 1, buf.size(), fp);
			std::fclose(fp);
			if (got != buf.size())
			{
				std::cerr << "CortexNet: short read on blob '" << path << "'\n";
				return false;
			}
			return true;
		}
	}

	bool CortexNet::load(const std::string& path)
	{
		loaded_ = false;
		std::vector<Uint8> buf;
		if (!readBlobFile(path, buf))
			return false;
		return loadFromMemory(buf.data(), buf.size(), NUM_FEATURES, NUM_LOGITS);
	}

	bool CortexNet::loadDecide(const std::string& path)
	{
		loaded_ = false;
		std::vector<Uint8> buf;
		if (!readBlobFile(path, buf))
			return false;
		return loadFromMemory(buf.data(), buf.size(),
		                      NUM_DECIDE_FEATURES, NUM_DECIDE_LOGITS);
	}

	bool CortexNet::loadFromMemory(const Uint8* data, size_t size,
	                               int expectIn, int expectOut)
	{
		loaded_ = parse(data, size, expectIn, expectOut);
		return loaded_;
	}

	bool CortexNet::parse(const Uint8* data, size_t size,
	                      int expectIn, int expectOut)
	{
		arch_.clear();
		layers_.clear();

		size_t off = 0;
		Uint32 magic = 0, version = 0, frac = 0, numLayers = 0, archLen = 0;
		if (!readU32(data, size, off, magic) || !readU32(data, size, off, version)
		 || !readU32(data, size, off, frac) || !readU32(data, size, off, numLayers)
		 || !readU32(data, size, off, archLen))
		{
			std::cerr << "CortexNet: truncated header\n";
			return false;
		}
		if (magic != BLOB_MAGIC)
		{
			std::cerr << "CortexNet: bad magic\n";
			return false;
		}
		if (version != BLOB_VERSION)
		{
			std::cerr << "CortexNet: unsupported blob version " << version << "\n";
			return false;
		}
		if (frac != static_cast<Uint32>(FRAC_BITS))
		{
			std::cerr << "CortexNet: unexpected frac bits " << frac << "\n";
			return false;
		}
		if (archLen < 2 || numLayers != archLen - 1)
		{
			std::cerr << "CortexNet: arch/layer mismatch\n";
			return false;
		}

		arch_.resize(archLen);
		for (Uint32 i = 0; i < archLen; i++)
		{
			Uint32 v;
			if (!readU32(data, size, off, v))
			{
				std::cerr << "CortexNet: truncated arch\n";
				return false;
			}
			arch_[i] = static_cast<int>(v);
		}

		// Validate against the caller's contract architecture endpoints (the
		// worker-cap net pins 16..20, the decision net pins 48..18). The blob
		// carries the full arch; only the endpoints distinguish the two nets, so a
		// wrong-net blob is rejected here at load.
		if (arch_.front() != expectIn || arch_.back() != expectOut)
		{
			std::cerr << "CortexNet: arch endpoints (" << arch_.front() << ".."
			          << arch_.back() << ") != contract (" << expectIn << ".."
			          << expectOut << ")\n";
			return false;
		}

		layers_.reserve(numLayers);
		for (Uint32 li = 0; li < numLayers; li++)
		{
			Uint32 inDim = 0, outDim = 0;
			if (!readU32(data, size, off, inDim) || !readU32(data, size, off, outDim))
			{
				std::cerr << "CortexNet: truncated layer header\n";
				return false;
			}
			if (static_cast<int>(inDim) != arch_[li]
			 || static_cast<int>(outDim) != arch_[li + 1])
			{
				std::cerr << "CortexNet: layer " << li << " dims disagree with arch\n";
				return false;
			}
			Layer layer;
			layer.inDim = static_cast<int>(inDim);
			layer.outDim = static_cast<int>(outDim);
			const size_t nw = static_cast<size_t>(inDim) * outDim;
			layer.W.resize(nw);
			for (size_t k = 0; k < nw; k++)
			{
				if (!readI32(data, size, off, layer.W[k]))
				{
					std::cerr << "CortexNet: truncated layer " << li << " weights\n";
					return false;
				}
			}
			layer.b.resize(outDim);
			for (Uint32 o = 0; o < outDim; o++)
			{
				if (!readI32(data, size, off, layer.b[o]))
				{
					std::cerr << "CortexNet: truncated layer " << li << " bias\n";
					return false;
				}
			}
			layers_.push_back(std::move(layer));
		}

		return true;
	}

	void CortexNet::forwardWide(const int* features, Sint64* logits) const
	{
		// Dim-agnostic: the input width and logit count come from the loaded
		// architecture (arch_.front()/back()), not from compile-time constants, so
		// the SAME arithmetic serves both the worker-cap (16->20) and decision
		// (48->18) nets. Promote raw int features to I16F16 (x << 16). Sint64
		// activations so the per-product fxmul and the accumulation never overflow.
		const int inDim = arch_.front();
		std::vector<Sint64> acts(inDim);
		for (int i = 0; i < inDim; i++)
			acts[i] = static_cast<Sint64>(features[i]) << FRAC_BITS;

		const size_t last = layers_.size() - 1;
		for (size_t li = 0; li < layers_.size(); li++)
		{
			const Layer& layer = layers_[li];
			std::vector<Sint64> out(layer.outDim);
			for (int o = 0; o < layer.outDim; o++)
			{
				Sint64 acc = 0; // I16F16 accumulator
				const int base = o * layer.inDim;
				for (int i = 0; i < layer.inDim; i++)
					acc += fxmul(static_cast<Sint64>(layer.W[base + i]), acts[i]);
				acc += static_cast<Sint64>(layer.b[o]);
				if (li != last && acc < 0)
					acc = 0; // integer ReLU
				out[o] = acc;
			}
			acts.swap(out);
		}

		const int outDim = arch_.back();
		for (int o = 0; o < outDim; o++)
			logits[o] = acts[o];
	}

	void CortexNet::forward(const int features[NUM_FEATURES], Sint32 logits[NUM_LOGITS]) const
	{
		Sint64 wide[NUM_LOGITS];
		forwardWide(features, wide);
		for (int o = 0; o < NUM_LOGITS; o++)
			logits[o] = static_cast<Sint32>(wide[o]);
	}

	void CortexNet::forwardDecide(const int features[NUM_DECIDE_FEATURES],
	                              Sint32 logits[NUM_DECIDE_LOGITS]) const
	{
		Sint64 wide[NUM_DECIDE_LOGITS];
		forwardWide(features, wide);
		for (int o = 0; o < NUM_DECIDE_LOGITS; o++)
			logits[o] = static_cast<Sint32>(wide[o]);
	}

	int CortexNet::chooseSwarmWorkers(const int features[NUM_FEATURES],
	                                  int maxBuildLevel, int freeWorkers,
	                                  int harvestableWheatNearby) const
	{
		// 1. Wheat-starved hard clamp (bypass the net).
		if (harvestableWheatNearby >= 0
		 && harvestableWheatNearby < CORTEX_SWARM_WHEAT_STARVED_TILES)
			return CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP;

		// 2. Integer forward pass. Argmax on the full-precision Sint64 logits so
		// the comparison matches int_ref.py, which never narrows the values.
		Sint64 logits[NUM_LOGITS];
		forwardWide(features, logits);

		// 3. Mask: valid action k+1 in [WORKER_MIN .. cap].
		const int cap = swarmWorkerCap(maxBuildLevel, freeWorkers);
		int loIdx = CORTEX_SWARM_WORKER_MIN - 1;
		int hiIdx = cap - 1;
		if (hiIdx >= NUM_LOGITS)
			hiIdx = NUM_LOGITS - 1;

		// 4. argmax over unmasked logits, ties -> lowest index.
		int bestIdx = -1;
		Sint64 bestVal = 0;
		for (int idx = loIdx; idx <= hiIdx; idx++)
		{
			if (idx < 0 || idx >= NUM_LOGITS)
				continue;
			if (bestIdx < 0 || logits[idx] > bestVal)
			{
				bestVal = logits[idx];
				bestIdx = idx;
			}
		}
		if (bestIdx < 0)
			return CORTEX_SWARM_WORKER_MIN; // degenerate: no valid class
		return bestIdx + 1;
	}

	int CortexNet::scoreDecision(const int features[NUM_DECIDE_FEATURES],
	                             Uint32 eligibleMask) const
	{
		// 0. Nothing eligible -> NoOp (matches decide()'s "no candidate wants to
		// act"). Checked first so we never run the net for an empty mask.
		if (eligibleMask == 0)
			return -1;

		// 1. Integer forward pass. Argmax on the full-precision Sint64 logits so the
		// comparison matches the numpy reference, which never narrows the values.
		Sint64 logits[NUM_DECIDE_LOGITS];
		forwardWide(features, logits);

		// 2. Mask every class whose eligibleMask bit is 0; 3. argmax over the
		// unmasked logits, ties -> lowest class index (== decide()'s earliest-
		// candidate-wins). No softmax, no floats.
		int bestIdx = -1;
		Sint64 bestVal = 0;
		for (int k = 0; k < NUM_DECIDE_LOGITS; k++)
		{
			if ((eligibleMask & (static_cast<Uint32>(1u) << k)) == 0)
				continue; // class k is not eligible this cycle
			if (bestIdx < 0 || logits[k] > bestVal)
			{
				bestVal = logits[k];
				bestIdx = k;
			}
		}
		return bestIdx; // -1 only if mask bits were all outside [0,18) (unreachable)
	}
}
