// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef MapScriptError_h
#define MapScriptError_h

#include <string>

///This class represents an error in the map script
class MapScriptError
{
public:
	///Constructs a map script error
	MapScriptError(int line, int column, const std::string& message);
	
	///Constructs a blank error
	MapScriptError();

	///Returns the line of the error
	int getLine() const;
	
	///Returns the column of the error
	int getColumn() const;
	
	///Returns the message of the error
	const std::string& getMessage() const;

private:
	int line;
	int column;
	std::string message;
};


#endif
