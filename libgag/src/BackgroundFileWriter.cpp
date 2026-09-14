// SPDX-License-Identifier: GPL-3.0-or-later

#include <BackgroundFileWriter.h>
#include <FileManager.h>
#include <Stream.h>
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

	void BackgroundFileWriter::write(const std::string &filename, std::string contents, std::function<void(std::string &)> finish)
	{
		std::unique_lock<std::mutex> lock(mutex);
		publishMetrics();
		if (pending)
			++replacedWrites;
		pendingMeasured = PerformanceTelemetry::collector().enabled;
		queuedAt = pendingMeasured ? PerformanceTelemetry::now() : 0;
		pendingName = filename;
		pendingContents = std::move(contents);
		pendingFinish = std::move(finish);
		pending = true;
		if (writing)
			return; // the running worker takes the newest snapshot next
		writing = true;
		lock.unlock();
		// A previous worker cleared writing before exiting, so this join is short.
		if (worker.joinable())
			worker.join();
		try
		{
			worker = std::thread(&BackgroundFileWriter::drain, this);
		}
		catch (const std::system_error &)
		{
			drain(); // no thread available: write on this one instead
		}
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
			std::string contents = std::move(pendingContents);
			const std::function<void(std::string &)> finish = std::move(pendingFinish);
			pendingFinish = nullptr;
			const bool measured = pendingMeasured;
			const auto started = measured ? PerformanceTelemetry::now() : 0;
			if (measured)
				queueTimes.add(started - queuedAt);
			pending = false;
			lock.unlock();
			if (finish)
				finish(contents);
			const auto hashed = measured ? PerformanceTelemetry::now() : 0;
			const bool written = fileManager->writeAtomically(name, [&contents](OutputStream &stream) {
				stream.write(contents.data(), contents.size(), "contents");
			});
			if (!written)
				std::cerr << "BackgroundFileWriter: " << name << " was not replaced; the previous file is kept" << std::endl;
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
