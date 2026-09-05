// SPDX-License-Identifier: GPL-3.0-or-later

/*!	\file SGSLParseContext.h
	\brief State shared by the statement parsers of one MapScriptSGSL::parseScript run
*/

#pragma once

#include "SGSL.h"

//! Outcome of parsing one statement
enum class SGSLParseStatus
{
	Ok,       //!< statement consumed, keep going
	Failed,   //!< error recorded; the story so far is still closed and kept
	Aborted,  //!< error recorded; parsing stops at once and the story is dropped
};

//! Token cursor, story under construction and error report of a parse run.
//! The error position always names the token consumed just before the error.
struct SGSLParseContext
{
	Aquisition *donnees;
	Game *game;
	Story *story;
	int numberOfTeams;
	ErrorReport &er;

	const SGSLToken &token() const { return *donnees->getToken(); }

	//! Appends the current token to the story being built
	void pushToken() { story->line.push_back(token()); }

	//! Records the current token position as the error position, then advances
	void nextToken()
	{
		er.line=donnees->getLine();
		er.col=donnees->getCol();
		er.pos=donnees->getPos();
		donnees->nextToken();
	}

	//! Advances and checks the new token's type, recording error on mismatch
	bool expect(SGSLToken::TokenType type, ErrorReport::ErrorType error)
	{
		nextToken();
		if (token().type != type)
		{
			er.type=error;
			return false;
		}
		return true;
	}
	bool expectParOpen() { return expect(SGSLToken::S_PAROPEN, ErrorReport::ET_MISSING_PAROPEN); }
	bool expectParClose() { return expect(SGSLToken::S_PARCLOSE, ErrorReport::ET_MISSING_PARCLOSE); }
	bool expectSemicol() { return expect(SGSLToken::S_SEMICOL, ErrorReport::ET_MISSING_SEMICOL); }

	//! Checks that the current token is an argument rather than a closing ")" or ","
	bool hasArgument()
	{
		if (token().type == SGSLToken::S_PARCLOSE || token().type == SGSLToken::S_SEMICOL)
		{
			er.type=ErrorReport::ET_MISSING_ARGUMENT;
			return false;
		}
		return true;
	}

	//! "(" then the first argument
	bool openArguments()
	{
		if (!expectParOpen())
			return false;
		nextToken();
		return hasArgument();
	}

	//! "," then the next argument
	bool nextArgument()
	{
		if (!expectSemicol())
			return false;
		nextToken();
		return hasArgument();
	}

	//! ")" then the token after it
	bool closeArguments()
	{
		if (!expectParClose())
			return false;
		nextToken();
		return true;
	}

	//! Records a syntax error unless the current token is an int
	bool requireInt()
	{
		if (token().type != SGSLToken::INT)
		{
			er.type=ErrorReport::ET_SYNTAX_ERROR;
			return false;
		}
		return true;
	}

	//! Records a syntax error unless the current token is a string
	bool requireString()
	{
		if (token().type != SGSLToken::STRING)
		{
			er.type=ErrorReport::ET_SYNTAX_ERROR;
			return false;
		}
		return true;
	}

	//! Records an error unless the current token is an int naming an existing team
	bool requireTeam()
	{
		if (!requireInt())
			return false;
		if (token().value >= numberOfTeams)
		{
			er.type=ErrorReport::ET_INVALID_TEAM;
			return false;
		}
		return true;
	}

	//! Records error and reports the statement as failed
	SGSLParseStatus fail(ErrorReport::ErrorType error)
	{
		er.type=error;
		return SGSLParseStatus::Failed;
	}

	//! Records error and reports the parse as aborted
	SGSLParseStatus abort(ErrorReport::ErrorType error)
	{
		er.type=error;
		return SGSLParseStatus::Aborted;
	}
};
