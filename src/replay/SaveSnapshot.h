// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Game.h"
#include <ApplicationHost.h>
#include <BackgroundFileWriter.h>
#include <DeferredStream.h>

// Capture runs on the game/editor owner thread. The returned encoder owns all
// inputs, so the simulation may resume while finalization runs on a worker.
template <class Save> GAGCore::BackgroundFileWriter::Encode captureSave(Save save)
{
	GAGCore::DeferredStream stream;
	DeferredGameSHA1 sha;
	save(&stream, &sha);
	auto snapshot = std::make_shared<GAGCore::DeferredStream::Snapshot>(stream.takeSnapshot());
	return [snapshot, sha](GAGCore::ChunkedBuffer &bytes) mutable -> GAGCore::CooperativeTask
	{
		if (!(co_await snapshot->finishTask(bytes)))
			co_return false;

		sha.start = snapshot->relocate(sha.start);
		sha.end = snapshot->relocate(sha.end);
		sha.headerOffset = snapshot->relocate(sha.headerOffset);
		sha.sha1Offset = snapshot->relocate(sha.sha1Offset);
		SHA1_CTX context;
		SHA1Init(&context);
		const auto hash = [&](const unsigned char *data, size_t n)
		{ SHA1Update(&context, data, n); };
		bytes.forEachRange(sha.start, sha.headerOffset - sha.start, hash);
		// Game::save hashes the initial header before backpatching it. Preserve
		// that contract, even though the finalized header now has real offsets.
		SHA1Update(&context, reinterpret_cast<const unsigned char *>(sha.initialHeader.data()),
				   sha.initialHeader.size());
		for (size_t off = sha.headerOffset + sha.initialHeader.size(); off < sha.end;)
		{
			const size_t n = std::min<size_t>(65536, sha.end - off);
			bytes.forEachRange(off, n, hash);
			off += n;
			co_await GAGCore::CooperativeTask::checkpoint("Hashing save");
		}
		unsigned char digest[20];
		SHA1Final(digest, &context);
		bytes.writeAt(sha.sha1Offset, digest, sizeof(digest));
		co_return true;
	};
}

// Polled only on the UI/owner thread. A busy writer delays capture, so there is
// never a second live snapshot waiting behind an autosave. Atomic file replacement
// precedes browser persistence; names/dirty flags are published only after both
// succeed. Terminal states are sticky, including repeated dialog polls.
class SaveOperation final : public GAGCore::ApplicationHost::Persistence
{
  public:
	using PersistenceFactory =
		std::function<std::unique_ptr<GAGCore::ApplicationHost::Persistence>()>;

	SaveOperation(GAGCore::BackgroundFileWriter &writer, std::string filename,
				  std::function<GAGCore::BackgroundFileWriter::Encode()> capture,
				  std::function<void()> succeeded,
				  PersistenceFactory persist = GAGCore::ApplicationHost::persistStorage)
		: writer(writer), filename(std::move(filename)), capture(std::move(capture)),
		  succeeded(std::move(succeeded)), persist(std::move(persist))
	{
	}

	GAGCore::ApplicationHost::PersistenceState state() const override
	{
		using State = GAGCore::ApplicationHost::PersistenceState;
		using WriteState = GAGCore::BackgroundFileWriter::State;
		if (failed)
			return State::Failed;
		if (done)
			return State::Succeeded;
		try
		{
			writer.poll();
			if (!result)
			{
				if (writer.busy())
					return State::Pending;
				result = writer.submit(filename, capture());
				capture = {};
			}
			if (result->state == WriteState::Pending)
				return State::Pending;
			if (result->state == WriteState::Failed)
			{
				failed = true;
				return State::Failed;
			}
			if (!persistence)
				persistence = persist();
			const auto status = persistence->state();
			if (status == State::Failed)
				failed = true;
			if (status == State::Succeeded)
			{
				succeeded();
				succeeded = {};
				done = true;
			}
			return status;
		}
		catch (...)
		{
			failed = true;
			capture = {};
			return State::Failed;
		}
	}

  private:
	GAGCore::BackgroundFileWriter &writer;
	std::string filename;
	mutable std::function<GAGCore::BackgroundFileWriter::Encode()> capture;
	mutable std::function<void()> succeeded;
	PersistenceFactory persist;
	mutable std::shared_ptr<GAGCore::BackgroundFileWriter::Result> result;
	mutable std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
	mutable bool failed = false;
	mutable bool done = false;
};
