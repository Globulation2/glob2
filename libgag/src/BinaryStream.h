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

#ifndef __BINARYSTREAM_H
#define __BINARYSTREAM_H

#include <Stream.h>
#include "StreamBackend.h"

// For htons/htonl
#ifdef WIN32
	#include <windows.h>
#else
	#include <netinet/in.h>
#endif
#include <stdio.h>
#include <string>
#include <valarray>

namespace GAGCore
{
	class BinaryOutputStream : public OutputStream
	{
	private:
		StreamBackend *backend;
		
	public:
		BinaryOutputStream(StreamBackend *backend) { this->backend = backend; }
		virtual ~BinaryOutputStream() { delete backend; }
	
		virtual void write(const void *data, const size_t size, const char *name = NULL) { backend->write(data, size); }
	
		virtual void writeEndianIndependant(const void *v, const size_t size, const char *name)
		{
			if (size==2)
			{
				*(Uint16 *)v = htons(*(Uint16 *)v);
			}
			else if (size==4)
			{
				*(Uint32 *)v = htonl(*(Uint32 *)v);
			}
			else if (size==8)
			{
				*(Uint32 *)v = htonl(*(Uint32 *)v);
				*((Uint32 *)v+1) = htonl(*((Uint32 *)v+1));
			}
			backend->write(v, size);
		}
	
		virtual void writeSint8(const Sint8 v, const char *name = NULL) { this->write(&v, 1, name); }
		virtual void writeUint8(const Uint8 v, const char *name = NULL) { this->write(&v, 1, name); }
		virtual void writeSint16(const Sint16 v, const char *name = NULL) { this->writeEndianIndependant(&v, 2, name); }
		virtual void writeUint16(const Uint16 v, const char *name = NULL) { this->writeEndianIndependant(&v, 2, name); }
		virtual void writeSint32(const Sint32 v, const char *name = NULL) { this->writeEndianIndependant(&v, 4, name); }
		virtual void writeUint32(const Uint32 v, const char *name = NULL) { this->writeEndianIndependant(&v, 4, name); }
		virtual void writeFloat(const float v, const char *name = NULL) { this->writeEndianIndependant(&v, 4, name); }
		virtual void writeDouble(const double v, const char *name = NULL) { this->writeEndianIndependant(&v, 8, name); }
		virtual void writeText(const std::string &v, const char *name = NULL)
		{
			writeUint32(v.size());
			write(v.c_str(), v.size()); 
		}
		
		virtual void flush(void) { backend->flush(); }
		
		virtual void writeEnterSection(const char *name) { }
		virtual void writeEnterSection(unsigned id) { }
		virtual void writeLeaveSection(size_t count = 1) { }
		
		virtual bool canSeek(void) { return true; }
		virtual void seekFromStart(int displacement) { backend->seekFromStart(displacement); }
		virtual void seekFromEnd(int displacement) { backend->seekFromEnd(displacement); }
		virtual void seekRelative(int displacement) { backend->seekRelative(displacement); }
		virtual size_t getPosition(void) { return backend->getPosition(); }
		virtual bool isEndOfStream(void) { return backend->isEndOfStream(); }
	};
	
	class BinaryInputStream : public InputStream
	{
	private:
		StreamBackend *backend;
		
	public:
		BinaryInputStream(StreamBackend *backend) { this->backend = backend; }
		virtual ~BinaryInputStream() { delete backend; }
	
		virtual void read(void *data, size_t size, const char *name = NULL) { backend->read(data, size); }
	
		virtual void readEndianIndependant(void *v, size_t size, const char *name)
		{
			backend->read(v, size);
			if (size==2)
			{
				*(Uint16 *)v = ntohs(*(Uint16 *)v);
			}
			else if (size==4)
			{
				*(Uint32 *)v = ntohl(*(Uint32 *)v);
			}
			else if (size==8)
			{
				*(Uint32 *)v = ntohl(*(Uint32 *)v);
				*((Uint32 *)v+1) = ntohl(*((Uint32 *)v+1));
			}
		}
	
		virtual Sint8 readSint8(const char *name = NULL) { Sint8 i; this->read(&i, 1, name); return i; }
		virtual Uint8 readUint8(const char *name = NULL) { Uint8 i; this->read(&i, 1, name); return i; }
		virtual Sint16 readSint16(const char *name = NULL) { Sint16 i; this->readEndianIndependant(&i, 2, name); return i; }
		virtual Uint16 readUint16(const char *name = NULL) { Uint16 i; this->readEndianIndependant(&i, 2, name); return i; }
		virtual Sint32 readSint32(const char *name = NULL) { Sint32 i; this->readEndianIndependant(&i, 4, name); return i; }
		virtual Uint32 readUint32(const char *name = NULL) { Uint32 i; this->readEndianIndependant(&i, 4, name); return i; }
		virtual float readFloat(const char *name = NULL) { float f; this->readEndianIndependant(&f, 4, name); return f; }
		virtual double readDouble(const char *name = NULL) { double d; this->readEndianIndependant(&d, 8, name); return d; }
		virtual std::string readText(const char *name = NULL)
		{
			std::valarray<char> buffer(readUint32());
			read(&buffer[0], buffer.size());
			return std::string(&buffer[0]);
		}
		
		virtual void readEnterSection(const char *name) { }
		virtual void readEnterSection(unsigned id) { }
		virtual void readLeaveSection(size_t count = 1) { }
		
		virtual bool canSeek(void) { return true; }
		virtual void seekFromStart(int displacement) { backend->seekFromStart(displacement); }
		virtual void seekFromEnd(int displacement) { backend->seekFromEnd(displacement); }
		virtual void seekRelative(int displacement) { backend->seekRelative(displacement); }
		virtual size_t getPosition(void) { return backend->getPosition(); }
		virtual bool isEndOfStream(void) { return backend->isEndOfStream(); }
	};
}

#endif
