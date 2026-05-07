// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
		if(pos == std::string::npos)
		{
			this->append(replacement);
		}
		else
		{
			this->replace(pos, search.str().length(), replacement);
		}
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
