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

#ifndef __STREAMBACKEND_H
#define __STREAMBACKEND_H

// For U/SintNN
#include <Types.h>
#include <stdio.h>

namespace GAGCore
{
	//! A backend for a stream, can be a file, a network socket, ...
	class StreamBackend
	{
	public:
		virtual ~StreamBackend() { }
		
		virtual void write(const void *data, const size_t size) = 0;
		virtual void flush(void) = 0;
		virtual void read(void *data, size_t size) = 0;
		virtual void putc(int c) = 0;
		virtual int getc(void) = 0;
		virtual void seekFromStart(int displacement) = 0;
		virtual void seekFromEnd(int displacement) = 0;
		virtual void seekRelative(int displacement) = 0;
		virtual size_t getPosition(void) = 0;
		virtual bool isEndOfStream(void) = 0;
	};
	
	//! The FILE* implementation of stream backend
	class FileStreamBackend : public StreamBackend
	{
	private:
		FILE *fp;
		
	public:
		FileStreamBackend(FILE *fp) { this->fp = fp; }
		virtual ~FileStreamBackend() { fclose(fp); }
		
		virtual void write(const void *data, const size_t size) { fwrite(data, size, 1 ,fp); }
		virtual void flush(void) { fflush(fp); }
		virtual void read(void *data, size_t size) { fread(data, size, 1, fp); }
		virtual void putc(int c) { fputc(c, fp); }
		virtual int getc(void) { return fgetc(fp); }
		virtual void seekFromStart(int displacement) { fseek(fp, displacement, SEEK_SET); }
		virtual void seekFromEnd(int displacement) { fseek(fp, displacement, SEEK_END); }
		virtual void seekRelative(int displacement) { fseek(fp, displacement, SEEK_CUR); }
		virtual size_t getPosition(void) { return ftell(fp); }
		virtual bool isEndOfStream(void) { return feof(fp) != 0; }
	};
}

#endif
