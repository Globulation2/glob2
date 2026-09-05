// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

// Standalone parity runner for the Cortex I16F16 inference. NOT part of the game
// binary — it is compiled separately by tools/cortex-ml-infer/parity.py with a
// one-off g++ command. Two modes, both diff bit-for-bit against the numpy
// references (int_ref.py for worker-cap, the decision reference for --decide):
//
//   Worker-cap (default):
//     <blob> <inputs.txt> <out.txt>
//     each input line: 16 features + maxBuildLevel + freeWorkers +
//     harvestableWheatNearby (19 ints). Output: one chosen action per line.
//
//   Decision net (--decide):
//     --decide <blob> <inputs.txt> <out.txt>
//     each input line: 48 features + eligibleMask (49 ints). Output per line: the
//     18 raw I16F16 logits then the scoreDecision result (chosen class index, or
//     -1 if nothing eligible), all whitespace-separated.

#include "CortexNet.h"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	// Worker-cap mode: 16 features + 3 rule inputs -> one chosen action per line.
	int runWorkerCap(const std::string& blobPath, const std::string& inputsPath,
	                 const std::string& outPath)
	{
		Cortex::CortexNet net;
		if (!net.load(blobPath))
		{
			std::cerr << "failed to load blob\n";
			return 1;
		}

		std::ifstream in(inputsPath.c_str());
		if (!in)
		{
			std::cerr << "cannot open inputs file\n";
			return 1;
		}
		std::ofstream out(outPath.c_str());
		if (!out)
		{
			std::cerr << "cannot open output file\n";
			return 1;
		}

		const int N = Cortex::CortexNet::NUM_FEATURES;
		std::string line;
		int rowCount = 0;
		while (std::getline(in, line))
		{
			if (line.empty())
				continue;
			std::istringstream ss(line);
			std::vector<int> vals;
			int v;
			while (ss >> v)
				vals.push_back(v);
			if (vals.size() != static_cast<size_t>(N) + 3)
			{
				std::cerr << "bad input row (expected " << (N + 3)
				          << " ints, got " << vals.size() << ")\n";
				return 1;
			}
			int features[Cortex::CortexNet::NUM_FEATURES];
			for (int i = 0; i < N; i++)
				features[i] = vals[i];
			const int maxBuildLevel = vals[N + 0];
			const int freeWorkers = vals[N + 1];
			const int harvestableWheatNearby = vals[N + 2];

			const int action = net.chooseSwarmWorkers(features, maxBuildLevel,
			                                           freeWorkers, harvestableWheatNearby);
			out << action << "\n";
			rowCount++;
		}

		std::cerr << "parity runner: wrote " << rowCount << " actions\n";
		return 0;
	}

	// Decision mode: 48 features + eligibleMask -> 18 raw I16F16 logits + chosen.
	int runDecide(const std::string& blobPath, const std::string& inputsPath,
	              const std::string& outPath)
	{
		Cortex::CortexNet net;
		if (!net.loadDecide(blobPath))
		{
			std::cerr << "failed to load decision blob\n";
			return 1;
		}

		std::ifstream in(inputsPath.c_str());
		if (!in)
		{
			std::cerr << "cannot open inputs file\n";
			return 1;
		}
		std::ofstream out(outPath.c_str());
		if (!out)
		{
			std::cerr << "cannot open output file\n";
			return 1;
		}

		const int N = Cortex::CortexNet::NUM_DECIDE_FEATURES;
		const int L = Cortex::CortexNet::NUM_DECIDE_LOGITS;
		std::string line;
		int rowCount = 0;
		while (std::getline(in, line))
		{
			if (line.empty())
				continue;
			std::istringstream ss(line);
			std::vector<long long> vals;
			long long v;
			while (ss >> v)
				vals.push_back(v);
			if (vals.size() != static_cast<size_t>(N) + 1)
			{
				std::cerr << "bad input row (expected " << (N + 1)
				          << " ints, got " << vals.size() << ")\n";
				return 1;
			}
			int features[Cortex::CortexNet::NUM_DECIDE_FEATURES];
			for (int i = 0; i < N; i++)
				features[i] = static_cast<int>(vals[i]);
			const Uint32 eligibleMask = static_cast<Uint32>(vals[N]);

			Sint32 logits[Cortex::CortexNet::NUM_DECIDE_LOGITS];
			net.forwardDecide(features, logits);
			const int chosen = net.scoreDecision(features, eligibleMask);

			for (int o = 0; o < L; o++)
				out << static_cast<Sint32>(logits[o]) << " ";
			out << chosen << "\n";
			rowCount++;
		}

		std::cerr << "parity runner (--decide): wrote " << rowCount << " rows\n";
		return 0;
	}
}

int main(int argc, char** argv)
{
	// --decide <blob> <inputs.txt> <out.txt>   (5 args)
	if (argc == 5 && std::string(argv[1]) == "--decide")
		return runDecide(argv[2], argv[3], argv[4]);

	// <blob> <inputs.txt> <out.txt>            (4 args, worker-cap)
	if (argc == 4)
		return runWorkerCap(argv[1], argv[2], argv[3]);

	std::cerr << "usage: " << argv[0] << " <blob> <inputs.txt> <out.txt>\n"
	          << "       " << argv[0] << " --decide <blob> <inputs.txt> <out.txt>\n";
	return 2;
}
