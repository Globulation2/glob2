/*
  Copyright (C) 2007 Bradley Arsenault

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
