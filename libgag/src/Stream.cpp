// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
			int c = backend->getChar();
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
