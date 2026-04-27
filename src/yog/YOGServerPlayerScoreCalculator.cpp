/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "YOGGameResults.h"
#include "GameHeader.h"
#include "YOGServer.h"
#include <cmath>

YOGServerPlayerScoreCalculator::YOGServerPlayerScoreCalculator(YOGServer* server)
	: server(server)
{

}



void YOGServerPlayerScoreCalculator::proccessResults(YOGGameResults& results, GameHeader& header)
{
	///For each player, calculates the the score of its allies against the score of its competitors.
	for(int i=0; i<header.getNumberOfPlayers(); ++i)
	{
		if(header.getBasePlayer(i).type == BasePlayer::P_IP)
		{
			int t1 = header.getBasePlayer(i).teamNumber;
			int ally_total = 0;
			int enemy_total = 0;
			YOGPlayerStoredInfo info = server->getPlayerStoredInfoManager().getPlayerStoredInfo(header.getBasePlayer(i).name);
			int your_rating = info.getPlayerRating();
			for(int j=0; j<header.getNumberOfPlayers(); ++j)
			{
				if(header.getBasePlayer(j).type == BasePlayer::P_IP)
				{
					int t2 = header.getBasePlayer(j).teamNumber;
					const YOGPlayerStoredInfo& info2 = server->getPlayerStoredInfoManager().getPlayerStoredInfo(header.getBasePlayer(j).name);
					if(header.getAllyTeamNumber(t1) == header.getAllyTeamNumber(t2))
					{
						ally_total += info2.getPlayerRating();
					}
					else
					{
						enemy_total += info2.getPlayerRating();
					}
				}
			}
			///Calculates the expected score using the logistic formula
			double expected = 1.0 / (1.0 + std::pow(10.0, (double(enemy_total) - double(ally_total)) / 200.0));
			double change = double(your_rating) / double(ally_total) * 32.0;
			if(results.getGameResultState(header.getBasePlayer(i).name)==YOGGameResultWonGame)
			{
				///Increases player score
				info.setPlayerRating(int(double(your_rating) + change * (1.0 - expected)));
				//std::cout<<"old: "<<your_rating<<" new: "<<int(double(your_rating) + change * (1.0 - expected))<<std::endl;
			}
			else
			{
				///Decreases player score
				info.setPlayerRating(int(double(your_rating) + change * (0.0 - expected)));
				//std::cout<<"old: "<<your_rating<<" new: "<<int(double(your_rating) + change * (0.0 - expected))<<std::endl;
			}
			server->getPlayerStoredInfoManager().setPlayerStoredInfo(header.getBasePlayer(i).name, info);
		}
	}
}



