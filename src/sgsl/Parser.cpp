// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat
// Copyright (C) 2001-2008 Luc-Olivier de Charrière
// Copyright (C) 2001-2008 Martin S. Nyffenegger

/*!	\file Parser.cpp
	\brief SGSL script parser: story loop, statement dispatch and the simple statements
*/

#include <algorithm>
#include <iostream>

#include "Game.h"
#include "SGSL.h"
#include "SGSLParseContext.h"

using std::cerr;
using std::endl;

// Control of the syntax of the script
ErrorReport MapScriptSGSL::parseScript(Aquisition *donnees, Game *game)
{
	ErrorReport er;
	er.type=ErrorReport::ET_OK;

	reset();

	// Set the size of the won/lost arrays and clear them
	hasWon.resize(game->mapHeader.getNumberOfTeams());
	std::fill(hasWon.begin(), hasWon.end(), false);
	hasLost.resize(game->mapHeader.getNumberOfTeams());
	std::fill(hasLost.begin(), hasLost.end(), false);

	SGSLParseContext ctx{donnees, game, nullptr, game->mapHeader.getNumberOfTeams(), er};
	ctx.nextToken();
	while (ctx.token().type != SGSLToken::S_EOF)
	{
		Story thisone(this);
		if (er.type != ErrorReport::ET_OK)
		{
			break;
		}
		ctx.story = &thisone;
		while ((ctx.token().type != SGSLToken::S_STORY) && (ctx.token().type != SGSLToken::S_EOF))
		{
			if (er.type != ErrorReport::ET_OK)
			{
				break;
			}
			if (parseStatement(ctx) == SGSLParseStatus::Aborted)
				return er;
		}
		thisone.line.push_back(SGSLToken(SGSLToken::S_STORY));
		stories.push_back(thisone);
		ctx.nextToken();
	}
	return er;
}

// Grammar check of the statement starting at the current token
SGSLParseStatus MapScriptSGSL::parseStatement(SGSLParseContext &ctx)
{
	switch (ctx.token().type)
	{
		case (SGSLToken::FUNC_CALL):
			return parseFunctionCall(ctx);
		case (SGSLToken::S_SUMMONUNITS):
			return parseSummonUnits(ctx);
		case (SGSLToken::S_SETAREA):
			return parseSetArea(ctx);
		case (SGSLToken::S_SUMMONFLAG):
			return parseSummonFlag(ctx);
		case (SGSLToken::S_DESTROYFLAG):
			return parseDestroyFlag(ctx);
		case (SGSLToken::S_ALLIANCE):
			return parseAlliance(ctx);
		case (SGSLToken::S_SHOW):
		case (SGSLToken::S_LABEL):
		case (SGSLToken::S_JUMP):
			return parseShowLabelJump(ctx);
		case (SGSLToken::S_WAIT):
			return parseWait(ctx);
		case (SGSLToken::NIL):
			return ctx.fail(ErrorReport::ET_UNKNOWN);
		case (SGSLToken::S_TIMER):
			return parseTimer(ctx);
		case (SGSLToken::S_GUIENABLE):
		case (SGSLToken::S_GUIDISABLE):
			return parseGUIChoice(ctx);
		case (SGSLToken::S_LOOSE):
		case (SGSLToken::S_WIN):
			return parseWinLoose(ctx);
		case (SGSLToken::S_HIDE):
		case (SGSLToken::S_SPACE):
			ctx.pushToken();
			ctx.nextToken();
			return SGSLParseStatus::Ok;
		default:
			cerr << "SGSL: unknown token found: " << ctx.token().type << endl;
			return ctx.fail(ErrorReport::ET_UNKNOWN);
	}
}

// name(arg, ...) with the arguments described by the function's table
SGSLParseStatus MapScriptSGSL::parseFunctionCall(SGSLParseContext &ctx)
{
	ctx.pushToken();

	Functions::const_iterator fIt = functions.find(ctx.token().msg);
	assert(fIt != functions.end());
	const FunctionArgumentDescription *argument = fIt->second.first;

	if (!ctx.openArguments())
		return SGSLParseStatus::Aborted;

	while (true)
	{
		int argumentTokenType = ctx.token().type;
		if ((argumentTokenType < argument->argRangeFirst) || (argumentTokenType > argument->argRangeLast))
			return ctx.abort(ErrorReport::ET_WRONG_FUNCTION_ARGUMENT);

		ctx.pushToken();

		argument++;
		if (argument->argRangeFirst<0)
			break;

		if (!ctx.nextArgument())
			return SGSLParseStatus::Aborted;
	}

	if (!ctx.closeArguments())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}

// show("text" [, lang]), label("name"), jump("name")
SGSLParseStatus MapScriptSGSL::parseShowLabelJump(SGSLParseContext &ctx)
{
	SGSLToken::TokenType type = ctx.token().type;
	Story &story = *ctx.story;

	ctx.pushToken();
	if (!ctx.openArguments())
		return SGSLParseStatus::Aborted;
	if (!ctx.requireString())
		return SGSLParseStatus::Failed;

	if (type == SGSLToken::S_LABEL)
	{
		// add label to table
		story.labels[ctx.token().msg] = story.line.size();
	}
	if (type == SGSLToken::S_JUMP)
	{
		// complain if label doesn't exists
		if (story.labels.find(ctx.token().msg) == story.labels.end())
			return ctx.fail(ErrorReport::ET_UNDEFINED_LABEL);
	}

	if (type == SGSLToken::S_SHOW)
	{
		ctx.pushToken();
		ctx.nextToken();
		if (ctx.token().type != SGSLToken::S_PARCLOSE)
		{
			// This is a multilingual show
			if (ctx.token().type != SGSLToken::S_SEMICOL)
				return ctx.fail(ErrorReport::ET_SYNTAX_ERROR);
			ctx.nextToken();
			if (!ctx.hasArgument())
				return SGSLParseStatus::Aborted;
			if (ctx.token().type != SGSLToken::LANG)
				return ctx.fail(ErrorReport::ET_NOT_VALID_LANG_ID);
			ctx.pushToken();
			if (!ctx.closeArguments())
				return SGSLParseStatus::Aborted;
		}
		else
		{
			ctx.nextToken();
		}
		return SGSLParseStatus::Ok;
	}

	ctx.pushToken();
	if (!ctx.closeArguments())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}

// timer(int)
SGSLParseStatus MapScriptSGSL::parseTimer(SGSLParseContext &ctx)
{
	ctx.pushToken();
	if (!ctx.openArguments())
		return SGSLParseStatus::Aborted;
	if (!ctx.requireInt())
		return SGSLParseStatus::Failed;
	ctx.pushToken();
	if (!ctx.closeArguments())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}

// guiEnable(object), guiDisable(object)
SGSLParseStatus MapScriptSGSL::parseGUIChoice(SGSLParseContext &ctx)
{
	ctx.pushToken();
	if (!ctx.openArguments())
		return SGSLParseStatus::Aborted;
	if ((ctx.token().type < SGSLToken::S_WORKER) || (ctx.token().type > SGSLToken::S_ALLIANCESCREEN))
		return ctx.fail(ErrorReport::ET_INVALID_VALUE);
	ctx.pushToken();
	if (!ctx.closeArguments())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}

// win(team), loose(team)
SGSLParseStatus MapScriptSGSL::parseWinLoose(SGSLParseContext &ctx)
{
	ctx.pushToken();
	if (!ctx.openArguments())
		return SGSLParseStatus::Aborted;
	if (!ctx.requireTeam())
		return SGSLParseStatus::Failed;
	ctx.pushToken();
	if (!ctx.closeArguments())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}
