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

#ifndef __TEXTSTREAM_H
#define __TEXTSTREAM_H

#include <Stream.h>
#include <StreamBackend.h>
#include <ostream>
#include <sstream>

namespace GAGCore
{
	//! Write data in a human readable form
	class TextOutputStream : public OutputStream
	{
	protected:
		StreamBackend *backend;
		
		unsigned level;
		
		//! print levels of tabs
		void printLevel(void);
		//! print string to backend
		void printString(const char *string);
		//! print a given type using ostringstream
		template <class T>
		void print(T v)
		{
			std::ostringstream oss;
			oss << v;
			printString(oss.str().c_str());
		}
		
	public:
		TextOutputStream(StreamBackend *backend) { this->backend = backend; level=0; };
		virtual ~TextOutputStream() { delete backend; }
	
		virtual void write(const void *data, const size_t size, const char *name = NULL);
	
		virtual void writeSint8(const Sint8 v, const char *name = NULL) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeUint8(const Uint8 v, const char *name = NULL) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeSint16(const Sint16 v, const char *name = NULL) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeUint16(const Uint16 v, const char *name = NULL) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeSint32(const Sint32 v, const char *name = NULL) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeUint32(const Uint32 v, const char *name = NULL) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeFloat(const float v, const char *name = NULL) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeDouble(const double v, const char *name = NULL) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeText(const std::string &v, const char *name = NULL);
		virtual void flush(void) { backend->flush(); }
		
		virtual void writeEnterSection(const char *name);
		virtual void writeEnterSection(unsigned id);
		virtual void writeLeaveSection(size_t count = 1);
		
		virtual bool canSeek(void) { return false; }
		virtual void seekFromStart(int displacement) { }
		virtual void seekFromEnd(int displacement) { }
		virtual void seekRelative(int displacement) { }
		virtual size_t getPosition(void) { return backend->getPosition(); }
		virtual bool isEndOfStream(void) { return backend->isEndOfStream(); }
	};
}

#endif
