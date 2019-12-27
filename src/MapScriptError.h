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
