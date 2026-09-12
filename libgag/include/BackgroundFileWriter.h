// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace GAGCore
{
	class FileManager;

	//! Replaces whole files through FileManager::writeAtomically on a worker
	//! thread, so the caller hands over a finished snapshot instead of waiting
	//! for the disk. A queued snapshot that has not started writing is replaced
	//! by a newer one. Use it from one thread; destruction finishes any write.
	class BackgroundFileWriter
	{
	public:
		explicit BackgroundFileWriter(FileManager *fileManager);
		~BackgroundFileWriter();
		BackgroundFileWriter(const BackgroundFileWriter &) = delete;
		BackgroundFileWriter &operator=(const BackgroundFileWriter &) = delete;

		//! Queues contents to become the whole of filename.
		void write(const std::string &filename, std::string contents);
		//! Returns once nothing is queued or being written; no worker thread remains.
		void waitUntilIdle();

	private:
		void drain();

		FileManager *fileManager;
		std::mutex mutex;
		std::condition_variable idle;
		std::string pendingName;
		std::string pendingContents;
		bool pending = false;
		bool writing = false;
		std::thread worker;
	};
}
