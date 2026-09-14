// SPDX-License-Identifier: GPL-3.0-or-later

#include <PerformanceTelemetry.h>
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace GAGCore
{
	class FileManager;

	//! Replaces whole files through FileManager::writeAtomically on a worker
	//! thread, so the caller hands over a finished snapshot instead of waiting
	//! for the disk. A queued snapshot that has not started writing is replaced
	//! by a newer one, along with its finish step. Use it from one thread;
	//! destruction finishes any write.
	class BackgroundFileWriter
	{
	public:
		explicit BackgroundFileWriter(FileManager *fileManager);
		~BackgroundFileWriter();
		BackgroundFileWriter(const BackgroundFileWriter &) = delete;
		BackgroundFileWriter &operator=(const BackgroundFileWriter &) = delete;

		//! Queues contents to become the whole of filename. finish, when given,
		//! runs on the worker just before the write and may change the contents.
		void write(const std::string &filename, std::string contents, std::function<void(std::string &)> finish = {});
		//! Returns once nothing is queued or being written; no worker thread remains.
		void waitUntilIdle();

	private:
		void drain();
		void publishMetrics(); // caller holds mutex, runs on the submitting thread
		PerformanceTelemetry::Moments queueTimes, hashTimes, writeTimes;
		std::uint64_t queuedAt = 0, completedWrites = 0, failedWrites = 0, replacedWrites = 0;
		bool pendingMeasured = false;

		FileManager *fileManager;
		std::mutex mutex;
		std::condition_variable idle;
		std::string pendingName;
		std::string pendingContents;
		std::function<void(std::string &)> pendingFinish;
		bool pending = false;
		bool writing = false;
		std::thread worker;
	};
}
