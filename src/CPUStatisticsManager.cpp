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

#include "CPUStatisticsManager.h"
#include "Stream.h"
#include "Toolkit.h"
#include "boost/lexical_cast.hpp"
#include "GlobalContainer.h"

using namespace GAGCore;

CPUStatisticsManager::CPUStatisticsManager()
	: frame_number(0), time_per_frame(40)
{
}



void CPUStatisticsManager::reset(int atime_per_frame)
{
	frame_number = 0;
	time_per_frame = atime_per_frame;
	statistics.clear();
}



void CPUStatisticsManager::addFrameData(int cpu_time_used)
{
	if(frame_number % 10 == 0)
		statistics.push_back(cpu_time_used);
	frame_number+=1;
}



void CPUStatisticsManager::format()
{
/*
	std::string logName = "logs/";
	logName += globalContainer->getUsername();
	logName += "CPU.log";
	OutputLineStream* stream = new OutputLineStream(Toolkit::getFileManager()->openOutputStreamBackend(logName));
	stream->writeLine("Time      CPU usage");
	for(int i=0; i<20; ++i)
	{
		std::string line="";
		int total_time = frame_number * time_per_frame * (i+1) / 20;
		int seconds = (total_time / 1000) % 60;
		int minutes = (total_time / 1000) / 60;
		line+=boost::lexical_cast<std::string>(minutes) + ":";
		if(seconds < 10)
			line+= "0" + boost::lexical_cast<std::string>(seconds);
		else
			line+= boost::lexical_cast<std::string>(seconds);

		while(line.size() < 10)
			line += " ";

		int total_cpu_time_consumed=0;
		int total_recorded=0;
		for(int j = (statistics.size() * (i) / 20); j < (statistics.size() * (i+1) / 20); ++j)
		{
			total_cpu_time_consumed += statistics[j];
			total_recorded += 1;
		}

		float cpu_usage = (float)(total_cpu_time_consumed) / (float)(total_recorded * time_per_frame);
		line += boost::lexical_cast<std::string>(cpu_usage);

		stream->writeLine(line);
	}

	delete stream;
*/
}

