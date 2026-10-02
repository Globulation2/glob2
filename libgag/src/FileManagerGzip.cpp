// SPDX-License-Identifier: GPL-3.0-or-later
#include <FileManager.h>
#include <GzipUtil.h>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <ChunkedStreamBackend.h>
#include <atomic>
#include <array>
#include <limits>
#include <utility>
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
		if (input.size() > std::numeric_limits<uInt>::max()) return false;
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

	bool gzipDecompress(const std::string& input, std::string& output, size_t maxOutput)
	{
		if (input.size() > std::numeric_limits<uInt>::max()) return false;
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
			const size_t produced = sizeof(buffer) - stream.avail_out;
			if (produced > maxOutput - output.size()) break;
			output.append(buffer, produced);
			if (result == Z_STREAM_END)
				break;
			if (stream.avail_in == 0)
				break; // ran out of input without reaching the end: truncated
		}
		const bool ok = (result == Z_STREAM_END && stream.avail_in == 0 &&
			stream.total_out == output.size());
		inflateEnd(&stream);
		return ok;
	}

	namespace
	{
		template<class Encode> bool writeGzipAtomicImpl(const std::string& path, Encode encode)
		{
			static std::atomic<unsigned long> sequence{0};
#ifdef WIN32
			const auto process = GetCurrentProcessId();
#else
			const auto process = getpid();
#endif
			FILE *file = nullptr;
			std::string temporary;
			for (int attempt = 0; attempt < 100; ++attempt)
			{
				temporary = path + ".tmp-" + std::to_string(process) + "-" + std::to_string(sequence++);
				file = openExclusive(temporary);
				if (file || errno != EEXIST) break;
			}
			if (!file) return false;
			bool wrote = false;
			try { wrote = encode(file) && fflush(file) == 0; }
			catch (const std::exception& error)
			{ std::cerr << "writeGzipAtomicToPath: " << path << ": " << error.what() << std::endl; }
			const bool closed = fclose(file) == 0;
			if (wrote && closed)
			{
#ifdef WIN32
				if (MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
#else
				if (std::rename(temporary.c_str(), path.c_str()) == 0) return true;
#endif
			}
			std::remove(temporary.c_str());
			return false;
		}

		// No flush between input ranges: block boundaries do not enter the wire format.
		template<class Ranges> bool deflateRanges(FILE* file, size_t size, int level, Ranges ranges)
		{
			z_stream stream{};
			if (deflateInit2(&stream, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
			auto end = [](z_stream* s) { deflateEnd(s); };
			std::unique_ptr<z_stream, decltype(end)> cleanup(&stream, end);
			gz_header header{}; header.os = 255;
			if (deflateSetHeader(&stream, &header) != Z_OK) return false;
			std::array<unsigned char, 256 * 1024> output;
			size_t consumed = 0;
            size_t written = 0;
			bool ok = true;
			int result = Z_OK;
			auto feed = [&](const unsigned char* bytes, size_t count) {
				if (!ok) return;
				stream.next_in = const_cast<Bytef*>(bytes);
				stream.avail_in = static_cast<uInt>(count);
				consumed += count;
				const int flush = consumed == size ? Z_FINISH : Z_NO_FLUSH;
				do {
					stream.next_out = output.data(); stream.avail_out = output.size();
					result = deflate(&stream, flush);
					const size_t n = output.size() - stream.avail_out;
					ok = (result == Z_OK || result == Z_STREAM_END) &&
                        n <= MAX_COMPRESSED_GAME_FILE_BYTES - written &&
                        fwrite(output.data(), 1, n, file) == n;
                    if (ok) written += n;
				} while (ok && result != Z_STREAM_END && (stream.avail_in || flush == Z_FINISH));
			};
			if (size) ranges(feed); else feed(nullptr, 0);
			return ok && result == Z_STREAM_END;
		}

		StreamBackend* inflateBackend(std::unique_ptr<StreamBackend> raw, size_t maxExpandedBytes = MAX_EXPANDED_GAME_FILE_BYTES)
		{
			if (!raw->isValid()) return raw.release();
			try
			{
				raw->seekFromEnd(0);
				size_t remaining = raw->getPosition();
				raw->seekFromStart(0);
				if (remaining > MAX_COMPRESSED_GAME_FILE_BYTES) return new FileStreamBackend(nullptr);
				z_stream stream{};
				if (inflateInit2(&stream, 15 + 16) != Z_OK) return new FileStreamBackend(nullptr);
				auto end = [](z_stream* s) { inflateEnd(s); };
				std::unique_ptr<z_stream, decltype(end)> cleanup(&stream, end);
				std::array<unsigned char, 65536> input;
				ChunkedBuffer inflated;
				for (;;)
				{
					if (!stream.avail_in && remaining)
					{
						const size_t n = std::min(remaining, input.size());
						if (!raw->readExact(input.data(), n)) break;
						remaining -= n;
						stream.next_in = input.data(); stream.avail_in = n;
					}
					// At the limit, consume the trailer without allocating another block.
                    // Any further payload byte rejects the entire stream.
                    unsigned char overflow;
                    const auto space = inflated.size() == maxExpandedBytes
                        ? std::make_pair(&overflow, size_t(1)) : inflated.prepareAppend();
					stream.next_out = space.first; stream.avail_out = space.second;
					const int result = inflate(&stream, Z_NO_FLUSH);
					const size_t produced = space.second - stream.avail_out;
                    // Cap expansion independently: valid late saves exceed the
                    // compressed-input limit, but hostile streams stay bounded.
                    if (produced > maxExpandedBytes - inflated.size()) break;
                    if (produced) inflated.commitAppend(produced);
					if (result == Z_STREAM_END)
					{
						// Expose bytes only after CRC/end validation and rejection of all
						// trailing input, before headers or mutable game state can read them.
						if (!remaining && !stream.avail_in && stream.total_out == inflated.size())
							return new ChunkedStreamBackend(std::move(inflated));
						break;
					}
					if (result != Z_OK) break;
				}
			}
			catch (const std::exception& error)
			{ std::cerr << "Cannot inflate save: " << error.what() << std::endl; }
			return new FileStreamBackend(nullptr);
		}
	}

	bool writeGzipAtomicToPath(const std::string& path, const std::string& contents, int level)
	{
		if (contents.size() > MAX_EXPANDED_GAME_FILE_BYTES) return false;
		return writeGzipAtomicImpl(path, [&](FILE* file) {
			if (level == Z_NO_COMPRESSION)
			{
				// Stored-block boundaries depend on output capacity; keep this optional legacy path.
				std::string compressed;
				return gzipCompress(contents, level, compressed) &&
                    compressed.size() <= MAX_COMPRESSED_GAME_FILE_BYTES && fwrite(compressed.data(), 1, compressed.size(), file) == compressed.size();
			}
			return deflateRanges(file, contents.size(), level, [&](auto feed) {
				feed(reinterpret_cast<const unsigned char*>(contents.data()), contents.size());
			});
		});
	}

	bool writeGzipAtomicToPath(const std::string& path, const ChunkedBuffer& contents, int level)
	{
		if (contents.size() > MAX_EXPANDED_GAME_FILE_BYTES) return false;
		if (level == Z_NO_COMPRESSION)
		{
			std::string legacy(contents.size(), '\0');
			contents.readAt(0, legacy.data(), legacy.size());
			return writeGzipAtomicToPath(path, legacy, level);
		}
		return writeGzipAtomicImpl(path, [&](FILE* file) {
			return deflateRanges(file, contents.size(), level, [&](auto feed) { contents.forEachRange(0, contents.size(), feed); });
		});
	}

	StreamBackend *openInflatingFileStreamBackend(const std::string& path, size_t maxExpandedBytes)
	{
		FILE *fp = fopen(path.c_str(), "rb");
		if (!fp)
			return new FileStreamBackend(NULL);
		if (!endsWith(path, ".gz"))
			return new FileStreamBackend(fp);

		return inflateBackend(std::make_unique<FileStreamBackend>(fp), std::min(maxExpandedBytes, MAX_EXPANDED_GAME_FILE_BYTES));
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

	bool FileManager::writeGzipAtomic(const std::string& filename, const ChunkedBuffer& contents, int level)
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
		try
		{
			if (level == Z_NO_COMPRESSION)
			{
				auto* memory = new MemoryStreamBackend();
				BinaryOutputStream stream(memory);
				writer(stream);
				return writeGzipAtomic(filename, memory->takeContents(), level);
			}
			auto *memory = new ChunkedStreamBackend();
			BinaryOutputStream stream(memory);
			writer(stream);
			return writeGzipAtomic(filename, memory->takeContents(), level);
		}
		catch (const std::exception& error)
		{
			std::cerr << "FileManager::writeGzipAtomically: " << filename << ": " << error.what() << std::endl;
			return false;
		}
	}

	StreamBackend *FileManager::openInflatingInputStreamBackend(const std::string& filename)
	{
		if (!endsWith(filename, ".gz"))
			return openInputStreamBackend(filename);

		return inflateBackend(std::unique_ptr<StreamBackend>(openInputStreamBackend(filename)));
	}
}
