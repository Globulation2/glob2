// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <string>
#include <sstream>

namespace GAGCore {
	/*!
	* string that can be used for argument substitution.
	* Example :
	* FormatableString fs("Hello %0");
	* cout << fs.arg("World");
	*/
	class FormatableString : public std::string
	{
		private:
			/*!
			* Next argument to be replaced.
			*/
			int argLevel;
		
			/*!
			* Replace the next argument by replacement.
			*/
			void proceedReplace(const std::string &replacement);
			
		public:
			
			FormatableString() : std::string(), argLevel(0) { }
			/*!
			* Creates a new FormatableString with format string set to s.
			* \param s A string with indicators for argument substitution.
			* Each indicator is the % symbol followed by a number. The number
			* is the index of the corresponding argument (starting at %0).
			*/
			FormatableString(const std::string &s)
		: std::string(s), argLevel(0) { }
			
			/*!
			* Replace the next arg by an int value.
			* \param value Value used to replace the current argument.
			* \param fieldWidth min width of the displayed number
			* \param base Radix of the number (8, 10 or 16)
			* \param fillChar Character used to pad the number to reach fieldWidth
			* \see arg(const T& value)
			*/
			FormatableString &arg(int value, int fieldWidth = 0, int base = 10, char fillChar = ' ');
			
			/*!
			* Replace the next arg by an int value.
			* \param value Value used to replace the current argument.
			* \param fieldWidth min width of the displayed number
			* \param base Radix of the number (8, 10 or 16)
			* \param fillChar Character used to pad the number to reach fieldWidth
			* \see arg(const T& value)
			*/
			FormatableString &arg(unsigned value, int fieldWidth = 0, int base = 10, char fillChar = ' ');
			
			/*!
			* Replace the next arg by a float value.
			* \param value Value used to replace the current argument.
			* \param fieldWidth min width of the displayed number.
			* \param precision Number of digits displayed.
			* \param fillChar Character used to pad the number to reach fieldWidth.
			* \see arg(const T& value)
			*/
			FormatableString &arg(float value, int fieldWidth = 0, int precision = 6, char fillChar = ' ');
			
			/*!
			* Replace the next arg by a value that can be passed to an ostringstream.
			* The first call to arg replace %0, the second %1, and so on.
			* \param value Value used to replace the current argument.
			*/
			template <typename T> FormatableString &arg(const T& value)
			{
				// transform value into std::string
				std::ostringstream oss;
				oss << value;
			
				proceedReplace(oss.str());
	
				// return reference to this so that .arg can proceed further
				return *this;
			}
			
			/*!
			* Affects a new value to the format string and reset the arguments
			* counter.
			* \param str New format string.
			*/
			FormatableString& operator=(const std::string& str) ;
			
			/*!
			* Casts this string to a const char*
			*/
			operator const char*() { return this->c_str(); }
	};
}
