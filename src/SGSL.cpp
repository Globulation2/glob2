/*
    Copyright (C) 2002 Stephane Magnenat & Martin Nyffenegger
    for any question or comment contact us at nct@ysagoon.com or msnyffenegger@gmx.ch

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/*
Types associations with int numbers.
11	wait(time in seconds): time to wait before exectuting the next command
12	timer(time in seconds): same thing as wait but is drawn on the screen
13	wait(condition)
        condition:  untit_or_building number_of_player condition number 
21	show(string): diplays a string on the screen until the next command is finished
1   activate: activates the given player (on by default)
2   deactivate: deactivate the given player
3   friend: makes a player become your friend
4   ennemy: makes a player become your ennemy (on by default)

5   dead number_of_player: return true if the player is dead
6   alive number_of_player: returns true if the player is alive
4	flag(number).order: give orders to flags
1   -you: returns true if one of your globule reaches the radius of the given flag
2   -noenemy: returns true if no enemy is in the radius of the given flag
3   -noally: returns true if no ally is in the radius of the given flag
51	win: obwiously makes you win the game
52	loose: bad for you...

//condition number
player xx -dead		xx0
player xx -alive	xx1
flag xx -you		xx5
flag xx -noennemy	xx6
flag xx -noally		xx7

for the parser:
story: starts another parallel storyline, so multiple endings for a map are possible
*/

#include <iostream>
#include <string>
#include <deque>
#include "SGSL.h"
#include "Game.h"

Story::Story(Mapscript *mapscript)
{
	lineSelector = 0;
	internTimer=0;
	hasWon=false;
	hasLost=false;
	this->mapscript=mapscript;
}

Story::~Story()
{
	
}

int Story::valueOfVariable(Token nameOfVariable,int numberOfPlayer)
{
	switch(nameOfVariable.type)
	{
		case(Token::S_WORKER):
			return mapscript->game->teams[numberOfPlayer]->latestStat.numberUnitPerType[0];
		case(Token::S_EXPLORER):
			return mapscript->game->teams[numberOfPlayer]->latestStat.numberUnitPerType[1];
		case(Token::S_WARRIOR):
			return mapscript->game->teams[numberOfPlayer]->latestStat.numberUnitPerType[2];
		case(Token::S_SWARM_B):
			return mapscript->game->teams[numberOfPlayer]->latestStat.numberBuildingPerType[0];
		case(Token::S_FOOD_B):
			return mapscript->game->teams[numberOfPlayer]->latestStat.numberBuildingPerType[1];
		case(Token::S_HEALTH_B):
			return mapscript->game->teams[numberOfPlayer]->latestStat.numberBuildingPerType[2];
		case(Token::S_WALKSPEED_B):
			return mapscript->game->teams[numberOfPlayer]->latestStat.numberBuildingPerType[3];
		case(Token::S_FLYSPEED_B):
			return mapscript->game->teams[numberOfPlayer]->latestStat.numberBuildingPerType[4];
		case(Token::S_ATTACK_B):
			return mapscript->game->teams[numberOfPlayer]->latestStat.numberBuildingPerType[5];
		case(Token::S_SCIENCE_B):
			return mapscript->game->teams[numberOfPlayer]->latestStat.numberBuildingPerType[6];
		case(Token::S_DEFENCE_B):
			return mapscript->game->teams[numberOfPlayer]->latestStat.numberBuildingPerType[7]; 
		default:
			return 0;
	}
}


bool Story::conditionTester()
{
	switch (line[lineSelector+3].type)
	{
		case (Token::S_HIGHER):
		{
			return (valueOfVariable(line[lineSelector+1],line[lineSelector+2].value) > line[lineSelector+4].value);
		}
		case (Token::S_LOWER):
		{
			return (valueOfVariable(line[lineSelector+1],line[lineSelector+2].value) < line[lineSelector+4].value);
		}
		case (Token::S_EQUAL):
		{
			return (valueOfVariable(line[lineSelector+1],line[lineSelector+2].value) == line[lineSelector+4].value);
		}
		default:
			return false;		
	}
}

bool Story::testCondition()
{

	if (line.size())
		switch (line[lineSelector].type)
		{
			case (Token::S_SHOW):
			{
				mapscript->isTextShown = true;
				lineSelector++;
				mapscript->textShown = line[lineSelector].msg;
				return true;
			}
			
			case (Token::S_WIN):
			{
				hasWon=true;
				return false;
			}
			
			case (Token::S_LOOSE):
			{
				hasLost=true;
				return false;
			}
			
			case (Token::S_TIMER):
			{
				lineSelector++;
				mapscript->mainTimer=line[lineSelector].value;
				return true;
			}
			
			case (Token::S_WAIT):
			{
				switch (line[lineSelector+1].type)
				{
					case(Token::INT):
					{
						lineSelector++;
						internTimer=line[lineSelector].value;
						return false;
					}
					case (Token::S_DEAD):
					{
						if (!mapscript->game->teams[line[lineSelector+2].value]->isAlive)
						{
							lineSelector+=2;
							return true;
						}
						else
							return false;
					}
					case (Token::S_ALIVE):
					{
						if (mapscript->game->teams[line[lineSelector+2].value]->isAlive)
						{
							lineSelector+=2;
							return true;
						}
						else
							return false;
					}
					case (Token::S_FLAG):
					{
						// TODO: STEPH: Need flags
						lineSelector +=3;
						return true;
					}
					default: //Test conditions
					{
						if (conditionTester())
						{
							lineSelector +=4;
							mapscript->isTextShown=false;
							return true;
						}
						else
							return false;
					}
				}
			}
			case (Token::S_MARK):
			{
				lineSelector += 1;
				return true;
			}
			case (Token::S_GOBACKTO):
			{
				//TODO traiter les erreurs en cas de nonexistance du token !
				int newEmplacement;
				for (int i = lineSelector; i > 0; i--)
				{
					if (line[lineSelector+1].msg == line[i].msg)
						newEmplacement = i;
						break;
				}
				lineSelector = newEmplacement;
				return true;
			}
			case (Token::INT):
			{
				internTimer--;
				if (internTimer==0)
					return true;
				else
					return false;
			}
			//Here the only possibilities are: friend,activate,deactivate,enemy
			//TODO make the right action ! HELP STEPH !!!
			case (Token::S_FRIEND):
			{
				mapscript->game->teams[line[lineSelector+1].value]->allies |= mapscript->game->teams[line[lineSelector+2].value]->me;
				mapscript->game->teams[line[lineSelector+1].value]->enemies = ~ mapscript->game->teams[line[lineSelector+1].value]->allies;
				lineSelector +=2;
				return true;
			}
			case (Token::S_ENEMY):
			{
				mapscript->game->teams[line[lineSelector+1].value]->allies &= ~ mapscript->game->teams[line[lineSelector+2].value]->me;
				mapscript->game->teams[line[lineSelector+1].value]->enemies = ~ mapscript->game->teams[line[lineSelector+1].value]->allies;
				lineSelector +=2;
				return true;
			}
			case (Token::S_ACTIVATE):
			{
				lineSelector ++;
				return true;
			}
			case (Token::S_DEACTIVATE):
			{
				lineSelector ++;
				return true;
			}
			
			case (Token::S_HIDE):
			{
				mapscript->isTextShown = false;
				return true;
			}
			
			default:
				return false;
		}

	return false;
}

void Story::step()
{
	if (testCondition())
	{
		lineSelector ++;
	}
}

//Aquisition du texte par le parseur
using namespace std;

Aquisition::~Aquisition(void)
{
	if (fp)
		fclose(fp);
}

Aquisition::Aquisition(void)
{
	fp=NULL;
	token.type=Token::NIL;
	actLine=0;
	actCol=0;
	lastLine=0;
	lastCol=0;
}

bool Aquisition::newFile(const char *filename)
{
	if (fp != NULL)
		fclose(fp);
	if ((fp = fopen(filename,"r")) == NULL)
	{
		fprintf(stderr,"SGSL : Can't open file %s\n", filename);
		return false;
	}
	return true;
}

#define HANDLE_NL(c) if (c=='\n') { actLine++; actCol=0; }

void Aquisition::nextToken()
{
	string mot;
	int c;
	
	lastCol=actCol;
	lastLine=actLine;

	// eat empty char
	while((c=fgetc(fp))!=EOF)
	{
		if (index(" \t\r\n().,", c)==NULL)
		{
			ungetc(c, fp);
			break;
		}
		HANDLE_NL(c);
	}

	if (c==EOF)
	{
		token.type=Token::S_EOF;
		return;
	}

	// push char in mot
	bool isInString=false;
	while((c=fgetc(fp))!=EOF)
	{
		if ((char)c=='"')
			isInString=!isInString;
		if (isInString)
		{
			if (index("\t\r\n", c)!=NULL)
			{
				ungetc(c, fp);
				break;
			}
		}
		else
		{
			if (index(" \t\r\n().,", c)!=NULL)
			{
				ungetc(c, fp);
				break;
			}
		}
		HANDLE_NL(c);
		mot+= (char)c;
	}

	if (mot.size()>0)
	{
		if ((mot[0]>='0') && (mot[0]<='9'))
		{
			token.type = Token::INT;
			token.value = atoi(mot.c_str());
		}
		else if (mot[0]=='"')
		{
			string::size_type start=mot.find_first_of("\"");
			string::size_type end=mot.find_last_of("\"");
			if ((start!=string::npos) && (end!=string::npos))
			{
				token.type = Token::STRING;
				token.msg = mot.substr(start+1, end-start-1);
			}
			else
				token.type = Token::NIL;
		}
		else if (mot=="wait")
		{
			token.type=Token::S_WAIT;
		}
		else if (mot=="=")
		{
			token.type=Token::S_EQUAL;
		}
		else if (mot==">")
		{
			token.type=Token::S_HIGHER;
		}
		else if (mot=="<")
		{
			token.type=Token::S_LOWER;
		}
		else if (mot=="timer")
		{
			token.type=Token::S_TIMER;
		}
		else if (mot=="show")
		{
			token.type=Token::S_SHOW;
		}
		else if (mot=="activate")
		{
			token.type=Token::S_ACTIVATE;
		}
		else if (mot=="deactivate")
		{
			token.type=Token::S_DEACTIVATE;
		}
		else if (mot=="friend")
		{
			token.type=Token::S_FRIEND;
		}
		else if (mot=="enemy")
		{
			token.type=Token::S_ENEMY;
		}
		else if (mot=="dead")
		{
			token.type=Token::S_DEAD;
		}
		else if (mot=="alive")
		{
			token.type=Token::S_ALIVE;
		}
		else if (mot=="flag")
		{
			token.type=Token::S_FLAG;
		}
		else if (mot=="you")
		{
			token.type=Token::S_YOU;
		}
		else if (mot=="noenemy")
		{
			token.type=Token::S_NOENEMY;
		}
		else if (mot=="win")
		{
			token.type=Token::S_WIN;
		}
		else if (mot=="loose")
		{
			token.type=Token::S_LOOSE;
		}
		else if (mot=="story")
		{
			token.type=Token::S_STORY;
		}
        else if (mot=="nbWorker")
		{
			token.type=Token::S_WORKER;
		}
		else if (mot=="nbExplorer")
		{
			token.type=Token::S_EXPLORER;
		}
		else if (mot=="nbWarrior")
		{
			token.type=Token::S_WARRIOR;
		}
		else if (mot=="nbSwarmBuilding")
		{
			token.type=Token::S_SWARM_B;
		}
		else if (mot=="nbFoodBuilding")
		{
			token.type=Token::S_FOOD_B;
		}
		else if (mot=="nbHealthBuilding")
		{
			token.type=Token::S_HEALTH_B;
		}
		else if (mot=="nbWalkBuilding")
		{
			token.type=Token::S_WALKSPEED_B;
		}
		else if (mot=="nbFlyBuilding")
		{
			token.type=Token::S_FLYSPEED_B;
		}
		else if (mot=="nbAttackBuilding")
		{
			token.type=Token::S_ATTACK_B;
		}
		else if (mot=="nbScienceBuilding")
		{
			token.type=Token::S_SCIENCE_B;
		}
		else if (mot == "nbDefenceBuilding")
		{
			token.type=Token::S_DEFENCE_B;
		}
		else if (mot == "hide")
		{
			token.type=Token::S_HIDE;
		}
		else if (mot == "mark")
		{
			token.type=Token::S_MARK;
		}
		else if (mot == "gobackto")
		{
			token.type=Token::S_GOBACKTO;
		}
		else
		{
			token.type=Token::NIL;
		}
	}
	else
		token.type = Token::NIL;
}

// Mapscript creation

Mapscript::Mapscript()
{
	reset();
}

Mapscript::~Mapscript(void)
{}

void Mapscript::reset(void)
{
	isTextShown = false;
	mainTimer=0;
	game=NULL;
	stories.clear();
}

bool Mapscript::testMainTimer()
{ 
	return (mainTimer <= 0);
}

void Mapscript::step()
{
	if (mainTimer)
		mainTimer--;
	for (std::deque<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
	{
		(*it).step();
	}
}


ErrorReport Mapscript::loadScript(const char *filename, Game *game)
{
	ErrorReport er;
	er.type=ErrorReport::ET_OK;
	if (donnees.newFile(filename))
	{
		reset();
		this->game=game;
		
		donnees.nextToken();
		while (donnees.getToken().type != Token::S_EOF)
		{
			Story thisone(this);
			if (er.type != ErrorReport::ET_OK)
				break;
			while ((donnees.getToken().type != Token::S_STORY) && (donnees.getToken().type !=Token::S_EOF))
			{
				// Grammar check
				er.line=donnees.getLine();
				er.col=donnees.getCol();
				switch (donnees.getToken().type)
				{
					case (Token::S_SHOW):
					case (Token::S_MARK):
					case (Token::S_GOBACKTO):
					{
						cout << donnees.getToken().type << endl;
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::STRING)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
					}
					break;
					case (Token::S_WAIT):
					{
						cout << donnees.getToken().type << endl;
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if ((donnees.getToken().type == Token::S_DEAD) || (donnees.getToken().type == Token::S_ALIVE))
						{
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
							if (donnees.getToken().type != Token::INT)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
						}
						else if (donnees.getToken().type == Token::S_FLAG)
						{
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
							if (donnees.getToken().type != Token::INT)
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
							if ((donnees.getToken().type != Token::S_YOU) || (donnees.getToken().type != Token::S_NOENEMY))
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
						}
						else if (donnees.getToken().type == Token::INT)
						{
							cout << donnees.getToken().type;
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
						}
						else if ((donnees.getToken().type > 100) && (donnees.getToken().type < 300))
						{
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
							if (donnees.getToken().type == Token::INT)
							{
								thisone.line.push_back(donnees.getToken());
								donnees.nextToken();
								if ((donnees.getToken().type > 300) && (donnees.getToken().type < 304))
								{
									thisone.line.push_back(donnees.getToken());
									donnees.nextToken();
									if (donnees.getToken().type == Token::INT)
									{
										thisone.line.push_back(donnees.getToken());
										donnees.nextToken();
									}
									else
									{
										er.type=ErrorReport::ET_SYNTAX_ERROR;
										break;
									}
								}
								else
								{
									er.type=ErrorReport::ET_SYNTAX_ERROR;
									break;
								}
							}
							else
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
						}
						else
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
					}
					break;
					case (Token::NIL):
					{
						donnees.nextToken();
					}
					break;
					case (Token::S_FRIEND):
					case (Token::S_ENEMY):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type == Token::INT)
						{
							thisone.line.push_back(donnees.getToken());
							donnees.nextToken();
							if (donnees.getToken().type == Token::INT)
							{
								thisone.line.push_back(donnees.getToken());
								donnees.nextToken();
							
							}
							else
							{
								er.type=ErrorReport::ET_SYNTAX_ERROR;
								break;
							}
						}
						else
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
					}
					break;
					case (Token::S_TIMER):
					case (Token::S_ACTIVATE):
					case (Token::S_DEACTIVATE):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
						if (donnees.getToken().type != Token::INT)
						{
							er.type=ErrorReport::ET_SYNTAX_ERROR;
							break;
						}
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
					}
					break;
					case (Token::S_WIN):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
					}
					break;
					case (Token::S_LOOSE):
					{
						thisone.line.push_back(donnees.getToken());
						donnees.nextToken();
					}
					break;
					case (Token::S_EOF):
					{
					}
					break;
					default:
						cout << donnees.getToken().type << endl;
						er.type=ErrorReport::ET_SYNTAX_ERROR;
						break;
				}
			}
			stories.push_back(thisone);
			printf("SGSL : story loaded, %d tokens, dumping now :\n", (int)thisone.line.size());
			for (unsigned  i=0; i<thisone.line.size(); i++)
				cout << "Token type " << thisone.line[i].type << endl;
			donnees.nextToken();
		}
	}
	else
	{
		er.type=ErrorReport::ET_UNKNOWN;
	}
	return er;
}

bool Mapscript::hasTeamWon(unsigned teamNumber)
{
	// Can make win or loose only player 0
	//Impement Timer feature (can't win while timer is working !)
	if (testMainTimer())
	{
		if (teamNumber==0)
		{
			for (std::deque<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
			{
				if ((*it).hasWon)
				return true;
			}
		}
	}
	return false;
}

bool Mapscript::hasTeamLost(unsigned teamNumber)
{
	// Can make win or loose only player 0
	if (teamNumber==0)
	{
		for (std::deque<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
		{
			if ((*it).hasLost)
				return true;
		}
	}
	return false;
}
