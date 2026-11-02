// SPDX-License-Identifier: GPL-3.0-or-later

#include <BackgroundFileWriter.h>
#include <FileManager.h>
#include <Stream.h>
#include <ChunkedStreamBackend.h>
#include <iostream>
#include <system_error>
#include <ThreadSupport.h>

namespace GAGCore
{
    BackgroundFileWriter::BackgroundFileWriter(FileManager *fileManager, bool cooperativeOnly) : fileManager(fileManager), cooperativeOnly(cooperativeOnly || !ThreadSupport::available)
    {
    }

    BackgroundFileWriter::~BackgroundFileWriter()
    {
        waitUntilIdle();
    }

    void BackgroundFileWriter::write(const std::string &filename, std::string contents, std::function<void(std::string &)> finish, bool gzip)
    {
        std::string name = filename; // allocate before replacing a queued job
        std::unique_lock<std::mutex> lock(mutex);
        publishMetrics();
        if (pending)
            ++replacedWrites;
        if (pendingResult) pendingResult->state = State::Failed;
        pendingResult.reset();
        pendingEncode = {};
        pendingMeasured = PerformanceTelemetry::collector().enabled;
        queuedAt = pendingMeasured ? PerformanceTelemetry::now() : 0;
        pendingChunks.reset();
        pendingChunkFinish = {};
        pendingName = std::move(name);
        pendingGzip = gzip;
        pendingContents = std::move(contents);
        pendingFinish = std::move(finish);
        pending = true;
        startWorker(lock);
    }

    void BackgroundFileWriter::write(const std::string& filename, ChunkedBuffer contents, std::function<void(ChunkedBuffer&)> finish)
    {
        auto snapshot = std::make_unique<ChunkedBuffer>(std::move(contents));
        std::string name = filename;
        std::unique_lock<std::mutex> lock(mutex);
        publishMetrics();
        if (pending) ++replacedWrites;
        if (pendingResult) pendingResult->state = State::Failed;
        pendingResult.reset();
        pendingEncode = {};
        pendingMeasured = PerformanceTelemetry::collector().enabled;
        queuedAt = pendingMeasured ? PerformanceTelemetry::now() : 0;
        pendingName = std::move(name);
        std::string().swap(pendingContents);
        pendingFinish = {};
        pendingChunks = std::move(snapshot);
        pendingChunkFinish = std::move(finish);
        pendingGzip = true;
        pending = true;
        startWorker(lock);
    }

    CooperativeTask BackgroundFileWriter::runSnapshot(std::string filename, Encode encode,
                                                     std::shared_ptr<Result> result,
                                                     bool measured, std::uint64_t queued)
    {
        const auto started = PerformanceTelemetry::now();
        auto encodedAt = started;
        try
        {
            ChunkedBuffer bytes;
            if (!(co_await encode(bytes))) throw std::runtime_error("Save encoding failed");
            encode = {};
            encodedAt = PerformanceTelemetry::now();
            result->state = (co_await fileManager->writeGzipTask(filename, bytes))
                ? State::Succeeded : State::Failed;
        }
        catch (...)
        {
            result->state = State::Failed;
        }
        if (result->state == State::Failed)
            std::cerr << "BackgroundFileWriter: " << filename
                      << " was not replaced; the previous file is kept\n";
        if (measured)
        {
            std::lock_guard<std::mutex> lock(mutex);
            queueTimes.add(started - queued);
            hashTimes.add(encodedAt - started);
            writeTimes.add(PerformanceTelemetry::now() - encodedAt);
            if (result->state == State::Succeeded) ++completedWrites;
            else ++failedWrites;
        }
        co_return result->state == State::Succeeded;
    }

    void BackgroundFileWriter::poll()
    {
        if (!cooperativeOnly || !cooperative) return;
        const auto start = PerformanceTelemetry::now();
        bool complete = false;
        // Stop between bounded encoding/deflate steps after a 2ms budget.
        do { complete = cooperative->advance(); }
        while (!complete && PerformanceTelemetry::now() - start < 2000000);
        if (complete)
        {
            cooperative.reset();
            std::unique_lock<std::mutex> lock(mutex);
            writing = false;
            // A legacy write may have queued a replacement during this job.
            // Transition directly to it so busy() never reports a false idle.
            if (pending) startWorker(lock);
            else idle.notify_all();
        }
    }

    bool BackgroundFileWriter::busy()
    {
        poll();
        std::lock_guard<std::mutex> lock(mutex);
        return writing || pending;
    }

    std::shared_ptr<BackgroundFileWriter::Result> BackgroundFileWriter::submit(const std::string& filename, Encode encode)
    {
        auto result = std::make_shared<Result>();
        std::unique_lock<std::mutex> lock(mutex);
        // Match write(): publish the previous job on the submitting thread.
        publishMetrics();
        if (writing || pending)
        {
            result->state = State::Failed;
            return result;
        }
        pendingName = filename;
        pendingEncode = std::move(encode);
        pendingResult = result;
        pending = true;
        pendingGzip = true;
        pendingMeasured = PerformanceTelemetry::collector().enabled;
        queuedAt = PerformanceTelemetry::now();
        startWorker(lock);
        return result;
    }

    void BackgroundFileWriter::startWorker(std::unique_lock<std::mutex>& lock)
    {
        if (writing)
            return; // the running worker takes the newest snapshot next
        writing = true;
        lock.unlock();
        if (cooperativeOnly)
        {
            // Legacy byte-only callers retain their existing fallback. Snapshot
            // jobs must never unexpectedly perform expensive work on the caller.
            if (pendingResult)
            {
                lock.lock();
                auto result = pendingResult;
                try
                {
                    cooperative = std::make_unique<CooperativeTask>(runSnapshot(
                        std::move(pendingName), std::move(pendingEncode), std::move(pendingResult),
                        pendingMeasured, queuedAt));
                }
                catch (...)
                {
                    result->state = State::Failed;
                    pendingResult.reset();
                    pendingEncode = {};
                    writing = false;
                    idle.notify_all();
                }
                pending = false;
            }
            else drain();
        }
        else
        {
            // A previous worker cleared writing before exiting, so this join is short.
            if (worker.joinable()) worker.join();
            try { worker = ThreadSupport::launch([this] { drain(); }); }
            catch (const std::exception &)
            {
                lock.lock();
                if (pendingResult) pendingResult->state = State::Failed;
                pendingResult.reset();
                pendingEncode = {};
                pendingChunks.reset();
                pendingContents.clear();
                pendingFinish = {};
                pendingChunkFinish = {};
                pending = false;
                writing = false;
                idle.notify_all();
            }
        }
    }

    void BackgroundFileWriter::waitUntilIdle()
    {
        PERF_SCOPE_TIME(SaveWait);
        if (cooperativeOnly) while (cooperative) poll();
        std::unique_lock<std::mutex> lock(mutex);
        idle.wait(lock, [this] { return !writing; });
        publishMetrics();
        lock.unlock();
        if (worker.joinable())
            worker.join();
    }

    void BackgroundFileWriter::publishMetrics()
    {
        auto &c = PerformanceTelemetry::collector();
        c.merge(PerformanceTelemetry::Id::SaveQueue, queueTimes);
        c.merge(PerformanceTelemetry::Id::SaveHash, hashTimes);
        c.merge(PerformanceTelemetry::Id::SaveWrite, writeTimes);
        c.saved += completedWrites;
        c.failed += failedWrites;
        c.superseded += replacedWrites;
        queueTimes = {};
        hashTimes = {};
        writeTimes = {};
        completedWrites = failedWrites = replacedWrites = 0;
    }

    void BackgroundFileWriter::drain()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (pending)
        {
            const std::string name = std::move(pendingName);
            const bool gzip = pendingGzip;
            auto encode = std::move(pendingEncode);
            auto result = std::move(pendingResult);
            std::string contents = std::move(pendingContents);
            const std::function<void(std::string &)> finish = std::move(pendingFinish);
            auto chunks = std::move(pendingChunks);
            const auto chunkFinish = std::move(pendingChunkFinish);
            pendingFinish = nullptr;
            const bool measured = pendingMeasured;
            const auto started = measured ? PerformanceTelemetry::now() : 0;
            if (measured)
                queueTimes.add(started - queuedAt);
            pending = false;
            lock.unlock();
            bool written = false;
            auto hashed = started;
            try
            {
                if (encode)
                {
                    chunks = std::make_unique<ChunkedBuffer>();
                    if (!encode(*chunks).run()) throw std::runtime_error("Save encoding failed");
                    encode = {};
                }
                if (chunks)
                {
                    if (chunkFinish) chunkFinish(*chunks);
                }
                else if (finish) finish(contents);
                hashed = measured ? PerformanceTelemetry::now() : 0;
                if (chunks) written = fileManager->writeGzipAtomic(name, *chunks);
                else written = gzip ? fileManager->writeGzipAtomic(name, contents) : fileManager->writeAtomically(name, [&contents](OutputStream& stream) {
                    stream.write(contents.data(), contents.size(), "contents");
                });
            }
            catch (const std::exception& error)
            { std::cerr << "BackgroundFileWriter: " << error.what() << std::endl; }
            catch (...) { std::cerr << "BackgroundFileWriter: finalization failed" << std::endl; }
            if (!written)
                std::cerr << "BackgroundFileWriter: " << name << " was not replaced; the previous file is kept" << std::endl;
            if (result) result->state = written ? State::Succeeded : State::Failed;
            // Idle must mean that snapshot memory has actually been released.
            chunks.reset();
            std::string().swap(contents);
            const auto finished = measured ? PerformanceTelemetry::now() : 0;
            lock.lock();
            if (measured)
            {
                hashTimes.add(hashed - started);
                writeTimes.add(finished - hashed);
                if (written)
                    ++completedWrites;
                else
                    ++failedWrites;
            }
        }
        writing = false;
        idle.notify_all();
    }
}
