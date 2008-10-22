/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charri�e
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

#include <Stream.h>
#include <StreamBackend.h>
#include <string.h>

namespace GAGCore
{
	OutputLineStream::OutputLineStream(StreamBackend *backend)
	{
		this->backend = backend;
	}
	
	OutputLineStream::~OutputLineStream()
	{
		delete backend;
	}
	
	InputLineStream::InputLineStream(StreamBackend *backend)
	{
		this->backend = backend;
	}
	
	InputLineStream::~InputLineStream()
	{
		delete backend;
	}
	
	void OutputLineStream::writeLine(const std::string &s)
	{
		backend->write(s.c_str(), s.length());
		backend->putc('\n');
	}
	
	void OutputLineStream::writeLine(const char *s)
	{
		backend->write(s, strlen(s));
		backend->putc('\n');
	}
	
	std::string InputLineStream::readLine()
	{
		std::string s;
		while (1)
		{
			int c = backend->getc();
			if(c=='\r')
				continue;
			if ((c >= 0) && (c != '\n'))
				s += c;
			else
				break;
		}
		return s;
	}
	
	bool OutputLineStream::isEndOfStream(void)
	{
		return backend->isEndOfStream();
	}
	
	bool InputLineStream::isEndOfStream(void)
	{
		return backend->isEndOfStream();
	}
}
