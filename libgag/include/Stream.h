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

#ifndef __STREAM_H
#define __STREAM_H

// For U/SintNN
#include "Types.h"

namespace GAGCore
{
	// Endian safe read and write
	// ==========================
	
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
	};
	
	class OutputStream : public Stream
	{
	public:
		virtual ~OutputStream() { }
		
		virtual void write(const void *data, const size_t size, const char *name = NULL) = 0;
		virtual void writeSint8(const Sint8 v, const char *name = NULL) = 0;
		virtual void writeUint8(const Uint8 v, const char *name = NULL) = 0;
		virtual void writeSint16(const Sint16 v, const char *name = NULL) = 0;
		virtual void writeUint16(const Uint16 v, const char *name = NULL) = 0;
		virtual void writeSint32(const Sint32 v, const char *name = NULL) = 0;
		virtual void writeUint32(const Uint32 v, const char *name = NULL) = 0;
		virtual void writeFloat(const float v, const char *name = NULL) = 0;
		virtual void writeDouble(const double v, const char *name = NULL) = 0;
		
		virtual void flush(void) = 0;
		
		virtual void writeEnterSection(const char *name) = 0;
		virtual void writeEnterSection(unsigned id) = 0;
		virtual void writeLeaveSection(size_t count = 1) = 0;
	};
	
	class InputStream : public Stream
	{
	public:
		virtual ~InputStream() { }
	
		virtual void read(void *data, size_t size, const char *name = NULL) = 0;
		virtual Sint8 readSint8(const char *name = NULL) = 0;
		virtual Uint8 readUint8(const char *name = NULL) = 0;
		virtual Sint16 readSint16(const char *name = NULL) = 0;
		virtual Uint16 readUint16(const char *name = NULL) = 0;
		virtual Sint32 readSint32(const char *name = NULL) = 0;
		virtual Uint32 readUint32(const char *name = NULL) = 0;
		virtual float readFloat(const char *name = NULL) = 0;
		virtual double readDouble(const char *name = NULL) = 0;
		
		virtual void readEnterSection(const char *name) = 0;
		virtual void readEnterSection(unsigned id) = 0;
		virtual void readLeaveSection(size_t count = 1) = 0;
	};
}

#endif
