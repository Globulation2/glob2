// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "MapScriptError.h"



MapScriptError::MapScriptError(int line, int column, const std::string& message)
	: line(line), column(column), message(message)
{

}



MapScriptError::MapScriptError()
	: line(0), column(0), message("")
{

}



int MapScriptError::getLine() const
{
	return line;
}



int MapScriptError::getColumn() const
{
	return column;
}



const std::string& MapScriptError::getMessage() const
{
	return message;
}


