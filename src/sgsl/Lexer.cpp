// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat
// Copyright (C) 2001-2008 Luc-Olivier de Charrière
// Copyright (C) 2001-2008 Martin S. Nyffenegger

/*!	\file Lexer.cpp
	\brief SGSL token table, error strings and tokenizer
*/

#include <cassert>
#include <cstddef>
#include <iostream>
#include <string>
#include <string.h>

#include <Toolkit.h>
#include <StringTable.h>

#include "SGSL.h"

using std::string;
using GAGCore::Toolkit;

SGSLToken::TokenSymbolLookupTable SGSLToken::table[] =
{
	{ INT, "int" },
	{ STRING, "string" },
	{ LANG, "lang" },
	{ FUNC_CALL, "function call" },

	{ S_PAROPEN, "("},
	{ S_PARCLOSE, ")"},
	{ S_SEMICOL, ","},
	{ S_STORY, "story" },

	{ S_EQUAL, "=" },
	{ S_HIGHER, ">" },
	{ S_LOWER, "<" },
	{ S_NOT, "not" },

	{ S_WAIT, "wait" },
	{ S_SPACE, "space" },
	{ S_TIMER, "timer" },
	{ S_SHOW, "show" },
	{ S_HIDE, "hide" },
	{ S_ALLIANCE, "alliance"},
	{ S_GUIENABLE, "guiEnable"},
	{ S_GUIDISABLE, "guiDisable"},
	{ S_SUMMONUNITS, "summonUnits" },
	{ S_SUMMONFLAG, "summonFlag" },
	{ S_DESTROYFLAG, "destroyFlag" },
	{ S_WIN, "win" },
	{ S_LOOSE, "loose" },
	{ S_LABEL, "label" },
	{ S_JUMP, "jump" },
	{ S_SETAREA, "setArea"},
	{ S_AREA, "area" },
	{ S_ISDEAD, "isdead" },
	{ S_ALLY, "ally" },
	{ S_ENEMY, "enemy" },
	{ S_ONLY, "only" },

	{ S_WORKER, "Worker" },
	{ S_EXPLORER, "Explorer" },
	{ S_WARRIOR, "Warrior" },
	{ S_SWARM_B, "Swarm" },
	{ S_FOOD_B, "Inn" },
	{ S_HEALTH_B, "Hospital" },
	{ S_WALKSPEED_B, "Racetrack" },
	{ S_SWIMSPEED_B, "Pool" },
	{ S_ATTACK_B, "Camp" },
	{ S_SCIENCE_B, "School" },
	{ S_DEFENCE_B, "Tower" },
	{ S_MARKET_B, "Market"},
	{ S_WALL_B, "Wall"},
	{ S_EXPLOR_F, "ExplorationFlag"},
	{ S_FIGHT_F, "WarFlag"},
	{ S_CLEARING_F, "ClearingFlag"},
	{ S_ALLIANCESCREEN, "AllianceScreen"},
	{ S_BUILDINGTAB, "BuildingTab"},
	{ S_FLAGTAB, "FlagTab"},
	{ S_TEXTSTATTAB, "TextStatTab"},
	{ S_GFXSTATTAB, "GfxStatTab"},

	// NIL must be at the end because it is a stop condition... not very clean
	{ NIL, "" },
};

SGSLToken::TokenType SGSLToken::getTypeByName(const std::string name)
{
	int i = 0;
	TokenType type=NIL;

	if (name.empty())
	{
		std::cerr << "Warning, SGSLToken::getTypeByName(name) called with empty name!" << std::endl;
		return NIL;
	}
	
	while (table[i].type != NIL)
	{
		//NOTE: SM: I reverted back to case-insensitive, as the other one breaks tutorial
		if (strcasecmp(name.c_str(), table[i].name.c_str())==0)
		{
			type = table[i].type;
			break;
		}
		i++;
	}
	
	if (type == NIL)
	{
		std::cerr << "Warning, SGSLToken::getTypeByName(name) found no type for name " << name << "!" << std::endl;
	}
	
	return type;
}

std::string SGSLToken::getNameByType(SGSLToken::TokenType type)
{
	int i = 0;
	std::string name="";

	if (type != NIL)
		while (!table[i].name.empty())
		{
			if (type == table[i].type)
			{
				name = table[i].name;
				break;
			}
			i++;
		}
	return name;
}

std::string ErrorReport::getErrorString(void) const
{
	static const std::string strings[]={
		"No error",
		"Invalid Value ",
		"Syntax error",
		"Invalid team",
		"No such file",
		"Area name not defined",
		"Area name already defined",
		"Label not defined",
		"Missing \"(\"",
		"Missing \")\"",
		"Missing \",\"",
		"Missing argument",
		"Invalid alliance level. Level must be between 0 and 3",
		"Not a valid language identifier",
		"Summing of a specific level is only valid for buildings",
		"The type of the argument to the function is wrong",
		"Unknown error"
	};
	assert(type >= 0);
	assert(type < ET_NB_ET);
	assert(ET_NB_ET == std::size(strings));
	return strings[(int)type];
}

//Text aquisition by the parser
Aquisition::~Aquisition(void)
{

}

Aquisition::Aquisition(const Functions& functions) :
	functions(functions)
{
	token.type=SGSLToken::NIL;
	actLine=0;
	actCol=0;
	actPos=0;
	lastLine=0;
	lastCol=0;
	lastPos=0;
	newLine=true;
}

#define HANDLE_ERROR_POS(c) { actPos++; if (c=='\n') { actLine++; actCol=0; } else { actCol++; } }
#undef getc


//Tokenizer
void Aquisition::nextToken()
{
	string word;
	int c;
	lastCol=actCol;
	lastLine=actLine;
	lastPos=actPos;
	// eat empty char
	while(( c=this->getChar() )!=EOF)
	{
		if (c=='#' && newLine)
		{
			while ((c!=EOF) && (c!='\n'))
			{
				c=this->getChar();
				HANDLE_ERROR_POS(c);
			}
		}
		newLine=false;
		if (strchr(" \t\r\n", c)==NULL)
		{
			this->ungetChar(c);
			break;
		}
		else if (c == '\n')
		{
			newLine=true;
		}
		HANDLE_ERROR_POS(c);
	}

	if (c==EOF)
	{
		token.type=SGSLToken::S_EOF;
		return;
	}

	// push char in word
	bool isInString=false;
	bool isInMot = false;
	while(( c=this->getChar() )!=EOF)
	{
		if ((char)c=='"')
			isInString=!isInString;
		if (isInString)
		{
			if (strchr("\t\r\n", c)!=NULL)
			{
				if (c == '\n')
					newLine=true;
				this->ungetChar(c);
				break;
			}
		}
		else
		{
			if (strchr(" \t\r\n", c)!=NULL)
			{
				if (c == '\n')
					newLine=true;

				this->ungetChar(c);
				break;
			}
			else if (strchr("().,", c)!=NULL)
			{
				if (isInMot)
					this->ungetChar(c);
				else
				{
					//no need to come back
					HANDLE_ERROR_POS(c);
					word+= (char)c;
				}
				break;
			}
			isInMot=true;
		}
		HANDLE_ERROR_POS(c);
		word+= (char)c;
	}


	if (word.size()>0)
	{
		if ((word[0]>='0') && (word[0]<='9'))
		{
			token.type = SGSLToken::INT;
			token.value = atoi(word.c_str());
		}
		else if (word[0]=='"')
		{
			string::size_type start=word.find_first_of("\"");
			string::size_type end=word.find_last_of("\"");
			if ((start!=string::npos) && (end!=string::npos))
			{
				token.type = SGSLToken::STRING;
				assert(end-start-1>=0);
				token.msg = word.substr(start+1, end-start-1);
			}
			else
				token.type = SGSLToken::NIL;
		}
		else
		{
			// is it a function call ?
			Functions::const_iterator fIt = functions.find(word);
			if (fIt != functions.end())
			{
				token.type = SGSLToken::FUNC_CALL;
				token.msg = word;
				return;
			}
			
			// is it a language ?
			for (int i=0; i<Toolkit::getStringTable()->getNumberOfLanguage(); i++)
			{
				if (word == std::string(Toolkit::getStringTable()->getStringInLang("[language-code]", i)))
				{
					token.type = SGSLToken::LANG;
					token.value = i;
					token.msg = word;
					return;
				}
			}
			
			// so it is another token
			token.type = SGSLToken::getTypeByName(word.c_str());
		}
	}
	else
		token.type = SGSLToken::NIL;
}

bool FileAquisition::open(const std::string filename)
{
	if (fp != NULL)
		fclose(fp);
	if ((fp = fopen(filename.c_str(),"r")) == NULL)
	{
		fprintf(stderr,"SGSL : Can't open file %s\n", filename.c_str());
		return false;
	}
	return true;
}


StringAquisition::StringAquisition(const Functions& functions) :
	Aquisition(functions)
{
	pos=0;
}

StringAquisition::~StringAquisition()
{
	
}

void StringAquisition::open(const std::string& text)
{
	buffer = text;
	pos=0;
}

int StringAquisition::getChar(void)
{
	if (pos < int(buffer.length()))
	{
		return (buffer[pos++]);
	}
	else
		return EOF;
}

int StringAquisition::ungetChar(char c)
{
	if (pos > 0)
	{
		buffer[--pos]=c;
	}
	return 0;
}
