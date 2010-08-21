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

#ifndef __STREAM_H
#define __STREAM_H

// For U/SintNN
#include "Types.h"
#include <string>

namespace GAGCore
{
	//! A stream is a high-level serialization structure, used to read/write structured datas
	class Stream
	{
	public:
		virtual ~Stream() { }
		virtual bool canSeek(void) = 0;
		virtual void seekFromStart(int displacement) = 0;
		virtual void seekFromEnd(int displacement) = 0;
		virtual void seekRelative(int displacement) = 0;
		virtual size_t getPosition(void) = 0;
		virtual bool isEndOfStream(void) = 0;
		virtual bool isValid(void) = 0;
	};
	
	//! The stream that can be written to
	class OutputStream : public Stream
	{
	public:
		virtual ~OutputStream() { }
		
		virtual void write(const void *data, const size_t size, const std::string name) = 0;
		virtual void writeSint8(const Sint8 v, const std::string name) = 0;
		virtual void writeUint8(const Uint8 v, const std::string name) = 0;
		virtual void writeSint16(const Sint16 v, const std::string name) = 0;
		virtual void writeUint16(const Uint16 v, const std::string name) = 0;
		virtual void writeSint32(const Sint32 v, const std::string name) = 0;
		virtual void writeUint32(const Uint32 v, const std::string name) = 0;
		virtual void writeFloat(const float v, const std::string name) = 0;
		virtual void writeDouble(const double v, const std::string name) = 0;
		virtual void writeText(const std::string &v, const std::string name) = 0;
		
		virtual void flush(void) = 0;
		
		virtual void writeEnterSection(const std::string name) = 0;
		virtual void writeEnterSection(unsigned id) = 0;
		virtual void writeLeaveSection(size_t count = 1) = 0;
	};
	
	//! The stream that can be read from
	class InputStream : public Stream
	{
	public:
		virtual ~InputStream() { }
	
		virtual void read(void *data, size_t size, const std::string name) = 0;
		virtual Sint8 readSint8(const std::string name) = 0;
		virtual Uint8 readUint8(const std::string name) = 0;
		virtual Sint16 readSint16(const std::string name) = 0;
		virtual Uint16 readUint16(const std::string name) = 0;
		virtual Sint32 readSint32(const std::string name) = 0;
		virtual Uint32 readUint32(const std::string name) = 0;
		virtual float readFloat(const std::string name) = 0;
		virtual double readDouble(const std::string name) = 0;
		virtual std::string readText(const std::string name) = 0;
		
		virtual void readEnterSection(const std::string name) = 0;
		virtual void readEnterSection(unsigned id) = 0;
		virtual void readLeaveSection(size_t count = 1) = 0;
	};
	
	class StreamBackend;
	
	//! Stream used to write line by line
	class OutputLineStream
	{
	private:
		StreamBackend *backend;
	
	public:
		OutputLineStream(StreamBackend *backend);
		virtual ~OutputLineStream();
		void writeLine(const std::string &s);
		void writeLine(const char *s);
		bool isEndOfStream(void);
	};
	
	//! Stream used to read line by line
	class InputLineStream
	{
	private:
		StreamBackend *backend;
	
	public:
		InputLineStream(StreamBackend *backend);
		virtual ~InputLineStream();
		std::string readLine(void);
		bool isEndOfStream(void);
	};
}

#endif
