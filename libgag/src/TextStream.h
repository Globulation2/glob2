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

// For U/SintNN
#include <Stream.h>
#include <fstream>

namespace GAGCore
{
	// Endian safe read and write
	// ==========================
	
	class TextOutputStream : public OutputStream
	{
	protected:
		unsigned level;
		std::ofstream *ofs;
		
		virtual void printLevel(void) { for (unsigned i=0; i<level; i++) *ofs << "\t"; }
		
	public:
		TextOutputStream(std::ofstream *ofs) { this->ofs = ofs; level=0; };
		virtual ~TextOutputStream() { ofs->close(); delete ofs; }
	
		virtual void write(const void *data, const size_t size, const char *name = NULL)
		{
			printLevel();
			*ofs << name << " = ";
			ofs->setf(std::ios_base::hex, std::ios_base::basefield);
			for (size_t i=0; i<size; i++)
			{
				ofs->width(2);
				ofs->fill('0');
				unsigned v = static_cast<const unsigned char *>(data)[i];
				(*ofs) << v;
			}
			ofs->setf(std::ios_base::dec, std::ios_base::basefield);
			*ofs << ";\n";
		}
	
		virtual void writeSint8(const Sint8 v, const char *name = NULL) { printLevel(); *ofs << name << " = " << v << ";\n"; }
		virtual void writeUint8(const Uint8 v, const char *name = NULL) { printLevel(); *ofs << name << " = " << v << ";\n"; }
		virtual void writeSint16(const Sint16 v, const char *name = NULL) { printLevel(); *ofs << name << " = " << v << ";\n"; }
		virtual void writeUint16(const Uint16 v, const char *name = NULL) { printLevel(); *ofs << name << " = " << v << ";\n"; }
		virtual void writeSint32(const Sint32 v, const char *name = NULL) { printLevel(); *ofs << name << " = " << v << ";\n"; }
		virtual void writeUint32(const Uint32 v, const char *name = NULL) { printLevel(); *ofs << name << " = " << v << ";\n"; }
		virtual void writeFloat(const float v, const char *name = NULL) { printLevel(); *ofs << name << " = " << v << ";\n"; }
		virtual void writeDouble(const double v, const char *name = NULL) { printLevel(); *ofs << name << " = " << v << ";\n"; }
		
		virtual void flush(void) { ofs->flush(); }
		
		virtual void writeEnterSection(const char *name) { printLevel(); *ofs << name << "\n"; printLevel(); *ofs << "{"; level++; *ofs << "\n"; }
		virtual void writeEnterSection(unsigned id) { printLevel(); *ofs << id << "\n"; printLevel(); *ofs << "{"; level++; *ofs << "\n"; }
		virtual void writeLeaveSection(size_t count = 1) { level--; printLevel(); *ofs << "}" << "\n"; }
		
		virtual bool canSeek(void) { return false; }
		virtual void seekFromStart(int displacement) { }
		virtual void seekFromEnd(int displacement) { }
		virtual void seekRelative(int displacement) { }
		virtual size_t getPosition(void) { return 0; }
		virtual bool isEndOfStream(void) { return ofs->eof(); }
	};
}

#endif
