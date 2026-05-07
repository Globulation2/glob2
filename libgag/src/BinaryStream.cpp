// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
	void BinaryOutputStream::write(const void *data, const size_t size, const std::string name)
	{
		if(doingSHA1)
			SHA1Update(&sha1Context, (const Uint8*)data, size);
		backend->write(data, size);
	}
	
	void BinaryOutputStream::writeEndianIndependant(const void *v, const size_t size, const std::string name)
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
		if(doingSHA1)
			SHA1Update(&sha1Context, (const Uint8*)v, size);
		backend->write(v, size);
	}
	
	void BinaryOutputStream::writeText(const std::string &v, const std::string name)
	{
		writeUint32(v.size(), "");
		write(v.c_str(), v.size(), "");
	}
	
	void BinaryOutputStream::enableSHA1()
	{
		doingSHA1=true;
		SHA1Init(&sha1Context);
	}
	
	
	void BinaryOutputStream::finishSHA1(Uint8 sha1[20])
	{
		doingSHA1=false;
		SHA1Final(sha1, &sha1Context);
	}
	
	void BinaryInputStream::readEndianIndependant(void *v, size_t size, const std::string name)
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
	
	std::string BinaryInputStream::readText(const std::string name)
	{
		size_t len = readUint32("");
		std::valarray<char> buffer(len+1);
		read(&buffer[0], len, "");
		buffer[len] = 0;

		// We don't use strings longer than 1024*1024, so if len > 1024*1024 these bits don't represent a string.
		if (len > 1024*1024)
		{
			// TODO: Make a BadFileFormatException (or similar) class and if necessary update the catch'es at
			//  - ChooseMapScreen.cpp : 167
			//  - Engine.cpp : 218, 688, 754, 932
			//  - MapEdit.cpp : 1135
			throw std::ios_base::failure("String "+name+" length > 1024*1024");
		}

		return std::string(&buffer[0]);
	}
}
