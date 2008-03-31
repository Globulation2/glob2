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

#ifndef __BINARYSTREAM_H
#define __BINARYSTREAM_H

#include <Stream.h>
#include <StreamBackend.h>
#include "../../gnupg/sha1.h"

namespace GAGCore
{
	class BinaryOutputStream : public OutputStream
	{
	protected:
		StreamBackend *backend;
		SHA1_CTX sha1Context;
		bool doingSHA1;
	public:
		BinaryOutputStream(StreamBackend *backend) { this->backend = backend; doingSHA1 = false;}
		virtual ~BinaryOutputStream() { delete backend; }
	
		virtual void write(const void *data, const size_t size, const char *name);
	
		virtual void writeEndianIndependant(const void *v, const size_t size, const char *name);
	
		virtual void writeSint8(const Sint8 v, const char *name) { this->write(&v, 1, name); }
		virtual void writeUint8(const Uint8 v, const char *name) { this->write(&v, 1, name); }
		virtual void writeSint16(const Sint16 v, const char *name) { this->writeEndianIndependant(&v, 2, name); }
		virtual void writeUint16(const Uint16 v, const char *name) { this->writeEndianIndependant(&v, 2, name); }
		virtual void writeSint32(const Sint32 v, const char *name) { this->writeEndianIndependant(&v, 4, name); }
		virtual void writeUint32(const Uint32 v, const char *name) { this->writeEndianIndependant(&v, 4, name); }
		virtual void writeFloat(const float v, const char *name) { this->writeEndianIndependant(&v, 4, name); }
		virtual void writeDouble(const double v, const char *name) { this->writeEndianIndependant(&v, 8, name); }
		virtual void writeText(const std::string &v, const char *name);
		
		virtual void flush(void) { backend->flush(); }
		
		virtual void writeEnterSection(const char *name) { }
		virtual void writeEnterSection(unsigned id) { }
		virtual void writeLeaveSection(size_t count = 1) { }
		
		void enableSHA1();
		void finishSHA1(Uint8 sha1[20]);
		
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
	
		virtual void read(void *data, size_t size, const char *name) { backend->read(data, size); }
	
		virtual void readEndianIndependant(void *v, size_t size, const char *name);
	
		virtual Sint8 readSint8(const char *name) { Sint8 i; this->read(&i, 1, name); return i; }
		virtual Uint8 readUint8(const char *name) { Uint8 i; this->read(&i, 1, name); return i; }
		virtual Sint16 readSint16(const char *name) { Sint16 i; this->readEndianIndependant(&i, 2, name); return i; }
		virtual Uint16 readUint16(const char *name) { Uint16 i; this->readEndianIndependant(&i, 2, name); return i; }
		virtual Sint32 readSint32(const char *name) { Sint32 i; this->readEndianIndependant(&i, 4, name); return i; }
		virtual Uint32 readUint32(const char *name) { Uint32 i; this->readEndianIndependant(&i, 4, name); return i; }
		virtual float readFloat(const char *name) { float f; this->readEndianIndependant(&f, 4, name); return f; }
		virtual double readDouble(const char *name) { double d; this->readEndianIndependant(&d, 8, name); return d; }
		virtual std::string readText(const char *name);
		
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
