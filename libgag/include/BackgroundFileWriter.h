// SPDX-License-Identifier: GPL-3.0-or-later

#include <PerformanceTelemetry.h>
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <CooperativeTask.h>

namespace GAGCore
{
	class FileManager;
	class ChunkedBuffer;

	//! Replaces whole files through FileManager::writeAtomically on a worker
	//! thread, so the caller hands over a finished snapshot instead of waiting
	//! for the disk. A queued snapshot that has not started writing is replaced
	//! by a newer one, along with its finish step. Use it from one thread;
	//! destruction finishes any write.
	class BackgroundFileWriter
	{
	public:
		explicit BackgroundFileWriter(FileManager *fileManager, bool cooperativeOnly = false);
		~BackgroundFileWriter();
		BackgroundFileWriter(const BackgroundFileWriter &) = delete;
		BackgroundFileWriter &operator=(const BackgroundFileWriter &) = delete;

		//! Queues contents to become the whole of filename. finish, when given,
		//! runs on the worker just before the write and may change the contents.
		//! gzip compresses the finalized bytes on the worker before replacement.
		void write(const std::string &filename, std::string contents, std::function<void(std::string &)> finish = {}, bool gzip = false);
        //! Chunked snapshot; finish runs before compression, without flattening.
        void write(const std::string& filename, ChunkedBuffer contents, std::function<void(ChunkedBuffer&)> finish = {});
		//! Returns once nothing is queued or being written; no worker thread remains.
        void waitUntilIdle();
        bool busy();
        struct Result { std::atomic<int> state{0}; }; // 0 pending, 1 success, -1 failure
        // Non-replacing owned snapshot job. Caller checks busy before capture.
        using Encode=std::function<CooperativeTask(ChunkedBuffer&)>;
        std::shared_ptr<Result> submit(const std::string& filename, Encode encode);
        void poll();

	private:
        Encode pendingEncode;
        std::unique_ptr<CooperativeTask> cooperative;
        CooperativeTask runSnapshot(std::string filename,Encode encode,std::shared_ptr<Result> result);
        std::shared_ptr<Result> pendingResult;
        void drain();
		void startWorker(std::unique_lock<std::mutex>& lock);
		void publishMetrics(); // caller holds mutex, runs on the submitting thread
		PerformanceTelemetry::Moments queueTimes, hashTimes, writeTimes;
		std::uint64_t queuedAt = 0, completedWrites = 0, failedWrites = 0, replacedWrites = 0;
		bool pendingMeasured = false;

		FileManager *fileManager;
        bool cooperativeOnly;
		std::mutex mutex;
		std::condition_variable idle;
		std::string pendingName;
		std::string pendingContents;
		std::function<void(std::string &)> pendingFinish;
		std::unique_ptr<ChunkedBuffer> pendingChunks;
		std::function<void(ChunkedBuffer&)> pendingChunkFinish;
		bool pending = false;
		bool pendingGzip = false;
		bool writing = false;
		std::thread worker;
	};
}
