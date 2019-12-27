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

#include <StreamBackend.h>
#include <iostream>
#include "SDL_net.h"

namespace GAGCore
{

	ZLibStreamBackend::ZLibStreamBackend(const std::string& file, bool read)
	{
		this->file = file;
		isRead = read;
		buffer = new MemoryStreamBackend();
		if(isRead && isValid())
		{
			gzFile fp = gzopen(file.c_str(), "rb");
			while(!gzeof(fp))
			{
				unsigned char b[1024];
				long ammount = gzread(fp, b, 1024);
				buffer->write(b, ammount);
			}
			buffer->seekFromStart(0);
		}
	}

	ZLibStreamBackend::~ZLibStreamBackend()
	{
		if(!isRead && isValid())
		{
			buffer->seekFromEnd(0);
			long size = buffer->getPosition();
			buffer->seekFromStart(0);
			gzFile fp = gzopen(file.c_str(), "wb9");
			gzwrite(fp, buffer->getBuffer(), size);
			gzclose(fp);
		}
	}

	void ZLibStreamBackend::write(const void *data, const size_t size)
	{
		buffer->write(data, size);
	}

	void ZLibStreamBackend::flush(void)
	{
		buffer->flush();
	}

	void ZLibStreamBackend::read(void *data, size_t size)
	{
		buffer->read(data, size);
	}

	void ZLibStreamBackend::putc(int c)
	{
		buffer->putc(c);
	}

	int ZLibStreamBackend::getChar(void)
	{
		return buffer->getChar();
	}

	void ZLibStreamBackend::seekFromStart(int displacement)
	{
		buffer->seekFromStart(displacement);
	}

	void ZLibStreamBackend::seekFromEnd(int displacement)
	{
		buffer->seekFromEnd(displacement);
	}

	void ZLibStreamBackend::seekRelative(int displacement)
	{
		buffer->seekRelative(displacement);
	}

	size_t ZLibStreamBackend::getPosition(void)
	{
		return buffer->getPosition();
	}

	bool ZLibStreamBackend::isEndOfStream(void)
	{
		return !isValid();
	}

	bool ZLibStreamBackend::isValid(void)
	{
		return (file.size()>0 && buffer->isValid());
	}

	MemoryStreamBackend::MemoryStreamBackend(const void *data, const size_t size)
	{
		index = 0;
		if (size)
			write(data, size);
	}

	void MemoryStreamBackend::write(const void *data, const size_t size)
	{
		const char *_data = static_cast<const char *>(data);
		if ((index + size) > datas.size())
			datas.resize(index + size);
		std::copy(_data, _data+size, datas.begin()+index);
		index += size;
	}

	void MemoryStreamBackend::read(void *data, size_t size)
	{
		char *_data = static_cast<char *>(data);
		if (index+size > datas.size())
		{
			// overread, read 0
			std::fill(_data, _data+size, 0);
		}
		else
		{
			std::copy(datas.data() + index, datas.data() + index + size, _data);
			index += size;
		}
	}

	void MemoryStreamBackend::putc(int c)
	{
		Uint8 ch = c;
		write(&ch, 1);
	}

	//TODO: Why is the Uint8 ch returned as int?
	int MemoryStreamBackend::getChar(void)
	{
		Uint8 ch;
		read(&ch, 1);
		return ch;
	}

	void MemoryStreamBackend::seekFromStart(int displacement)
	{
		index = std::min(static_cast<size_t>(displacement), datas.size());
	}

	void MemoryStreamBackend::seekFromEnd(int displacement)
	{
		index = static_cast<size_t>(std::max(0, static_cast<int>(datas.size()) - displacement));
	}

	void MemoryStreamBackend::seekRelative(int displacement)
	{
		int newIndex = static_cast<int>(index) + displacement;
		newIndex = std::max(newIndex, 0);
		newIndex = std::min(newIndex, static_cast<int>(datas.size()));
		index = static_cast<size_t>(newIndex);
	}

	size_t MemoryStreamBackend::getPosition(void)
	{
		return index;
	}

	bool MemoryStreamBackend::isEndOfStream(void)
	{
		return index >= datas.size();
	}

	void HashStreamBackend::write(const void *data, const size_t size)
	{
		unsigned char *p = (unsigned char *)data; // Pointer to data
		unsigned char *e = p + size; // Pointer to the end of the data

		// FNV-1a hash each byte in the buffer
		while (p < e)
		{
			// xor the least significant bits of the hash with the current byte
			hash ^= (Uint32)(*p);

			// Multiply by the 32 bit FNV magic prime mod 2^32
			hash *= 0x01000193;

			p++;
		}
	}

	void HashStreamBackend::putc(int c)
	{
		c = (unsigned int)c;

		while (c != 0)
		{
			// xor the least significant bits of the hash with the current byte
			hash ^= (Uint32)(c|0xFF);

			// Multiply by the 32 bit FNV magic prime mod 2^32
			hash *= 0x01000193;

			// Next byte
			c >>= 8;
		}
	}
}
