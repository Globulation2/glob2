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

	void BackgroundFileWriter::write(const std::string &filename, std::string contents)
	{
		std::unique_lock<std::mutex> lock(mutex);
		pendingName = filename;
		pendingContents = std::move(contents);
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
		std::unique_lock<std::mutex> lock(mutex);
		idle.wait(lock, [this] { return !writing; });
		lock.unlock();
		if (worker.joinable())
			worker.join();
	}

	void BackgroundFileWriter::drain()
	{
		std::unique_lock<std::mutex> lock(mutex);
		while (pending)
		{
			const std::string name = std::move(pendingName);
			const std::string contents = std::move(pendingContents);
			pending = false;
			lock.unlock();
			const bool written = fileManager->writeAtomically(name, [&contents](OutputStream &stream) {
				stream.write(contents.data(), contents.size(), "contents");
			});
			if (!written)
				std::cerr << "BackgroundFileWriter: " << name << " was not replaced; the previous file is kept" << std::endl;
			lock.lock();
		}
		writing = false;
		idle.notify_all();
	}
}
