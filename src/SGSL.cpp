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
21	show(string): diplays a string on the screen until the next command is finished
22	show(string,time in seconds): displays a string for time seconds on the screen
3	player(number).order: gives orders to players, orders are
1   -activate: activates the given player (on by default)
2   -deactivate: deactivate the given player
3   -friend: makes a player become your friend
4   -ennemy: makes a player become your ennemy (on by default)
5   -dead: return true if the player is dead
6   -alive: returns true if the player is alive
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

Story::Story()
{
	hasWon=false;
	hasLost=false;
}

bool Story::testCondition()
{
	/*TODO for Steph, lˆ je peux que te faire la structure de base si tu veux,
	d'ailleurs pour les test ( wait(x > y) ) il faudrai rajouter tous les noms de variables ˆ Token non ?
	*/

	if (line.size())
		switch (line.front().type)
		{
			case (Token::S_TIMER):
							/* Il faut entrer le code pour le timer*/
							return true;
			
			case (Token::S_SHOW):
			{
				line.pop_front();
				cout<< line.front().msg;
				return true;
			}
			
			case (Token::S_WIN):
			{
				hasWon=true;
				return true;
			}
			
			case (Token::S_LOOSE):
			{
				hasLost=true;
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
		line.pop_front();
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

void Aquisition::nextToken()
{
	string mot;
	int c;

	// eat empty char
	while((c=fgetc(fp))!=EOF)
	{
		if (index(" \t\r\n().,", c)==NULL)
		{
			ungetc(c, fp);
			break;
		}
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

		mot.push_back((char)c);
	}

	if (c==EOF)
	{
		token.type=Token::S_EOF;
		return;
	}

	if (mot.size()>0)
	{
		if ((mot[0]>0) && (mot[0]<9))
		{
			token.type = Token::INT;
			token.value = atoi(mot.c_str());
		}
		else if (mot[0]=='"')
		{
			string::size_type start=mot.find_first_of("\"");
			string::size_type end=mot.find_last_of("\"");
			if ((start!=string::npos) && (end!=string::npos))
				token.msg = mot.substr(start+1, end-start-1);
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
		else if (mot=="=")
		{
			token.type=Token::S_LOWER;
		}
		else if (mot=="player")
		{
			token.type=Token::S_PLAYER;
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
		else
		{
			token.type=Token::NIL;
		}
	}
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
	game=NULL;
	stories.clear();
}

void Mapscript::step()
{
	for (std::deque<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
	{
		(*it).step();
	}
}


bool Mapscript::loadScript(const char *filename, Game *game)
{
	if (donnees.newFile(filename))
	{
		reset();
		this->game=game;
		
		donnees.nextToken();
		while (donnees.getToken().type != Token::S_EOF)
		{
			Story thisone;
			while ((donnees.getToken().type != Token::S_STORY) && (donnees.getToken().type !=Token::S_EOF))
			{
				thisone.line.push_back(donnees.getToken());
				donnees.nextToken();
			}
			stories.push_back(thisone);
			printf("SGSL : story loaded, %d tokens\n", thisone.line.size());
			donnees.nextToken();
		}
		return true;
	}
	else
	{
		return false;
	}
}

bool Mapscript::hasTeamWon(unsigned teamNumber)
{
	// Can make win or loose only player 0
	if (teamNumber==0)
	{
		for (std::deque<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
		{
			if ((*it).hasWon)
				return true;
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