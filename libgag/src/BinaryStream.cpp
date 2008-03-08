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

#include <BinaryStream.h>

#include <valarray>
#include <assert.h>
// For htons/htonl
#ifdef WIN32
	#include <windows.h>
#else
	#include <netinet/in.h>
#endif

namespace GAGCore
{
	void BinaryOutputStream::writeEndianIndependant(const void *v, const size_t size, const char *name)
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
		else
			assert(false);
		backend->write(v, size);
	}
	
	void BinaryOutputStream::writeText(const std::string &v, const char *name)
	{
		writeUint32(v.size(), NULL);
		write(v.c_str(), v.size(), NULL); 
	}
	
	void BinaryInputStream::readEndianIndependant(void *v, size_t size, const char *name)
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
		else
			assert(false);
	}
	
	std::string BinaryInputStream::readText(const char *name)
	{
		size_t len = readUint32(NULL);
		std::valarray<char> buffer(len+1);
		read(&buffer[0], len, NULL);
		buffer[len] = 0;
		return std::string(&buffer[0]);
	}
}
