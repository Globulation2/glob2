// SPDX-License-Identifier: GPL-3.0-or-later

#include <BackgroundFileWriter.h>
#include <FileManager.h>
#include <Stream.h>
#include <ChunkedStreamBackend.h>
#include <iostream>
#include <system_error>

namespace GAGCore
{
	BackgroundFileWriter::BackgroundFileWriter(FileManager *fileManager) : fileManager(fileManager)
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

    void BackgroundFileWriter::startWorker(std::unique_lock<std::mutex>& lock)
    {
		if (writing)
			return; // the running worker takes the newest snapshot next
		writing = true;
		lock.unlock();
#ifdef __EMSCRIPTEN__
        drain();
#else
		// A previous worker cleared writing before exiting, so this join is short.
		if (worker.joinable())
			worker.join();
		try
		{
			worker = std::thread(&BackgroundFileWriter::drain, this);
		}
		catch (const std::exception &)
		{
			drain(); // thread creation/allocation failed: write on this one instead
		}
#endif
	}

	void BackgroundFileWriter::waitUntilIdle()
	{
		PERF_SCOPE_TIME(SaveWait);
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
                if (chunks) { if (chunkFinish) chunkFinish(*chunks); }
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
