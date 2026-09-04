// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __CPUStatisticsManager_H
#define __CPUStatisticsManager_H

#include <vector>

///This class holds the responsibility of managing CPU statistics
class CPUStatisticsManager
{
public:
	///Constructs a CPU statistics manager
	CPUStatisticsManager();
	
	///Resets the statistics
	void reset(int time_per_frame);
	
	///Add the data for another frame to the manager
	void addFrameData(int cpu_time_used);

	///Writes out CPU statistics chart
	void format();
private:
	int frame_number;
	int time_per_frame;
	std::vector<int> statistics;
};

#endif
