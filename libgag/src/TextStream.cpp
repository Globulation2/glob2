/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <TextStream.h>
#include <fstream>

namespace GAGCore
{
	void TextOutputStream::printLevel(void)
	{
		for (unsigned i=0; i<level; i++)
			backend->putc('\t');
	}
	
	void TextOutputStream::printString(const char *string)
	{
		backend->write(string, strlen(string));
	}
	
	void TextOutputStream::write(const void *data, const size_t size, const char *name)
	{
		printLevel();
		if (name)
		{
			printString(name);
			printString(" = ");
		}
		const unsigned char *dataChars = static_cast<const unsigned char *>(data);
		for (size_t i=0; i<size; i++)
		{
			char buffer[4];
			snprintf(buffer, sizeof(buffer), "%02x ", dataChars[i]);
			printString(buffer);
		}
		printString(";\n");
	}
	
	void TextOutputStream::writeText(const std::string &v, const char *name)
	{
		printLevel();
		if (name)
		{
			printString(name);
			printString(" = \"");
		}
		for (size_t i = 0; i<v.size(); i++)
			if (v[i] == '\"')
				printString("\\\"");
			else
				backend->putc(v[i]);
		printString("\";\n");
	}
	
	void TextOutputStream::writeEnterSection(const char *name)
	{
		printLevel();
		printString(name);
		printString("\n");
		printLevel();
		printString("{\n");
		level++;
	}
	
	void TextOutputStream::writeEnterSection(unsigned id)
	{
		printLevel();
		print(id);
		printString("\n");
		printLevel();
		printString("{\n");
		level++;
	}
	
	void TextOutputStream::writeLeaveSection(size_t count)
	{
		level -= count;
		printLevel();
		printString("}\n");
	}
}
