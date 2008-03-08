/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include <cassert>
#include <string>
#include <sstream>
#include <ios>
#include <iostream>
#include <iomanip>

#include <FormatableString.h>

namespace GAGCore {
	void FormatableString::proceedReplace(const std::string &replacement)
	{
		std::ostringstream search;
		search << "%" << this->argLevel;
		std::string::size_type pos = this->find(search.str(), 0);
		assert(pos != std::string::npos);
		this->replace(pos, search.str().length(), replacement);
		++argLevel;
	}
	
	FormatableString &FormatableString::arg(int value, int fieldWidth, int base, char fillChar)
	{
		std::ostringstream oss;
		oss << std::setbase(base);
		oss.width(fieldWidth);
		oss.fill(fillChar);
		
		// transform value into std::string
		oss << value;
	
		proceedReplace(oss.str());
		
		// return reference to this so that .arg can proceed further
		return *this;
	}
	
	FormatableString &FormatableString::arg(unsigned value, int fieldWidth, int base, char fillChar)
	{
		std::ostringstream oss;
		oss << std::setbase(base);
		oss.width(fieldWidth);
		oss.fill(fillChar);
		
		// transform value into std::string
		oss << value;
	
		proceedReplace(oss.str());
		
		// return reference to this so that .arg can proceed further
		return *this;
	}
	
	FormatableString &FormatableString::arg(float value, int fieldWidth, int precision, char fillChar)
	{
		std::ostringstream oss;
		oss.precision(precision);
		oss.width(fieldWidth);
		oss.fill(fillChar);
	
		oss.setf(oss.fixed, oss.floatfield);
		// transform value into std::string
		oss << value;
	
		proceedReplace(oss.str());
		
		// return reference to this so that .arg can proceed further
		return *this;
	}
	
	FormatableString &FormatableString::operator=(const std::string& str)
	{
		this->assign(str);
		this->argLevel = 0;
		return (*this);
	}
}
