/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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
#include <map>
#include <vector>
#include <set>

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
	
		virtual void write(const void *data, const size_t size, const char *name);
	
		virtual void writeSint8(const Sint8 v, const char *name) { printLevel(); printString(name); printString(" = "); print<signed>(v); print(";\n"); }
		virtual void writeUint8(const Uint8 v, const char *name) { printLevel(); printString(name); printString(" = "); print<unsigned>(v); print(";\n"); }
		virtual void writeSint16(const Sint16 v, const char *name) { printLevel(); printString(name); printString(" = "); print<signed>(v); print(";\n"); }
		virtual void writeUint16(const Uint16 v, const char *name) { printLevel(); printString(name); printString(" = "); print<unsigned>(v); print(";\n"); }
		virtual void writeSint32(const Sint32 v, const char *name) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeUint32(const Uint32 v, const char *name) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeFloat(const float v, const char *name) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeDouble(const double v, const char *name) { printLevel(); printString(name); printString(" = "); print(v); print(";\n"); }
		virtual void writeText(const std::string &v, const char *name);
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
	
	//! Read data from a human readable form, C-like, supporting C and C++ comments
	class TextInputStream : public InputStream
	{
	protected:
		//! table of parsed value
		std::map<std::string, std::string> table;
		//! recursive keys
		std::vector<std::string> levels;
		//! actual complete key
		std::string key;
		
		//! Read from table using keys key and name and put result to result
		void readFromTableToString(const char *name, std::string *result);
		
		//! read from table and convert to type T using std::istringstream
		template <class T>
		T readFromTable(const char *name)
		{
			std::string s;
			readFromTableToString(name, &s);
			std::istringstream iss(s);
			T v;
			iss >> v;
			return v;
		}
		
	public:
		//! Constructor. Uses backend, but does not delete it
		TextInputStream(StreamBackend *backend);
		//! Return all subsections of root
		void getSubSections(const std::string &root, std::set<std::string> *sections);
		
		virtual void read(void *data, size_t size, const char *name);
		virtual Sint8 readSint8(const char *name) { return static_cast<Sint8>(readFromTable<signed>(name)); }
		virtual Uint8 readUint8(const char *name) { return static_cast<Uint8>(readFromTable<unsigned>(name)); }
		virtual Sint16 readSint16(const char *name) { return static_cast<Sint16>(readFromTable<signed>(name)); }
		virtual Uint16 readUint16(const char *name) { return static_cast<Uint16>(readFromTable<unsigned>(name)); }
		virtual Sint32 readSint32(const char *name) { return readFromTable<Sint32>(name); }
		virtual Uint32 readUint32(const char *name) { return readFromTable<Uint32>(name); }
		virtual float readFloat(const char *name) { return readFromTable<float>(name); }
		virtual double readDouble(const char *name) { return readFromTable<double>(name); }
		virtual std::string readText(const char *name) { std::string s; readFromTableToString(name, &s); return s; }
		
		virtual void readEnterSection(const char *name);
		virtual void readEnterSection(unsigned id);
		virtual void readLeaveSection(size_t count = 1);
		
		virtual bool canSeek(void) { return false; }
		virtual void seekFromStart(int displacement) { }
		virtual void seekFromEnd(int displacement) { }
		virtual void seekRelative(int displacement) { }
		virtual size_t getPosition(void) { return 0; }
		virtual bool isEndOfStream(void) { return false; }
	};
}

#endif
