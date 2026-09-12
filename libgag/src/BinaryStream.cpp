// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <BinaryStream.h>

#include <string>
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
	
	void BinaryOutputStream::writeEndianIndependent(const void *v, const size_t size, const std::string name)
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

	// Same bytes and hash as writeUint16 per value: sections are not stored.
	void BinaryOutputStream::writeUint16Sections(const Uint16 *values, size_t count, const std::string name)
	{
		Uint8 bytes[16384];
		while (count > 0)
		{
			const size_t chunk = count < sizeof(bytes) / 2 ? count : sizeof(bytes) / 2;
			for (size_t i = 0; i < chunk; ++i)
			{
				bytes[2*i] = static_cast<Uint8>(values[i] >> 8);
				bytes[2*i+1] = static_cast<Uint8>(values[i]);
			}
			if(doingSHA1)
				SHA1Update(&sha1Context, bytes, chunk * 2);
			backend->write(bytes, chunk * 2);
			values += chunk;
			count -= chunk;
		}
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
	
	void BinaryInputStream::read(void *data, size_t size, const std::string name)
	{
		if (size == 0) return;
		if (checkedReads)
		{
			if (!backend->readExact(data, size))
				throw std::ios_base::failure("Incomplete binary field: " + name);
		}
		else
			backend->read(data, size);
	}

	void BinaryInputStream::readEndianIndependent(void *v, size_t size, const std::string name)
	{
		read(v, size, name);
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
	
	// Upper bound on any string written by writeText. Beyond this the bits on
	// the wire are taken to be garbage rather than a real string. Mirrors the
	// 1 MiB cap used by NetSendOrder::decodeData (see MAX_NET_SEND_ORDER_SIZE).
	constexpr size_t MAX_BINARY_STRING_LENGTH = 1024 * 1024;

	std::string BinaryInputStream::readText(const std::string name)
	{
		size_t len = readUint32("");

		// We don't use strings longer than MAX_BINARY_STRING_LENGTH, so beyond that the bits don't represent a string.
		if (len > MAX_BINARY_STRING_LENGTH)
		{
			// TODO: Make a BadFileFormatException (or similar) class and if necessary update the catch'es at
			//  - ChooseMapScreen.cpp : 167
			//  - Engine.cpp : 218, 688, 754, 932
			//  - MapEdit.cpp : 1135
			throw std::ios_base::failure("String "+name+" length > "+std::to_string(MAX_BINARY_STRING_LENGTH));
		}

		std::valarray<char> buffer(len+1);
		read(&buffer[0], len, "");
		buffer[len] = 0;

		return std::string(&buffer[0]);
	}
}
