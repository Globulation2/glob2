// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <fstream>
#include <iostream>
#include <Types.h>
#include <string>
#include <stdio.h>
#include <assert.h>
#include "zlib.h"
#ifdef putc
#undef putc
#endif

namespace GAGCore
{
	class MemoryStreamBackend;

	//! A stream backend is a low-level serialization structure, that can be a file, a memory area, a network socket, ...
	class StreamBackend
	{
	public:
		virtual ~StreamBackend() { }
		
		virtual void write(const void *data, const size_t size) = 0;
		virtual void flush(void) = 0;
		virtual void read(void *data, size_t size) = 0;
		virtual void putc(int c) = 0;
		virtual int getChar(void) = 0;
		virtual void seekFromStart(int displacement) = 0;
		virtual void seekFromEnd(int displacement) = 0;
		virtual void seekRelative(int displacement) = 0;
		virtual size_t getPosition(void) = 0;
		virtual bool isEndOfStream(void) = 0;
		virtual bool isValid(void) = 0;
	};
	
	//! The FILE* implementation of stream backend
	class FileStreamBackend : public StreamBackend
	{
	private:
		FILE *fp;
		
	public:
		//! Constructor. If fp is NULL, isEndOfStream returns true and all other functions excepted destructor are invalid and will assert false if called
		FileStreamBackend(FILE *fp) { this->fp = fp; }
		virtual ~FileStreamBackend() { if (fp) fclose(fp); }
		
		virtual void write(const void *data, const size_t size) { assert(fp); fwrite(data, size, 1 ,fp); }
		virtual void flush(void) { assert(fp); fflush(fp); }
		virtual void read(void *data, size_t size) { assert(fp); fread(data, size, 1, fp); }
		virtual void putc(int c) { assert(fp); fputc(c, fp); }
		virtual int getChar(void) { assert(fp); return fgetc(fp); }
		virtual void seekFromStart(int displacement) { assert(fp); fseek(fp, displacement, SEEK_SET); }
		virtual void seekFromEnd(int displacement) { assert(fp); fseek(fp, displacement, SEEK_END); }
		virtual void seekRelative(int displacement) { assert(fp); fseek(fp, displacement, SEEK_CUR); }
		virtual size_t getPosition(void) { assert(fp); return ftell(fp); }
		virtual bool isEndOfStream(void) { return (fp == NULL) || (feof(fp) != 0); }
		virtual bool isValid(void) { return (fp != NULL); }
	};
	
	//! The zlib implementation of stream backend. *important* all zlib activity is run through full-file buffer in memory
	class ZLibStreamBackend : public StreamBackend
	{
	private:
		MemoryStreamBackend* buffer;
		std::string file;
		bool isRead;
	public:
		//! Constructor. If file is "", isEndOfStream returns true and all other functions excepted destructor are invalid and will assert false if called
		ZLibStreamBackend(const std::string& file, bool read);
		virtual ~ZLibStreamBackend();
		
		virtual void write(const void *data, const size_t size);
		virtual void flush(void);
		virtual void read(void *data, size_t size);
		virtual void putc(int c);
		virtual int getChar(void);
		virtual void seekFromStart(int displacement);
		virtual void seekFromEnd(int displacement);
		virtual void seekRelative(int displacement);
		virtual size_t getPosition(void);
		virtual bool isEndOfStream(void);
		virtual bool isValid(void);
	};
	
	//! A stream backend that lies in memory
	class MemoryStreamBackend : public StreamBackend
	{
	private:
		std::string datas;
		size_t index;
		
	public:
		//! Constructor. If NULL is passed to data, internal buffer is empty, otherwise size bytes are copied from data.
		MemoryStreamBackend(const void *data = NULL, const size_t size = 0);
		virtual ~MemoryStreamBackend() { }
		
		virtual void write(const void *data, const size_t size);
		virtual void flush(void) { }
		virtual void read(void *data, size_t size);
		virtual void putc(int c);
		virtual int getChar(void);
		virtual void seekFromStart(int displacement);
		virtual void seekFromEnd(int displacement);
		virtual void seekRelative(int displacement);
		virtual size_t getPosition(void);
		virtual bool isEndOfStream(void);
		virtual bool isValid(void) { return true; }
		virtual const char* getBuffer() { return datas.c_str(); }
	};

	//! A stream that doesn't save data, it just produces a hash. Don't try to read from it!
	//! It uses the FNV-1a algorithm for its speed
	class HashStreamBackend : public StreamBackend
	{
	private:
		Uint32 hash;

	public:
		HashStreamBackend() { hash = 0x811c9dc5; }
		virtual ~HashStreamBackend() {}

		virtual void write(const void *data, const size_t size);
		virtual void flush(void) {}
		virtual void read(void *data, size_t size) { assert(false); }
		virtual void putc(int c);
		virtual int getChar(void) { assert(false); }
		virtual void seekFromStart(int displacement) {}
		virtual void seekFromEnd(int displacement) {}
		virtual void seekRelative(int displacement) {}
		virtual size_t getPosition(void) { return 0; }
		virtual bool isEndOfStream(void) { return false; }
		virtual bool isValid(void) { return true; }

		virtual Uint32 getHash(void) { return hash; }
	};
}
