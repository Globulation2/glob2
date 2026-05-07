// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière

#include <StreamFilter.h>
#include <BinaryStream.h>
#include <valarray>
#include "zlib.h"
#include <iostream>

namespace GAGCore
{
	CompressedInputStreamBackendFilter::CompressedInputStreamBackendFilter(StreamBackend *backend)
	{
		assert(backend);
		// read
		BinaryInputStream *stream = new BinaryInputStream(backend);
		Uint32 compressedLength = stream->readUint32("compressedLength");
		Uint32 uncompressedLength = stream->readUint32("uncompressedLength");
		
		std::valarray<unsigned char> source(compressedLength);
		std::valarray<unsigned char> dest(uncompressedLength);
		unsigned long destLength;
		std::cout << "Decompressing " << compressedLength <<  " bytes to " << uncompressedLength << " bytes." << std::endl;
		
		stream->read(&source[0], compressedLength, "compressedDatas");
		delete stream;
		
		// decompress
		int zret = uncompress(&dest[0], &destLength, &source[0], compressedLength);
		if (zret != Z_OK)
			std::cerr << "CompressedInputStreamBackendFilter: uncompress failed with error " << zret << std::endl;
		assert(destLength == uncompressedLength);
		
		this->write(&dest[0], uncompressedLength);
		this->seekFromStart(0);
	}
	
	CompressedOutputStreamBackendFilter::CompressedOutputStreamBackendFilter(StreamBackend *backend)
	{
		assert(backend);
		this->backend = backend;
	}
	
	CompressedOutputStreamBackendFilter::~CompressedOutputStreamBackendFilter()
	{
		// compress
		this->seekFromEnd(0);
		Uint32 uncompressedLength = static_cast<Uint32>(this->getPosition());
		Uint32 compressedLength = (uncompressedLength << 1) + 12; // dest should be at least 0.1% + 12 bytes source length, let's take some margin... in paging we trust
		this->seekFromStart(0);
		
		std::valarray<unsigned char> source(uncompressedLength);
		std::valarray<unsigned char> dest(compressedLength);
		
		this->read(&source[0], uncompressedLength);
		int zret = compress2(&dest[0], (uLongf *)&compressedLength, &source[0], uncompressedLength, Z_DEFAULT_COMPRESSION);
		if (zret != Z_OK)
			std::cerr << "CompressedOutputStreamBackendFilter: compress2 failed with error " << zret << std::endl;
		std::cout << "Compressing " << uncompressedLength <<  " bytes into " << compressedLength << " bytes." << std::endl;
		
		BinaryOutputStream *stream = new BinaryOutputStream(backend);
		stream->writeUint32(compressedLength, "compressedLength");
		stream->writeUint32(uncompressedLength, "uncompressedLength");
		stream->write(&dest[0], compressedLength, "compressedDatas");
		delete stream;
	}
}
