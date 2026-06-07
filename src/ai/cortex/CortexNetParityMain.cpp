// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

// Standalone parity runner for the Cortex I16F16 inference. NOT part of the game
// binary — it is compiled separately by tools/cortex-ml-infer/parity.py with a
// one-off g++ command. It loads a cortex-i16f16-v1 blob and a whitespace input
// file (16 features + maxBuildLevel + freeWorkers + harvestableWheatNearby per
// line) and writes one chosen action per line, so parity.py can diff it against
// the numpy reference int_ref.py. Must be BIT-IDENTICAL.

#include "CortexNet.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
	if (argc != 4)
	{
		std::cerr << "usage: " << argv[0] << " <blob> <inputs.txt> <out.txt>\n";
		return 2;
	}
	const std::string blobPath = argv[1];
	const std::string inputsPath = argv[2];
	const std::string outPath = argv[3];

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
		if (vals.size() != static_cast<size_t>(Cortex::CortexNet::NUM_FEATURES) + 3)
		{
			std::cerr << "bad input row (expected "
			          << (Cortex::CortexNet::NUM_FEATURES + 3) << " ints, got "
			          << vals.size() << ")\n";
			return 1;
		}
		int features[Cortex::CortexNet::NUM_FEATURES];
		for (int i = 0; i < Cortex::CortexNet::NUM_FEATURES; i++)
			features[i] = vals[i];
		const int maxBuildLevel = vals[Cortex::CortexNet::NUM_FEATURES + 0];
		const int freeWorkers = vals[Cortex::CortexNet::NUM_FEATURES + 1];
		const int harvestableWheatNearby = vals[Cortex::CortexNet::NUM_FEATURES + 2];

		const int action = net.chooseSwarmWorkers(features, maxBuildLevel,
		                                           freeWorkers, harvestableWheatNearby);
		out << action << "\n";
		rowCount++;
	}

	std::cerr << "parity runner: wrote " << rowCount << " actions\n";
	return 0;
}
