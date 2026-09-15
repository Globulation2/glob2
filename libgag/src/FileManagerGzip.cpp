// SPDX-License-Identifier: GPL-3.0-or-later
#include <FileManager.h>
#include <GzipUtil.h>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>
#include <zlib.h>
#ifdef WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

namespace GAGCore
{
	namespace
	{
		// Mirrors the exclusive-create pattern in FileManagerAtomic.cpp so a gzip
		// write can never observe or clobber another writer's temporary file.
		FILE *openExclusive(const std::string& path)
		{
#ifdef WIN32
			const int fd = _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
				_S_IREAD | _S_IWRITE);
			if (fd < 0) return NULL;
			FILE *file = _fdopen(fd, "wb");
			if (!file)
			{
				const int error = errno;
				_close(fd);
				std::remove(path.c_str());
				errno = error;
			}
			return file;
#else
			return fopen(path.c_str(), "wbx");
#endif
		}

		bool endsWith(const std::string& value, const std::string& suffix)
		{
			return value.size() >= suffix.size() &&
				value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
		}
	}

	bool gzipCompress(const std::string& input, int level, std::string& output)
	{
		z_stream stream;
		std::memset(&stream, 0, sizeof(stream));
		if (deflateInit2(&stream, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
			return false;
		gz_header header;
		std::memset(&header, 0, sizeof(header));
		header.os = 255; // "unknown": keeps output identical across platforms
		deflateSetHeader(&stream, &header);
		output.resize(deflateBound(&stream, input.size()));
		stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
		stream.avail_in = static_cast<uInt>(input.size());
		stream.next_out = reinterpret_cast<Bytef*>(output.empty() ? nullptr : &output[0]);
		stream.avail_out = static_cast<uInt>(output.size());
		const int result = deflate(&stream, Z_FINISH);
		const bool ok = (result == Z_STREAM_END);
		output.resize(stream.total_out);
		deflateEnd(&stream);
		return ok;
	}

	bool gzipDecompress(const std::string& input, std::string& output)
	{
		z_stream stream;
		std::memset(&stream, 0, sizeof(stream));
		if (inflateInit2(&stream, 15 + 16) != Z_OK)
			return false;
		stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
		stream.avail_in = static_cast<uInt>(input.size());
		output.clear();
		char buffer[65536];
		int result = Z_OK;
		for (;;)
		{
			stream.next_out = reinterpret_cast<Bytef*>(buffer);
			stream.avail_out = sizeof(buffer);
			result = inflate(&stream, Z_NO_FLUSH);
			if (result != Z_OK && result != Z_STREAM_END)
				break;
			output.append(buffer, sizeof(buffer) - stream.avail_out);
			if (result == Z_STREAM_END)
				break;
			if (stream.avail_in == 0)
				break; // ran out of input without reaching the end: truncated
		}
		const bool ok = (result == Z_STREAM_END);
		inflateEnd(&stream);
		return ok;
	}

	bool writeGzipAtomicToPath(const std::string& path, const std::string& contents, int level)
	{
		std::string compressed;
		if (!gzipCompress(contents, level, compressed))
		{
			std::cerr << "writeGzipAtomicToPath: compression failed for " << path << std::endl;
			return false;
		}

		static std::atomic<unsigned long> sequence{0};
#ifdef WIN32
		const auto process = GetCurrentProcessId();
#else
		const auto process = getpid();
#endif
		FILE *file = NULL;
		std::string temporary;
		for (int attempt = 0; attempt < 100; ++attempt)
		{
			temporary = path + ".tmp-" + std::to_string(process) + "-" + std::to_string(sequence++);
			file = openExclusive(temporary);
			if (file || errno != EEXIST) break;
		}
		if (!file)
		{
			std::cerr << "writeGzipAtomicToPath: cannot create temporary file for " << path << std::endl;
			return false;
		}

		const bool wrote = fwrite(compressed.data(), 1, compressed.size(), file) == compressed.size();
		const bool flushed = wrote && fflush(file) == 0;
		const bool closed = fclose(file) == 0;
		if (!(wrote && flushed && closed))
		{
			std::cerr << "writeGzipAtomicToPath: " << path << ": write failed" << std::endl;
			std::remove(temporary.c_str());
			return false;
		}
#ifdef WIN32
		const bool renamed = MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
		const bool renamed = std::rename(temporary.c_str(), path.c_str()) == 0;
#endif
		if (!renamed)
		{
			std::cerr << "writeGzipAtomicToPath: " << path << ": replacement failed" << std::endl;
			std::remove(temporary.c_str());
			return false;
		}
		return true;
	}

	StreamBackend *openInflatingFileStreamBackend(const std::string& path)
	{
		FILE *fp = fopen(path.c_str(), "rb");
		if (!fp)
			return new FileStreamBackend(NULL);
		if (!endsWith(path, ".gz"))
			return new FileStreamBackend(fp);

		FileStreamBackend raw(fp);
		raw.seekFromEnd(0);
		const size_t length = raw.getPosition();
		raw.seekFromStart(0);
		std::string compressed(length, '\0');
		if (length > 0)
			raw.read(&compressed[0], length);

		std::string inflated;
		if (!gzipDecompress(compressed, inflated))
		{
			std::cerr << "openInflatingFileStreamBackend: corrupt or truncated gzip data in " << path << std::endl;
			return new FileStreamBackend(NULL);
		}
		auto *memory = new MemoryStreamBackend(inflated.data(), inflated.size());
		memory->seekFromStart(0); // the constructor's copy-in leaves the position at the end
		return memory;
	}

	bool FileManager::writeGzipAtomic(const std::string& filename, const std::string& contents, int level)
	{
		std::vector<std::string> paths;
		if (isAbsolutePath(filename)) paths.push_back(filename);
		else for (const auto& directory : dirList) paths.push_back(directory + DIR_SEPARATOR + filename);

		for (const auto& path : paths)
			if (writeGzipAtomicToPath(path, contents, level))
				return true;
		return false;
	}

	bool FileManager::writeGzipAtomically(const std::string& filename, const std::function<void(OutputStream&)>& writer, int level)
	{
		auto *memory = new MemoryStreamBackend();
		std::string contents;
		{
			BinaryOutputStream stream(memory);
			writer(stream);
			contents = memory->takeContents();
		}
		return writeGzipAtomic(filename, contents, level);
	}

	StreamBackend *FileManager::openInflatingInputStreamBackend(const std::string& filename)
	{
		if (!endsWith(filename, ".gz"))
			return openInputStreamBackend(filename);

		StreamBackend *raw = openInputStreamBackend(filename);
		if (!raw->isValid())
			return raw;
		raw->seekFromEnd(0);
		const size_t length = raw->getPosition();
		raw->seekFromStart(0);
		std::string compressed(length, '\0');
		if (length > 0)
			raw->read(&compressed[0], length);
		delete raw;

		std::string inflated;
		if (!gzipDecompress(compressed, inflated))
		{
			std::cerr << "FileManager::openInflatingInputStreamBackend: corrupt or truncated gzip data in " << filename << std::endl;
			return new FileStreamBackend(NULL);
		}
		auto *memory = new MemoryStreamBackend(inflated.data(), inflated.size());
		memory->seekFromStart(0); // the constructor's copy-in leaves the position at the end
		return memory;
	}
}
