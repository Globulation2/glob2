// SPDX-License-Identifier: GPL-3.0-or-later
#include <FileManager.h>
#include <ApplicationHost.h>
#include <Toolkit.h>
#include <BinaryStream.h>
#include <atomic>
#include <cerrno>
#include <memory>
#ifdef WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace GAGCore
{
	namespace
	{
#if !defined(WIN32) && !defined(__EMSCRIPTEN__)
		void synchronize(int descriptor)
		{
			int result;
			do { result = fsync(descriptor); } while (result != 0 && errno == EINTR);
			if (result != 0) throw std::ios_base::failure("Storage synchronization failed");
		}

		class ParentDirectory
		{
		public:
			explicit ParentDirectory(const std::string& path)
			{
				const auto slash = path.find_last_of('/');
				const std::string parent = slash == std::string::npos ? "." :
					(slash == 0 ? "/" : path.substr(0, slash));
				do { descriptor = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); }
				while (descriptor < 0 && errno == EINTR);
				if (descriptor < 0) throw std::ios_base::failure("Cannot open save directory for synchronization");
			}
			~ParentDirectory() { ::close(descriptor); }
			void sync() { synchronize(descriptor); }
			ParentDirectory(const ParentDirectory&) = delete;
			ParentDirectory& operator=(const ParentDirectory&) = delete;
		private:
			int descriptor;
		};
#endif

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

		class CheckedFileBackend : public FileStreamBackend
		{
		public:
			explicit CheckedFileBackend(FILE *file) : FileStreamBackend(file) {}
			void write(const void *data, size_t size) override
			{
				if (fwrite(data, 1, size, fp) != size)
					throw std::ios_base::failure("File write failed");
			}
			void flush() override
			{
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
				return static_cast<size_t>(position);
			}
			void close()
			{
				FILE *file = fp;
				fp = NULL;
				if (fclose(file) != 0)
					throw std::ios_base::failure("File close failed");
			}
			void sync()
			{
#ifdef WIN32
				if (_commit(_fileno(fp)) != 0)
					throw std::ios_base::failure("Storage synchronization failed");
#elif !defined(__EMSCRIPTEN__)
				synchronize(fileno(fp));
#ifdef __APPLE__
				int result;
				do { result = fcntl(fileno(fp), F_FULLFSYNC); } while (result != 0 && errno == EINTR);
				if (result != 0) throw std::ios_base::failure("Storage full synchronization failed");
#endif
#endif
				// Emscripten persistence is completed separately by IDBFS syncfs.
			}
		private:
			void seek(int offset, int origin)
			{
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
#if !defined(WIN32) && !defined(__EMSCRIPTEN__)
				ParentDirectory directory(path);
#endif
				auto *backend = new CheckedFileBackend(file);
				owner.release();
				BinaryOutputStream stream(backend);
				writer(stream);
				stream.flush();
				backend->sync();
				backend->close();
#ifdef WIN32
				if (!MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
				if (std::rename(temporary.c_str(), path.c_str()) != 0)
#endif
					throw std::ios_base::failure("File replacement failed");
#if !defined(WIN32) && !defined(__EMSCRIPTEN__)
				// Failure here means complete new bytes are visible, but durability
				// is uncertain. Never remove the destination or retry another root.
				directory.sync();
#endif
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

bool GAGCore::ApplicationHost::exportLocalFile(const std::string& exportPath)
{
    try {
        BinaryInputStream stream(Toolkit::getFileManager()->openInputStreamBackend(exportPath));
        if (!stream.isValid()) throw std::runtime_error("Save unavailable");
        stream.seekFromEnd(0);
        const size_t size = stream.getPosition();
        if (!size || size > 64u * 1024u * 1024u) throw std::runtime_error("Save exceeds export limit");
        stream.seekFromStart(0);
        std::vector<unsigned char> bytes(size);
        stream.read(bytes.data(), size, "export");
        const auto slash = exportPath.find_last_of("/\\");
        const auto name = exportPath.substr(slash == std::string::npos ? 0 : slash + 1);
        return exportFile(name, bytes);
    } catch (const std::exception&) { return false; }
}
