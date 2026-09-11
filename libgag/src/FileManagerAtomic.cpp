// SPDX-License-Identifier: GPL-3.0-or-later
#include <FileManager.h>
#include <BinaryStream.h>
#include <BufferedFileStreamBackend.h>
#include <atomic>
#include <cerrno>
#include <memory>
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

		class CheckedFileBackend : public BufferedFileStreamBackend
		{
		public:
			explicit CheckedFileBackend(FILE *file) : BufferedFileStreamBackend(file) {}
			void writeBufferedData(const void *data, size_t size) override
			{
				if (fwrite(data, 1, size, fp) != size)
					throw std::ios_base::failure("File write failed");
			}
			void flush() override
			{
				drain();
				if (fflush(fp) != 0 || ferror(fp))
					throw std::ios_base::failure("File flush failed");
			}
			void seekFromStart(int offset) override { seek(offset, SEEK_SET); }
			void seekFromEnd(int offset) override { seek(offset, SEEK_END); }
			void seekRelative(int offset) override { seek(offset, SEEK_CUR); }
			size_t getPosition() override
			{
				const long position = ftell(fp);
				if (position < 0) throw std::ios_base::failure("File position failed");
				return static_cast<size_t>(position) + bufferedSize();
			}
			void close()
			{
				flush();
				FILE *file = fp;
				fp = NULL;
				if (fclose(file) != 0)
					throw std::ios_base::failure("File close failed");
			}
		private:
			void seek(int offset, int origin)
			{
				drain();
				if (fseek(fp, offset, origin) != 0)
					throw std::ios_base::failure("File seek failed");
			}
		};
	}

	bool FileManager::writeAtomically(const std::string& filename, const std::function<void(OutputStream&)>& writer)
	{
		std::vector<std::string> paths;
		if (isAbsolutePath(filename)) paths.push_back(filename);
		else for (const auto& directory : dirList)
			paths.push_back(directory + DIR_SEPARATOR + filename);

		static std::atomic<unsigned long> sequence{0};
#ifdef WIN32
		const auto process = GetCurrentProcessId();
#else
		const auto process = getpid();
#endif
		for (const auto& path : paths)
		{
			FILE *file = NULL;
			std::string temporary;
			for (int attempt = 0; attempt < 100; ++attempt)
			{
				temporary = path + ".tmp-" + std::to_string(process) + "-" + std::to_string(sequence++);
				file = openExclusive(temporary);
				if (file || errno != EEXIST) break;
			}
			if (!file) continue;

			try
			{
				std::unique_ptr<FILE, decltype(&std::fclose)> owner(file, &std::fclose);
				auto *backend = new CheckedFileBackend(file);
				owner.release();
				BinaryOutputStream stream(backend);
				writer(stream);
				backend->close();
#ifdef WIN32
				if (!MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
#else
				if (std::rename(temporary.c_str(), path.c_str()) != 0)
#endif
					throw std::ios_base::failure("File replacement failed");
				return true;
			}
			catch (const std::exception& error)
			{
				std::remove(temporary.c_str());
				std::cerr << "FileManager::writeAtomically: " << path << ": " << error.what() << std::endl;
				return false;
			}
		}
		std::cerr << "FileManager::writeAtomically: cannot create temporary file for " << filename << std::endl;
		return false;
	}

}
