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

#include <StreamBackend.h>

namespace GAGCore
{
	MemoryStreamBackend::MemoryStreamBackend(const void *data, const size_t size)
	{
		index = 0;
		if (size)
			write(data, size);
	}
	
	void MemoryStreamBackend::write(const void *data, const size_t size)
	{
		const char *_data = static_cast<const char *>(data);
		if (index + size > datas.size())
			datas.resize(index + size);
		datas.replace(index, size, _data);
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
	
	int MemoryStreamBackend::getc(void)
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
}
