// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <PerformanceTelemetry.h>

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

    //! Finalizes owned snapshots and atomically replaces whole files. Public
    //! methods and poll() belong to one owner thread. Native builds use a worker;
    //! threadless snapshot jobs advance in bounded poll() steps. Legacy write()
    //! calls retain replacement semantics and synchronous threadless fallback.
    //! Destruction drains every retained job.
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
        enum class State { Pending, Succeeded, Failed };
        struct Result { std::atomic<State> state{State::Pending}; };
        // Non-replacing owned snapshot job. Caller checks busy before capture.
        // Rejection returns Failed without running encode. A later legacy write
        // may supersede a queued job, reporting Failed to that job's observer.
        using Encode = std::function<CooperativeTask(ChunkedBuffer&)>;
        std::shared_ptr<Result> submit(const std::string& filename, Encode encode);
        // Advances threadless snapshot work; a native worker needs no polling.
        void poll();

    private:
        // One active job and at most one legacy replacement are retained.
        // submit() refuses overlap; write() may replace a queued legacy job.
        // In cooperative mode, poll() owns advancing the active coroutine and
        // starts the next queued job before marking the writer idle.
        Encode pendingEncode;
        std::unique_ptr<CooperativeTask> cooperative;
        CooperativeTask runSnapshot(std::string filename, Encode encode, std::shared_ptr<Result> result,
                                    bool measured, std::uint64_t queued);
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
