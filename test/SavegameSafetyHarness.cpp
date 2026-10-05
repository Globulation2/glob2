// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "SaveSnapshot.h"
#include "LoadSaveDialog.h"
#include <vector>
#include <string>
#include <utility>
#include <chrono>
#include <iterator>
#include <exception>
#include <PerformanceTelemetry.h>
#include "GlobalContainer.h"
#include "Version.h"
#include "Engine.h"
#include "EngineTiming.h"
#include "FileFormatVersions.h"
#include "Utilities.h"
#include "Order.h"
#include "Player.h"
#include <BackgroundFileWriter.h>
#include <ThreadSupport.h>
#include <ChunkedStreamBackend.h>
#include <future>
#include <atomic>
#include "Version.h"
#include "FileImport.h"
#include "ReplayWriter.h"
#include "Campaign.h"
#include "KeyboardManager.h"
#include "GameGUIKeyActions.h"
#include "MapEditKeyActions.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <FileManager.h>
#include <GzipUtil.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>
#include <stdexcept>
#ifdef WIN32
#include <process.h>
#else
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

class SavegameSafetyHarness
{
public:
    static void openSaveDialog(GameGUI& gui, std::unique_ptr<LoadSaveDialog> dialog)
    {
        gui.openDialog(GameGUI::IGM_SAVE, std::move(dialog));
    }
    static void closeDialog(GameGUI& gui) { gui.closeDialog(); }
    static void pollSaveDialog(GameGUI& gui)
    {
        auto* dialog = static_cast<LoadSaveDialog*>(gui.gameMenuScreen.get());
        if (dialog->pollPersistence()) gui.closeDialog();
        else if (dialog->finished()) gui.processGameMenu(nullptr);
    }
    static void stop(GameGUI& gui)
    {
        gui.localTeam = gui.game.teams[0];
        gui.isRunning = false;
    }

    static GAGCore::BackgroundFileWriter& writer(GameGUI& gui, GAGCore::FileManager& files)
    {
        if (!gui.autosaveWriter) gui.autosaveWriter = std::make_unique<GAGCore::BackgroundFileWriter>(&files);
        return *gui.autosaveWriter;
    }
};

namespace
{

using namespace GAGCore;
namespace fs = std::filesystem;

static std::string contents(const fs::path& path)
{
	std::ifstream file(path, std::ios::binary);
	REQUIRE(file);
	return std::string(std::istreambuf_iterator<char>(file), {});
}

static void checkChunkedStreams()
{
    MemoryStreamBackend legacy;
    ChunkedStreamBackend chunks;
    std::string data(3 * ChunkedBuffer::blockSize + 17, 'x');
    uint32_t rng = 19;
    for (char& c : data) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; c = char(rng); }
    for (StreamBackend* stream : {static_cast<StreamBackend*>(&legacy), static_cast<StreamBackend*>(&chunks)})
    {
        stream->write(data.data(), data.size());
        stream->seekFromStart(ChunkedBuffer::blockSize - 4);
        stream->write("header backpatch", 16);
        stream->seekFromEnd(-9);
        REQUIRE(stream->readExact(nullptr, 0));
        stream->write("", 0);
        stream->write("gap", 3);
        stream->seekFromStart(-1);
        stream->seekRelative(-27);
    }
    REQUIRE(chunks.getPosition() == legacy.getPosition());
    std::string expected(legacy.getPosition(), '\0'), actual(expected.size(), '\0');
    legacy.read(expected.data(), expected.size()); // overread: zeros, no movement
    chunks.read(actual.data(), actual.size());
    REQUIRE(actual == expected);
    REQUIRE(chunks.getPosition() == legacy.getPosition());
    chunks.seekFromStart(0); legacy.seekFromStart(0);
    auto snapshot = chunks.takeContents();
    REQUIRE(chunks.contents().size() == 0);
    REQUIRE(snapshot.allocatedCapacity() >= snapshot.size());
    REQUIRE(snapshot.allocatedCapacity() < snapshot.size() + ChunkedBuffer::blockSize);
    const unsigned char* address = nullptr;
    snapshot.forEachRange(0, 1, [&](const unsigned char* p, size_t) { address = p; });
    ChunkedStreamBackend moved(std::move(snapshot));
    REQUIRE(snapshot.size() == 0);
    moved.contents().forEachRange(0, 1, [&](const unsigned char* p, size_t) { REQUIRE(p == address); });
    const size_t size = moved.contents().size();
    expected.resize(size); actual.resize(size);
    legacy.read(expected.data(), size); moved.read(actual.data(), size);
    REQUIRE(actual == expected);
    REQUIRE(moved.isEndOfStream());
    REQUIRE(!moved.readExact(actual.data(), 1));
    REQUIRE(moved.getPosition() == size);
    unsigned char byte = 99; moved.read(&byte, 1);
    REQUIRE(byte == 0);
    REQUIRE(moved.getPosition() == size);
    chunks.write("reuse", 5); REQUIRE(chunks.contents().size() == 5);
    std::cout << "PASS chunked boundary reads/writes, seeks, gaps, overreads, capacity bound and ownership transfer" << std::endl;
}

static void checkGzipWrites(FileManager& files, const fs::path& directory)
{
	const std::string original(200000, 'x');
	std::string compressed, again, decoded;
	REQUIRE(gzipCompress(original, 6, compressed));
	REQUIRE((gzipCompress(original, 6, again) && again == compressed));
	REQUIRE((gzipDecompress(compressed, decoded) && decoded == original));
	REQUIRE(!gzipDecompress(compressed, decoded, original.size() - 1));
	REQUIRE(!gzipDecompress(compressed.substr(0, compressed.size()-1), decoded));
	REQUIRE(!gzipDecompress(compressed + "trailing", decoded));
	again = compressed; again[again.size()-8] ^= 1;
	REQUIRE(!gzipDecompress(again, decoded));
	const auto path = (directory / "atomic.game.gz").string();
	REQUIRE(files.writeGzipAtomic(path, original));
	REQUIRE(contents(path) == compressed);
	REQUIRE(!files.writeGzipAtomically(path, [](OutputStream& stream) {
		stream.write("partial", 7, "data");
		throw std::runtime_error("injected serialization failure");
	}));
	REQUIRE(contents(path) == compressed);
	REQUIRE(!files.writeGzipAtomic((directory / "missing" / "save.game.gz").string(), original));
	const auto blocked = directory / "blocked.game.gz";
	fs::create_directory(blocked);
	REQUIRE(!files.writeGzipAtomic(blocked.string(), original));
	for (const auto& entry : fs::directory_iterator(directory))
		REQUIRE(entry.path().filename().string().find(".tmp-") == std::string::npos);
	// Exercise several full output buffers with incompressible input too.
	for (size_t size : {size_t(0),size_t(262143),size_t(262144),size_t(262145),size_t(1048576),size_t(1048577),size_t(3*1048576+17)})
	{
		std::string input(size,'\0'); uint32_t rng=19;
		for (char &c : input) { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; c=char(rng); }
		for (int level : {0,1,6,9})
		{
			REQUIRE(gzipCompress(input,level,compressed));
			REQUIRE(files.writeGzipAtomic(path,input,level));
			REQUIRE(contents(path)==compressed);
            ChunkedBuffer chunks;
            // Deliberately split writes independently of storage/output boundaries.
            const size_t split = input.size() / 3;
            chunks.writeAt(0, input.data(), split);
            chunks.writeAt(split, input.data() + split, input.size() - split);
            REQUIRE(writeGzipAtomicToPath(path, chunks, level));
            REQUIRE(contents(path) == compressed);
			std::unique_ptr<StreamBackend> stream(openInflatingFileStreamBackend(path));
			REQUIRE(stream->getPosition()==0);
			std::string loaded(size,'\0'); stream->read(loaded.data(),loaded.size());
			REQUIRE(loaded==input);
            auto* chunked = dynamic_cast<ChunkedStreamBackend*>(stream.get());
            REQUIRE(chunked != nullptr);
            REQUIRE(chunked->contents().allocatedCapacity() <= input.size() + ChunkedBuffer::blockSize);
			stream->seekFromStart(0); REQUIRE(stream->getPosition()==0);
		}
	}
    // Invalid gzip must fail before any decoded bytes reach the game loader.
    const std::string valid = contents(path);
    for (const std::string& corrupt : {valid.substr(0, valid.size()-1), valid + "trailing", valid + valid})
    {
        std::ofstream(path, std::ios::binary).write(corrupt.data(), corrupt.size());
        std::unique_ptr<StreamBackend> rejected(files.openInflatingInputStreamBackend(path));
        REQUIRE(!rejected->isValid());
    }
    // Expansion budgets include the exact payload but must still validate its
    // trailer. A full final block needs no spare block just to consume the CRC.
    std::string bounded(ChunkedBuffer::blockSize, 'z'), boundedGzip;
    REQUIRE(gzipCompress(bounded, 6, boundedGzip));
    std::ofstream(path, std::ios::binary).write(boundedGzip.data(), boundedGzip.size());
    std::unique_ptr<StreamBackend> exact(openInflatingFileStreamBackend(path, bounded.size()));
    REQUIRE(exact->isValid());
    auto* exactChunks = dynamic_cast<ChunkedStreamBackend*>(exact.get());
    REQUIRE(exactChunks != nullptr);
    REQUIRE(exactChunks->contents().size() == bounded.size());
    REQUIRE(exactChunks->contents().allocatedCapacity() == bounded.size());
    std::unique_ptr<StreamBackend> tooLarge(openInflatingFileStreamBackend(path, bounded.size() - 1));
    REQUIRE(!tooLarge->isValid());
    std::unique_ptr<StreamBackend> emptyBudget(openInflatingFileStreamBackend(path, 0));
    REQUIRE(!emptyBudget->isValid());
    std::string corrupt = valid; corrupt[corrupt.size()-8] ^= 1;
    std::ofstream(path, std::ios::binary).write(corrupt.data(), corrupt.size());
    std::unique_ptr<StreamBackend> rejected(openInflatingFileStreamBackend(path));
    REQUIRE(!rejected->isValid());
    std::ofstream(path, std::ios::binary).write(valid.data(), valid.size());
    REQUIRE(!files.writeGzipAtomically(path, [](OutputStream&) { throw std::bad_alloc(); }));
    REQUIRE(contents(path) == valid);
	std::string owned(1048576,'m'); const char *allocation=owned.data();
	MemoryStreamBackend moved(std::move(owned));
	REQUIRE(moved.getBuffer()==allocation);
	REQUIRE(moved.getPosition()==0);
	REQUIRE(moved.getChar()=='m');
	std::cout << "PASS gzip deterministic encoding, round trip, size limit, corruption rejection and failed atomic replacement" << std::endl;
}

static void checkAtomicWrites(FileManager& files, const fs::path& directory)
{
	const std::string path = (directory / "atomic.game").string();
#ifdef WIN32
	const auto process = _getpid();
#else
	const auto process = getpid();
#endif
	const std::string collision = path + ".tmp-" + std::to_string(process) + "-0";
	{ std::ofstream existing(collision); existing << "keep"; }
	const auto write = [](OutputStream& stream) { stream.write("complete", 8, "data"); };
	REQUIRE(files.writeAtomically(path, write));
	REQUIRE(contents(collision) == "keep");
	fs::remove(collision);
	REQUIRE(contents(path) == "complete");
	REQUIRE(!files.writeAtomically(path, [](OutputStream& stream) {
		stream.write("partial", 7, "data");
		throw std::runtime_error("injected serialization failure");
	}));
	REQUIRE(contents(path) == "complete");
	REQUIRE(files.writeAtomically(path, [](OutputStream& stream) {
		stream.write("placeholder", 11, "data");
		stream.seekFromStart(0);
		stream.write("replacement", 11, "data");
	}));
	REQUIRE(contents(path) == "replacement");
	const auto blocked = directory / "directory.game";
	fs::create_directory(blocked);
	REQUIRE(!files.writeAtomically(blocked.string(), write));
	REQUIRE(fs::is_directory(blocked));
	REQUIRE(!files.writeAtomically((directory / "missing" / "save.game").string(), write));
#ifndef WIN32
	for (rlim_t limit : {rlim_t(0), rlim_t(1024)})
	{
		const pid_t child = fork();
		REQUIRE(child >= 0);
		if (child == 0)
		{
			std::signal(SIGXFSZ, SIG_IGN);
			struct rlimit budget = {limit, limit};
			if (setrlimit(RLIMIT_FSIZE, &budget) != 0) _exit(2);
			const bool saved = files.writeAtomically(path, [limit](OutputStream& stream) {
				const std::string data(limit ? 16384 : 8, 'x');
				stream.write(data.data(), data.size(), "data");
			});
			_exit(saved ? 3 : 0);
		}
		int status = 0;
		REQUIRE((waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0));
		REQUIRE(contents(path) == "replacement");
	}
#endif
	for (const auto& entry : fs::directory_iterator(directory))
		REQUIRE(entry.path().filename().string().find(".tmp-") == std::string::npos);
	std::cout << "PASS atomic creation/replacement, close drains pending writes, temporary-name collision, seek, serialization/open/rename failure, temporary cleanup" << std::endl;
#ifndef WIN32
	std::cout << "PASS injected short write and buffered flush failure preserve previous bytes" << std::endl;
#endif
}

static void checkBackgroundWriter(FileManager& files, const fs::path& directory)
{
	auto &perf = PerformanceTelemetry::collector();
	perf.reset();
	const std::string path = (directory / "background.game").string();
	{
		BackgroundFileWriter writer(&files);
		writer.waitUntilIdle();
		// Each snapshot's finish step travels with it, so a superseded one never runs on a newer snapshot.
		for (int i = 0; i < 50; ++i)
			writer.write(path, "snapshot " + std::to_string(i), [i](std::string& bytes) { bytes += " finished " + std::to_string(i); });
		writer.waitUntilIdle();
		REQUIRE(contents(path) == "snapshot 49 finished 49");
		writer.write(path, "finished by the destructor");
	}
	REQUIRE(contents(path) == "finished by the destructor");
	{
		BackgroundFileWriter writer(&files);
		writer.write((directory / "missing" / "save.game").string(), "unwritable");
		writer.waitUntilIdle();
		writer.write(path, "written after a failure");
	}
	REQUIRE(contents(path) == "written after a failure");
	REQUIRE((perf.saved + perf.superseded == 52 && perf.failed == 1));
	REQUIRE(perf.window[unsigned(PerformanceTelemetry::Id::SaveWrite)].time.count ==
		   perf.saved + perf.failed);
	REQUIRE(perf.window[unsigned(PerformanceTelemetry::Id::SaveQueue)].time.count ==
		   perf.saved + perf.failed);
    {
        BackgroundFileWriter writer(&files);
        writer.write(path, "never published", [](std::string&) { throw std::bad_alloc(); });
        writer.waitUntilIdle();
        REQUIRE(contents(path) == "written after a failure");
        ChunkedBuffer bytes; bytes.writeAt(0, "chunked after failure", 21);
        writer.write(path, std::move(bytes));
        writer.waitUntilIdle();
        std::string decoded;
        REQUIRE(gzipDecompress(contents(path), decoded));
        REQUIRE(decoded == "chunked after failure");
        const auto previous = contents(path);
        ChunkedBuffer failed; failed.writeAt(0, "partial", 7);
        writer.write(path, std::move(failed), [](ChunkedBuffer&) { throw std::runtime_error("injected chunk finalization failure"); });
        writer.waitUntilIdle();
        REQUIRE(contents(path) == previous);
    }

    for(bool cooperative:{false,true}) {
        BackgroundFileWriter writer(&files,cooperative);
        const auto owner=std::this_thread::get_id();
        bool encoded=false;
        auto job=writer.submit(path,[&](ChunkedBuffer& out)->CooperativeTask {
            CHECK((std::this_thread::get_id()==owner)==(cooperative || !ThreadSupport::available));
            for(unsigned i=0;i<8;++i) {
                std::string block(65536,char(i));out.writeAt(out.size(),block.data(),block.size());
                co_await CooperativeTask::checkpoint();
            }
            encoded=true;co_return true;
        });
        if(cooperative) CHECK(!encoded);
        writer.waitUntilIdle();REQUIRE(job->state==BackgroundFileWriter::State::Succeeded);REQUIRE(encoded);
        std::string decoded;REQUIRE(gzipDecompress(contents(path),decoded));
        REQUIRE(decoded.size()==8*65536);
        for(unsigned i=0;i<8;++i) REQUIRE(decoded.substr(i*65536,65536)==std::string(65536,char(i)));
        const auto prior=contents(path);
        auto bad=writer.submit(path,[](ChunkedBuffer&)->CooperativeTask {throw std::runtime_error("injected snapshot failure");co_return false;});
        writer.waitUntilIdle();REQUIRE(bad->state==BackgroundFileWriter::State::Failed);REQUIRE(contents(path)==prior);
    }

    {
        const auto prior=contents(path);
        ChunkedBuffer bytes;std::string raw(256*1024,'x');bytes.writeAt(0,raw.data(),raw.size());
        {
            auto task=files.writeGzipTask(path,bytes);
            CHECK(!task.advance());CHECK(contents(path)==prior);
            // Destroy a suspended write: its exclusive temporary must disappear.
        }
        CHECK(contents(path)==prior);
        for(const auto& entry:fs::directory_iterator(directory))
            CHECK(entry.path().filename().string().find(".save-job-")==std::string::npos);
    }

	for (const auto& entry : fs::directory_iterator(directory))
		REQUIRE(entry.path().filename().string().find(".tmp-") == std::string::npos);
	std::cout << "PASS background writes keep the newest snapshot with its finish step, finish on destruction and continue after a failure" << std::endl;
}

static void checkSaveOperation(FileManager& files, const fs::path& directory)
{
    using State = ApplicationHost::PersistenceState;
    struct ControlledPersistence final : ApplicationHost::Persistence
    {
        State& status;
        explicit ControlledPersistence(State& status) : status(status) {}
        State state() const override { return status; }
    };
    const auto path = (directory / "operation.game").string();
    BackgroundFileWriter writer(&files, true);
    bool releasePrior = false;
    auto prior = writer.submit(path, [&](ChunkedBuffer& bytes) -> CooperativeTask {
        while (!releasePrior) co_await CooperativeTask::checkpoint();
        bytes.writeAt(0, "prior", 5);
        co_return true;
    });
    unsigned captures = 0, persisted = 0, completed = 0;
    State storage = State::Pending;
    SaveOperation save(writer, path,
        [&]() -> BackgroundFileWriter::Encode {
            ++captures;
            return [](ChunkedBuffer& bytes) -> CooperativeTask {
                bytes.writeAt(0, "manual", 6);
                co_return true;
            };
        }, [&] { ++completed; }, [&]() -> std::unique_ptr<ApplicationHost::Persistence> {
            ++persisted;
            std::string decoded;
            REQUIRE(gzipDecompress(contents(path), decoded));
            CHECK(decoded == "manual"); // Persistence cannot begin before replacement.
            return std::make_unique<ControlledPersistence>(storage);
        });
    for (int i = 0; i < 3; ++i) CHECK(save.state() == State::Pending);
    CHECK(captures == 0);
    CHECK(persisted == 0);
    releasePrior = true;
    for (int i = 0; i < 100 && persisted == 0; ++i) CHECK(save.state() == State::Pending);
    REQUIRE(persisted == 1);
    CHECK(prior->state == BackgroundFileWriter::State::Succeeded);
    CHECK(captures == 1);
    CHECK(completed == 0);
    storage = State::Succeeded;
    for (int i = 0; i < 3; ++i) CHECK(save.state() == State::Succeeded);
    CHECK(completed == 1);
    CHECK(captures == 1);
    CHECK(persisted == 1);

    // Each failure is terminal for its operation. Retrying creates a new job;
    // a failed persistence must never publish success or keep capturing state.
    for (bool captureFailure : {false, true})
    {
        captures = persisted = completed = 0;
        storage = State::Failed;
        const auto previous = contents(path);
        SaveOperation failed(writer, path,
            [&]() -> BackgroundFileWriter::Encode {
                ++captures;
                if (captureFailure) throw std::runtime_error("injected capture failure");
                return [](ChunkedBuffer& bytes) -> CooperativeTask {
                    bytes.writeAt(0, "retry", 5);
                    co_return true;
                };
            }, [&] { ++completed; }, [&]() -> std::unique_ptr<ApplicationHost::Persistence> {
                ++persisted;
                return std::make_unique<ControlledPersistence>(storage);
            });
        State status = State::Pending;
        for (int i = 0; i < 100 && status == State::Pending; ++i) status = failed.state();
        REQUIRE(status == State::Failed);
        for (int i = 0; i < 3; ++i) CHECK(failed.state() == State::Failed);
        CHECK(captures == 1);
        CHECK(completed == 0);
        CHECK(persisted == (captureFailure ? 0 : 1));
        if (captureFailure) CHECK(contents(path) == previous);
    }
}

static void checkSaveDialogExitLifecycle()
{
    using State = ApplicationHost::PersistenceState;
    struct ControlledPersistence final : ApplicationHost::Persistence
    {
        State& status;
        explicit ControlledPersistence(State& status) : status(status) {}
        State state() const override { return status; }
    };
    glob2test::GameOptions options; options.header = true;
    glob2test::HeadlessGame world(options);
    auto dialog = std::make_unique<LoadSaveDialog>("games", "game", false);
    auto* active = dialog.get();
    State storage = State::Pending;
    active->beginPersistence(std::make_unique<ControlledPersistence>(storage));
    SavegameSafetyHarness::openSaveDialog(world.gui, std::move(dialog));
    SavegameSafetyHarness::stop(world.gui);
    REQUIRE(world.gui.savePending());
    SavegameSafetyHarness::closeDialog(world.gui);
    SavegameSafetyHarness::openSaveDialog(world.gui,
        std::make_unique<LoadSaveDialog>("games", "game", false));
    SavegameSafetyHarness::pollSaveDialog(world.gui);
    CHECK(active->isPersisting()); // Pending operation survived both panel actions.
    storage = State::Failed;
    SavegameSafetyHarness::pollSaveDialog(world.gui);
    CHECK(active->filePresentation().failed);
    CHECK(world.gui.savePending()); // Session exit must retain retry/cancel UI.
    storage = State::Pending;
    active->beginPersistence(std::make_unique<ControlledPersistence>(storage));
    SavegameSafetyHarness::pollSaveDialog(world.gui);
    CHECK(world.gui.savePending());
    storage = State::Succeeded;
    SavegameSafetyHarness::pollSaveDialog(world.gui);
    CHECK_FALSE(world.gui.savePending());

    dialog = std::make_unique<LoadSaveDialog>("games", "game", false);
    active = dialog.get();
    storage = State::Failed;
    active->beginPersistence(std::make_unique<ControlledPersistence>(storage));
    SavegameSafetyHarness::openSaveDialog(world.gui, std::move(dialog));
    SavegameSafetyHarness::pollSaveDialog(world.gui);
    REQUIRE(world.gui.savePending());
    active->cancelPresentedFile();
    SavegameSafetyHarness::pollSaveDialog(world.gui);
    CHECK_FALSE(world.gui.savePending());
}

static void checkCooperativeWriterCompatibility(FileManager& files, const fs::path& directory)
{
    const auto path = (directory / "cooperative-mixed.game").string();
    BackgroundFileWriter writer(&files, true);
    auto snapshot = writer.submit(path, [](ChunkedBuffer& bytes) -> CooperativeTask {
        co_await CooperativeTask::checkpoint();
        bytes.writeAt(0, "snapshot", 8);
        co_return true;
    });
    writer.write(path, "queued legacy");
    writer.waitUntilIdle();
    CHECK(snapshot->state == BackgroundFileWriter::State::Succeeded);
    CHECK_FALSE(writer.busy());
    CHECK(contents(path) == "queued legacy");
    writer.write(path, "legacy before snapshot");
    writer.waitUntilIdle();
    auto next = writer.submit(path, [](ChunkedBuffer& bytes) -> CooperativeTask {
        bytes.writeAt(0, "next", 4);
        co_return true;
    });
    writer.waitUntilIdle();
    CHECK(next->state == BackgroundFileWriter::State::Succeeded);
    std::string decoded;
    REQUIRE(gzipDecompress(contents(path), decoded));
    CHECK(decoded == "next");

    // Polling completes the job; the next submission publishes its metrics,
    // just like the legacy write API, without requiring a teardown wait.
    const auto completedBefore = PerformanceTelemetry::collector().saved;
    const auto encode = [](ChunkedBuffer& bytes) -> CooperativeTask {
        bytes.writeAt(0, "metrics", 7);
        co_return true;
    };
    auto measured = writer.submit(path, encode);
    for (int i = 0; i < 100 && writer.busy(); ++i) {}
    REQUIRE(measured->state == BackgroundFileWriter::State::Succeeded);
    auto following = writer.submit(path, encode);
    CHECK(PerformanceTelemetry::collector().saved == completedBefore + 1);
    writer.waitUntilIdle();
    CHECK(following->state == BackgroundFileWriter::State::Succeeded);

    const auto immediate = (directory / "immediate-gzip.game").string();
    // Cross output/input-buffer boundaries, with both repetitive and random data.
    std::string raw(300000, 'x');
    Uint32 random = 42;
    for (size_t i = 65500; i < 200000; ++i)
    {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        raw[i] = char(random);
    }
    ChunkedBuffer bytes;
    bytes.writeAt(0, raw.data(), raw.size());
    REQUIRE(files.writeGzipAtomic(immediate, bytes));
    REQUIRE(files.writeGzipTask(path, bytes).run());
    CHECK(contents(path) == contents(immediate));
}

static void checkSlowAutosave(FileManager& files, const fs::path& directory)
{
#ifndef __EMSCRIPTEN__
    GameGUI gui;
    auto map = Engine::loadMapHeader("maps/balanced.map");
    GameHeader header; header.setNumberOfPlayers(1); header.setRandomSeed(123456);
    header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
    REQUIRE(gui.loadFromHeaders(map, header, true, true));
    gui.localPlayer = gui.localTeamNo = 0; gui.adjustLocalTeam();
    const fs::path save = directory / "games" / "Auto_save.game.gz";
    std::promise<void> started, release;
    auto ready = started.get_future(); auto released = release.get_future();
    std::atomic<bool> finalized{false};
    ChunkedBuffer previous; previous.writeAt(0, "previous", 8);
    SavegameSafetyHarness::writer(gui, files).write(save.string(), std::move(previous), [&](ChunkedBuffer&) {
        started.set_value(); released.wait(); finalized = true;
    });
    ready.wait();
    std::thread unblock([&] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); release.set_value(); });
    globalContainer->settings.autosaveGames = true;
    gui.game.stepCounter = AUTOSAVE_PHASE_TICKS;
    gui.syncStep();
    const bool waited = finalized.load();
    unblock.join();
    REQUIRE(!waited);
    gui.waitForAutosave();
    ++gui.game.stepCounter;
    gui.syncStep();
    gui.waitForAutosave();
    BinaryInputStream stream(files.openInflatingInputStreamBackend(save.string()));
    GameGUI restored;
    REQUIRE(restored.load(&stream));
    REQUIRE(restored.game.stepCounter == AUTOSAVE_PHASE_TICKS+1);
    globalContainer->settings.autosaveGames = false;
    fs::remove(save);
    std::cout << "PASS busy autosave defers capture without blocking and retains the later exact tick" << std::endl;
#endif
}

static std::unique_ptr<BinaryInputStream> input(const std::string& bytes, bool file)
{
	if (file)
	{
		FILE *fp = tmpfile();
		REQUIRE((fp && fwrite(bytes.data(), 1, bytes.size(), fp) == bytes.size()));
		rewind(fp);
		return std::make_unique<BinaryInputStream>(new FileStreamBackend(fp));
	}
	auto *backend = new MemoryStreamBackend(bytes.data(), bytes.size());
	backend->seekFromStart(0);
	return std::make_unique<BinaryInputStream>(backend);
}

static void checkPendingConstruction()
{
	const int type = globalContainer->buildingsTypes.getTypeNum("inn", 0, true);
	for (bool text : {false, true})
	{
		GameGUI source, target;
		for (Game* game : {&source.game, &target.game})
		{
			game->map.setSize(5, 5, GRASS);
			game->map.setGame(game);
			game->addTeam(0);
			game->teams[0]->race.loadDefault();
			game->map.setMapDiscovered();
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x) game->map.clearImmobileUnit(x, y);
		}
		const auto roundTrip = [&] {
			auto* backend = new MemoryStreamBackend;
			std::unique_ptr<OutputStream> writer;
			if (text) writer = std::make_unique<TextOutputStream>(backend);
			else writer = std::make_unique<BinaryOutputStream>(backend);
			source.game.saveBuildProjects(writer.get());
			writer->flush();
			MemoryStreamBackend bytes(*backend);
			bytes.seekFromStart(0);
			if (text)
			{
				TextInputStream reader(&bytes);
				target.game.loadBuildProjects(&reader);
			}
			else
			{
				BinaryInputStream reader(new MemoryStreamBackend(bytes));
				target.game.loadBuildProjects(&reader);
			}
		};
		source.game.buildProjects = {{8, 8, 0, type, 2, 3}, {16, 16, 0, type, 4, 5}};
		roundTrip();
		REQUIRE(target.game.buildProjects.size() == 2);
		auto first = target.game.buildProjects.begin(), second = std::next(first);
		REQUIRE((first->posX == 8 && first->posY == 8 && first->teamNumber == 0 && first->typeNum == type
			&& first->unitWorking == 2 && first->unitWorkingFuture == 3
			&& second->posX == 16 && second->posY == 16 && second->teamNumber == 0 && second->typeNum == type
			&& second->unitWorking == 4 && second->unitWorkingFuture == 5));
		target.game.map.setGroundUnit(8, 8, 0);
		target.game.buildProjectSyncStep(0);
		REQUIRE(target.game.buildProjects.size() == 1);
		target.game.map.setGroundUnit(8, 8, NOGUID);
		target.game.buildProjectSyncStep(0);
		REQUIRE(target.game.buildProjects.empty());
		REQUIRE(target.game.map.getBuilding(8, 8) != NOGBID);
		source.game.buildProjects.clear();
		roundTrip();
		REQUIRE(target.game.buildProjects.empty());
		source.game.buildProjects = {{8, 8, 99, type, 2, 3}};
		bool rejected = false;
		try { roundTrip(); }
		catch (const std::runtime_error&) { rejected = true; }
		REQUIRE((rejected && target.game.buildProjects.empty()));
	}
	std::cout << "PASS pending construction binary/text round trips, queue order, staffing, delayed placement, empty queue and invalid reference" << std::endl;
}

static void checkRandomContinuation(bool text, bool ai)
{
	GameGUI gui;
	auto map = Engine::loadMapHeader("maps/balanced.map");
	GameHeader header;
	header.setNumberOfPlayers(1);
	header.setRandomSeed(123456);
	header.getBasePlayer(0) = BasePlayer(0, "Test", 0, ai ? BasePlayer::playerTypeFromImplementationID(AI::NUMBI) : BasePlayer::P_LOCAL);
	REQUIRE(gui.loadFromHeaders(map, header, true, true));
	// Advance the game's own stream away from its seed before checkpointing.
	for (int i=0; i<713; ++i) gui.game.syncRandom();
	const auto step = [](Game &game) {
		if (game.players[0]->ai)
		{
			auto order=game.players[0]->ai->getOrder(false);
			order->sender=0;
			game.executeOrder(order,0);
		}
		game.syncStep(0);
	};
	for (int i=0; i<100; ++i) step(gui.game);
	const auto savedRandom = gui.game.syncRandom;
	auto *backend = new MemoryStreamBackend();
	class CheckpointOutput : public BinaryOutputStream
	{
	public:
		size_t runtimeStart=0, runtimeEnd=0, pipelineStart=0;
		explicit CheckpointOutput(StreamBackend *backend) : BinaryOutputStream(backend) {}
		void writeEnterSection(const std::string name) override
		{
			if (name=="mapRuntime") runtimeStart=getPosition();
			if (name=="GameGUI") runtimeEnd=getPosition();
			if (name=="gradientPipeline") pipelineStart=getPosition();
			BinaryOutputStream::writeEnterSection(name);
		}
	} output(backend);
	gui.save(&output, "RNG continuation");
	const std::string bytes(backend->getBuffer(), backend->getPosition());
	std::string runtimeText;
	if (text)
	{
		auto *storage=new MemoryStreamBackend();
		TextOutputStream writer(storage);
		gui.game.map.saveRuntimeState(&writer);
		runtimeText.assign(storage->getBuffer(),storage->getPosition());
	}

	if (!(gui.game.syncRandom == savedRandom))
	{
		std::ostringstream now, was;
		now << gui.game.syncRandom;
		was << savedRandom;
		std::cerr << "sync RNG changed across save:\n  was " << was.str().substr(0, 96)
				  << "\n  now " << now.str().substr(0, 96) << std::endl;
		REQUIRE(false);
	}
	auto simulationState = [](Game &game) {
		std::vector<Uint32> result, buildings, units;
		game.checkSum(&result, &buildings, &units, true);
		result.erase(result.begin()); // save upgrades the map format header
		result.insert(result.end(), buildings.begin(), buildings.end());
		result.insert(result.end(), units.begin(), units.end());
		return result;
	};
	std::vector<std::vector<Uint32>> continuation;
	std::vector<std::vector<GameplayMeasurements>> measurementContinuation;
	const auto savedAI = ai ? gui.game.players[0]->ai->telemetrySeries->current : AITelemetry::Sample{};
	for (int i=0; i<700; ++i)
	{
		step(gui.game);
		continuation.push_back(simulationState(gui.game));
		std::vector<GameplayMeasurements> measurements;
		for (int t=0; t<gui.game.teamsCount(); ++t) measurements.push_back(gui.game.teams[t]->stats.measurements);
		measurementContinuation.push_back(measurements);
	}
	// Unrelated draws from the process stream must not affect the restored game.
	for (int i=0; i<37; ++i) syncRand();
	GameGUI restored;
	if (!text && !ai)
	{
		REQUIRE((output.runtimeStart>0 && output.runtimeEnd>output.runtimeStart));
		for (const auto cut : {output.runtimeStart,output.runtimeStart+7,output.runtimeEnd-1})
		{
			auto partial=input(bytes.substr(0,cut),true);
			bool rejected=false;
			try { rejected=!restored.load(partial.get()); }
			catch (const std::exception&) { rejected=true; }
			REQUIRE(rejected);
		}
		REQUIRE(output.pipelineStart>output.runtimeStart);
		// Reject invalid queue sizes, deadlines, destinations and cancellation flags.
		const auto pipeline=output.pipelineStart;
		REQUIRE(static_cast<unsigned char>(bytes[pipeline+1])>0);
		for (const auto [offset,value] : std::vector<std::pair<size_t,unsigned char>>{
			{pipeline,0}, {pipeline,17}, {pipeline+1,17},
			{pipeline+2,255}, {pipeline+4,0}, {pipeline+4,17}, {pipeline+5,2}}) {
			auto malformed=bytes; malformed[offset]=value;
			auto bad=input(malformed,true); bool rejected=false;
			try { rejected=!restored.load(bad.get()); } catch(const std::exception &) { rejected=true; }
			REQUIRE(rejected);
		}
		auto corrupt=bytes;
		corrupt[output.runtimeStart]=2; // fog buffer selector must be 0 or 1
		auto invalid=input(corrupt,true);
		bool rejected=false;
		try { rejected=!restored.load(invalid.get()); }
		catch (const std::exception&) { rejected=true; }
		REQUIRE(rejected);
	}
	auto stream=input(bytes,true);
	REQUIRE(restored.load(stream.get()));
	if (ai) REQUIRE(restored.game.players[0]->ai->telemetrySeries->current == savedAI);
	if (text)
	{
		MemoryStreamBackend source(runtimeText.data(),runtimeText.size());
		source.seekFromStart(0);
		TextInputStream reader(&source);
		restored.game.map.loadRuntimeState(&reader, VERSION_MINOR);
	}

	// The normal saved-game loader replaces the player header after loading.
	restored.game.setGameHeader(header, true);
	REQUIRE(restored.game.syncRandom == savedRandom);
	for (int i=0; i<700; ++i)
	{
		step(restored.game);
		const auto actual = simulationState(restored.game);

		for (int t=0; t<restored.game.teamsCount(); ++t)
			REQUIRE(restored.game.teams[t]->stats.measurements == measurementContinuation[i][t]);
		if (actual != continuation[i])
		{
			std::cerr << "Continuation mismatch at step " << i << " sizes " << actual.size() << '/' << continuation[i].size() << std::endl;
			for (size_t j=0; j<std::min(actual.size(),continuation[i].size()); ++j)
				if (actual[j]!=continuation[i][j]) std::cerr << " component " << j << ": " << actual[j] << " != " << continuation[i][j] << std::endl;
			REQUIRE(false);
		}
	}
	for (int t=0; t<restored.game.teamsCount(); ++t)
		REQUIRE(restored.game.teams[t]->stats.measurementHistory == gui.game.teams[t]->stats.measurementHistory);

	std::cout << "PASS " << (text ? "binary + text routing" : "binary") << (ai ? " AI" : " human") << " saved game continues RNG and 700 simulation steps and measurements across header replacement" << std::endl;
}

static std::string headerBytes(const MapHeader& header)
{
	auto *backend = new MemoryStreamBackend;
	BinaryOutputStream output(backend);
	header.save(&output);
	backend->seekFromStart(0);
	std::string bytes;
	while (!backend->isEndOfStream()) bytes += char(backend->getChar());
	return bytes;
}

static void checkMapHeaders()
{
	MapHeader source;
	source.setMapName("Import validation");
	source.setNumberOfTeams(1);
	const std::string bytes = headerBytes(source);
	for (bool file : {false, true})
	{
		MapHeader header;
		header.setMapName("Previous selection");
		const std::string previous = headerBytes(header);
		for (size_t cut = 0; cut < bytes.size(); ++cut)
		{
			auto stream = input(bytes.substr(0, cut), file);
			bool rejected = false;
			try { rejected = !header.load(stream.get()); }
			catch (const std::ios_base::failure&) { rejected = true; }
			REQUIRE((rejected && headerBytes(header) == previous));
		}
		const size_t fields = 4 + source.getMapName().size();
		const auto replace = [&](size_t offset, Uint32 value) {
			std::string corrupt = bytes;
			for (int i = 0; i < 4; ++i) corrupt[offset + i] = char(value >> (24 - i * 8));
			return corrupt;
		};
		for (const auto& corrupt : {replace(fields, VERSION_MAJOR + 1),
			replace(fields + 4, MINIMUM_VERSION_MINOR - 1), replace(fields + 4, VERSION_MINOR + 1),
			replace(fields + 8, 0xffffffffu), replace(fields + 8, Team::MAX_COUNT + 1)})
		{
			auto stream = input(corrupt, file);
			REQUIRE((!header.load(stream.get()) && headerBytes(header) == previous));
		}
		auto corrupt = bytes;
		corrupt[fields + 16] = 2;
		auto invalid = input(corrupt, file);
		REQUIRE((!header.load(invalid.get()) && headerBytes(header) == previous));
		auto valid = input(bytes, file);
		REQUIRE((header.load(valid.get()) && headerBytes(header) == bytes));
	}
	std::cout << "PASS every truncated header, invalid versions/team counts/save flags rejected; previous header preserved; valid reload succeeds" << std::endl;
}

static void checkReplayImports(GameGUI& gui, const fs::path& directory)
{
    for (bool finished : {false, true}) {
        const auto recording = directory / (finished ? "finished.replay" : "live.replay");
        ReplayWriter writer;
        writer.init(recording.string(), gui);
        REQUIRE(writer.isValid());
        writer.advanceStep();
        if (finished) writer.finish();
        REQUIRE(writer.write(recording.string()));
        const auto bytes = contents(recording);
        const auto validate = [&](const std::string& name, const std::string& payload) {
            FileImport operation(ApplicationHost::SelectedFile{name,
                std::vector<unsigned char>(payload.begin(), payload.end())}, "replay");
            for (unsigned i = 0; i < 100000 && (operation.state() == FileImport::State::Validating ||
                operation.state() == FileImport::State::Persisting); ++i)
                operation.advance();
            return operation.state();
        };
        REQUIRE(validate(finished ? "ImportedFinished.replay" : "ImportedLive.replay", bytes) == FileImport::State::Succeeded);
        REQUIRE(validate("Truncated.replay", bytes.substr(0, bytes.size() - 1)) == FileImport::State::Failed);
        REQUIRE(validate("Trailing.replay", bytes + "x") == FileImport::State::Failed);
        auto badTelemetry = bytes;
        REQUIRE(badTelemetry.size() >= 17);
        badTelemetry[badTelemetry.size() - 17] ^= 1; // Empty telemetry footer magic.
        REQUIRE(validate("BadTelemetry.replay", badTelemetry) == FileImport::State::Failed);
    }
}

static void checkImports(const std::string& bytes, const fs::path& directory)
{
    using namespace ApplicationHost;
    const auto selected = [&](const std::string& name, const std::string& payload) {
        return SelectedFile{name, std::vector<unsigned char>(payload.begin(), payload.end())};
    };
    const auto validate = [](FileImport& operation) {
        for (unsigned i = 0; i < 100000 && operation.state() == FileImport::State::Validating; ++i) operation.advance();
        REQUIRE(operation.state() != FileImport::State::Validating);
    };
    const auto beforeRng = getSyncRandState();
    const auto preferences = directory / "preferences.txt";
    { std::ofstream out(preferences); out << "preserved preferences"; }
    const auto preferencesTime = fs::last_write_time(preferences);
    const bool remember = globalContainer->settings.rememberUnit;
    globalContainer->settings.rememberUnit = true;
    {
        FileImport cancelled(selected("cancel.game", bytes), "game", persistStorage,
            CooperativeSlice(std::chrono::steady_clock::now, std::chrono::milliseconds(4), 1));
        cancelled.advance();
        REQUIRE(cancelled.state() == FileImport::State::Validating);
    }
    REQUIRE((getSyncRandState() == beforeRng && !fs::exists(directory / "games/cancel.game")));
    for (const auto& payload : {bytes.substr(0, 3), bytes.substr(0, bytes.size()-1), bytes + "extra"}) {
        FileImport invalid(selected("invalid.game", payload), "game");
        validate(invalid);
        REQUIRE((invalid.state() == FileImport::State::Failed && invalid.path().empty()));
    }
    {
        auto stream = input(bytes, false);
        MapHeader header; REQUIRE(header.load(stream.get()));
        auto badCount = bytes;
        // GameHeader begins with latency (4), order rate (1), player count (4).
        std::fill_n(badCount.begin() + stream->getPosition() + 5, 4, char(0xff));
        auto badOffset = bytes;
        const auto offsetField = 4 + header.getMapName().size() + 12;
        const Uint32 offset = header.getMapOffset() + 1;
        for (int i = 0; i < 4; ++i) badOffset[offsetField+i] = char(offset >> (24-i*8));
        auto badPlayer = bytes;
        const auto player = badPlayer.find("PLYb"); REQUIRE(player != std::string::npos);
        badPlayer[player] = '!';
        for (const auto& corrupt : {badCount, badPlayer, badOffset}) {
            FileImport invalid(selected("invalid.game", corrupt), "game");
            validate(invalid); REQUIRE(invalid.state() == FileImport::State::Failed);
        }
    }
    for (const auto& name : {"../escape.game", "con.game", "wrong.map"}) {
        FileImport invalid(selected(name, bytes), "game");
        validate(invalid); REQUIRE(invalid.state() == FileImport::State::Failed);
    }
    {
        std::string compressed;
        REQUIRE(gzipCompress(bytes, 6, compressed));
        FileImport operation(selected("Compressed.GAME.GZ", compressed), "game");
        validate(operation); operation.advance();
        REQUIRE(operation.state() == FileImport::State::Succeeded);
        REQUIRE(operation.path() == "games/Compressed.game.gz");
        REQUIRE(contents(directory / operation.path()) == compressed);
        FileImport rawSibling(selected("Compressed.game", bytes), "game");
        validate(rawSibling); rawSibling.advance();
        REQUIRE(rawSibling.state() == FileImport::State::Succeeded);
        REQUIRE(rawSibling.path() == "games/Compressed_(1).game");
        FileImport gzipSibling(selected("Compressed_(1).game.gz", compressed), "game");
        validate(gzipSibling); gzipSibling.advance();
        REQUIRE(gzipSibling.state() == FileImport::State::Succeeded);
        REQUIRE(gzipSibling.path() == "games/Compressed_(1)_(1).game.gz");
        for (const auto& corrupt : {compressed.substr(0, compressed.size()-1), compressed + "extra"}) {
            FileImport invalid(selected("invalid.game.gz", corrupt), "game");
            validate(invalid); REQUIRE(invalid.state() == FileImport::State::Failed);
        }
    }
    const auto original = directory / "games/Imported.game";
    { std::ofstream out(original, std::ios::binary); out << "previous save"; }
    struct ControlledPersistence : Persistence {
        std::shared_ptr<PersistenceState> result;
        explicit ControlledPersistence(std::shared_ptr<PersistenceState> result) : result(std::move(result)) {}
        PersistenceState state() const override { return *result; }
    };
    auto result = std::make_shared<PersistenceState>(PersistenceState::Pending);
    const auto persist = [result] { return std::make_unique<ControlledPersistence>(result); };
    std::string imported;
    {
        FileImport operation(selected("Imported.game", bytes), "game", persist);
        validate(operation);
        REQUIRE(operation.state() == FileImport::State::Persisting);
        imported = operation.path();
        REQUIRE(imported == "games/Imported_(1).game");
        REQUIRE((contents(directory / imported) == bytes && contents(original) == "previous save"));
        operation.advance(); REQUIRE(operation.state() == FileImport::State::Persisting);
        *result = PersistenceState::Failed;
        operation.advance(); REQUIRE(operation.canRetry());
        operation.retryPersistence();
        *result = PersistenceState::Succeeded;
        operation.advance(); REQUIRE(operation.state() == FileImport::State::Succeeded);
    }
    REQUIRE((contents(directory / imported) == bytes && contents(original) == "previous save"));
    {
        *result = PersistenceState::Failed;
        FileImport operation(selected("abandoned.game", bytes), "game", persist);
        validate(operation); operation.advance(); REQUIRE(operation.canRetry());
    }
    REQUIRE(!fs::exists(directory / "games/abandoned.game"));
    REQUIRE(getSyncRandState() == beforeRng);
    REQUIRE((contents(preferences) == "preserved preferences" && fs::last_write_time(preferences) == preferencesTime));
    globalContainer->settings.rememberUnit = remember;
    std::cout << "PASS imported save full validation, cancellation/RNG restoration, name collision, pending/failure/retry persistence and abandoned-file cleanup" << std::endl;
}

static void checkCampaignProgress(const fs::path& directory)
{
    Campaign base;
    base.setName("Progress fixture");
    base.setPlayerName("First player");
    CampaignMapEntry first("First", "campaigns/first.map"), second("Second", "campaigns/second.map");
    first.unlockMap(); second.lockMap(); second.getUnlockedByMaps().push_back("First");
    base.appendMap(first); base.appendMap(second);
    Campaign source = base;
    source.setCompleted("First"); source.setPlayerName("Restored player");
    const auto backup = source.exportProgress();
    const auto unchanged = base.exportProgress();
    for (size_t cut = 0; cut < backup.size(); ++cut) {
        REQUIRE(!base.importProgress({backup.begin(), backup.begin()+cut}));
        REQUIRE(base.exportProgress() == unchanged);
    }
    auto extra = backup; extra.push_back(0);
    REQUIRE(!base.importProgress(extra));
    REQUIRE(!base.importProgress(std::vector<unsigned char>(1024*1024+1)));
    auto invalidFlag = backup; invalidFlag.back() = 2;
    REQUIRE(!base.importProgress(invalidFlag));
    auto future = backup; future[7] = 2;
    REQUIRE(!base.importProgress(future));
    Campaign changed = base; changed.getMap(0).setMapFileName("../different.map");
    REQUIRE(!changed.importProgress(backup));
    changed = base; changed.getMap(1).getUnlockedByMaps().clear();
    REQUIRE(!changed.importProgress(backup));
    changed = base; changed.setName("Different campaign");
    REQUIRE(!changed.importProgress(backup));
    base.getMap(1).unlockMap(); base.getMap(1).setCompleted(true);
    REQUIRE(base.importProgress(backup));
    REQUIRE((base.getMap(0).isCompleted() && base.getMap(1).isCompleted() && base.getMap(1).isUnlocked()));
    REQUIRE(base.getPlayerName() == "Restored player");
    REQUIRE(base.save(true));
    const auto file = directory / "games/Progress_fixture.txt";
    const auto previous = contents(file);
    Campaign restored; REQUIRE(restored.load(file.string()));
    REQUIRE(restored.exportProgress() == base.exportProgress());
#ifndef WIN32
    const auto child = fork(); REQUIRE(child >= 0);
    if (child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        struct rlimit budget = {0,0};
        if (setrlimit(RLIMIT_FSIZE, &budget)) _exit(2);
        base.setPlayerName("Unwritten");
        _exit(base.save(true) ? 3 : 0);
    }
    int status = 0; REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE((WIFEXITED(status) && WEXITSTATUS(status) == 0));
    REQUIRE(contents(file) == previous);
#endif
    std::cout << "PASS campaign progress truncation/version/definition validation, monotonic merge, legacy text round trip and atomic failure preservation" << std::endl;
}

static void checkPreferences(const fs::path& directory)
{
    Settings settings;
    settings.setGraphicsDetail(false);
    const auto file = directory / "preferences-test.txt";
    REQUIRE(settings.save(file.string()));
    Settings loaded;
    loaded.load(file.string());
    REQUIRE(loaded.optionFlags == settings.optionFlags);
    KeyboardManager game(GameGUIShortcuts), editor(MapEditShortcuts);
    REQUIRE((game.saveKeyboardLayout() && editor.saveKeyboardLayout()));
    const auto gamePath = directory / GameGUIKeyActions::getConfigurationFile();
    const auto editorPath = directory / MapEditKeyActions::getConfigurationFile();
    const auto previous = contents(file), gameBytes = contents(gamePath), editorBytes = contents(editorPath);
    REQUIRE((!gameBytes.empty() && !editorBytes.empty()));
    const auto blocked = directory / "blocked-preferences.txt";
    fs::create_directory(blocked);
    REQUIRE((!settings.save(blocked.string()) && fs::is_directory(blocked)));
#ifndef WIN32
    const pid_t child = fork(); REQUIRE(child >= 0);
    if (child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        struct rlimit budget = {0, 0};
        if (setrlimit(RLIMIT_FSIZE, &budget) != 0) _exit(2);
        settings.optionFlags = 0;
        const bool preferencesFailed = !settings.save(file.string());
        const bool gameFailed = !game.saveKeyboardLayout();
        const bool editorFailed = !editor.saveKeyboardLayout();
        _exit(preferencesFailed && gameFailed && editorFailed ? 0 : 3);
    }
    int status = 0; REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE((WIFEXITED(status) && WEXITSTATUS(status) == 0));
    REQUIRE((contents(file) == previous && contents(gamePath) == gameBytes && contents(editorPath) == editorBytes));
#endif
    std::cout << "PASS preference/keyboard writes round trip and preserve prior files on failure" << std::endl;
}
}

TEST_SUITE("SavegameSafety")
{
	TEST_CASE("autosave defaults; atomic and background writes; truncated and malformed loads; imports and campaign progress [save-format][writes-preferences]")
	{
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings = true});
		globals->runNoX = true;
		globals->load();
		REQUIRE(!globals->settings.autosaveGames);
		globals->settings.rememberUnit = false;
		checkPendingConstruction();
		for (bool text : {false,true})
			for (bool ai : {false,true}) checkRandomContinuation(text,ai);
		const fs::path directory = fs::absolute(globals->fileManager->getDir(0));
		checkAtomicWrites(*globals->fileManager, directory);
		checkChunkedStreams();
		checkGzipWrites(*globals->fileManager, directory);
		checkBackgroundWriter(*globals->fileManager, directory);
        checkSaveOperation(*globals->fileManager, directory);
        checkSaveDialogExitLifecycle();
        checkCooperativeWriterCompatibility(*globals->fileManager, directory);
		checkSlowAutosave(*globals->fileManager, directory);
	    checkPreferences(directory);
		checkMapHeaders();
	    checkCampaignProgress(directory);
		for (bool file : {false, true})
		{
			const std::string payload("before\0after", 12);
			const std::string bytes = std::string("\0\0\0\14", 4) + payload
				+ std::string("\0\0\0\12", 4) + "next field";
			auto restored = input(bytes, file);
			REQUIRE(restored->readText("binary string") == payload);
			REQUIRE(restored->readText("following string") == "next field");
		}
		{
			GameGUI gui;
			auto map = Engine::loadMapHeader("maps/balanced.map");
			GameHeader header;
			header.setNumberOfPlayers(1);
			header.setRandomSeed(123456);
			header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
			REQUIRE(gui.loadFromHeaders(map, header, true, true));
			// Loading now leaves resource/area gradients unallocated. A simulation
			// tick must finish even before any unit has requested one of them.
			gui.game.map.syncStep(0);
			gui.game.map.syncStep(1);
			gui.localPlayer = gui.localTeamNo = 0;
			gui.adjustLocalTeam();
			gui.game.stepCounter = AUTOSAVE_PHASE_TICKS;
			// The engine's initial replay save sets the serialized map offset.
			{
				BinaryOutputStream initial(new MemoryStreamBackend());
				gui.save(&initial, "Auto save");
			}
            {
                auto encode=captureSave([&](OutputStream* out,DeferredGameSHA1* sha){gui.save(out,"Owned snapshot",sha);});
                auto* memory=new MemoryStreamBackend;BinaryOutputStream reference(memory);
                gui.save(&reference,"Owned snapshot");const auto expected=memory->takeContents();
                const auto tick=gui.game.stepCounter;gui.game.stepCounter+=77;
                ChunkedBuffer restored;REQUIRE(encode(restored).run());
                std::string actual(restored.size(),'\0');restored.readAt(0,actual.data(),actual.size());
                REQUIRE(actual==expected);gui.game.stepCounter=tick;
            }
			const fs::path save = directory / "games" / "Auto_save.game.gz";
			gui.syncStep();
			gui.waitForAutosave();
			REQUIRE(!fs::exists(save));
			globals->settings.autosaveGames = true;
			gui.syncStep();
			gui.waitForAutosave();

			const auto compressedBytes = contents(save);
			std::string bytes;
			REQUIRE(gzipDecompress(compressedBytes, bytes));
			{
				// Autosave hashes on the writer thread; a direct save hashes as it writes.
				const fs::path reference = directory / "games" / "reference.game";
				REQUIRE(globals->fileManager->writeAtomically(reference.string(), [&](OutputStream& stream) { gui.save(&stream, "Auto save"); }));
				REQUIRE(contents(reference) == bytes);
				fs::remove(reference);
			}
			{
				// A stale map offset makes the header backpatch rewrite hashed bytes; the deferred hash must still match.
				const auto serialize = [&](DeferredGameSHA1* deferred) {
					gui.game.mapHeader.setMapOffset(0);
					auto *backend = new MemoryStreamBackend();
					BinaryOutputStream stream(backend);
					gui.save(&stream, "Stale offset", deferred);
					return backend->takeContents();
				};
				const std::string inlineHashed = serialize(nullptr);
				DeferredGameSHA1 deferred;
				std::string deferredHashed = serialize(&deferred);
				REQUIRE(deferredHashed != inlineHashed);
				deferred.apply(deferredHashed);
				REQUIRE(deferredHashed == inlineHashed);
			}
			{
				// A stale map offset makes the header backpatch rewrite hashed bytes; the deferred hash must still match.
				const auto serialize = [&](DeferredGameSHA1* deferred) {
					gui.game.mapHeader.setMapOffset(0);
					auto *backend = new ChunkedStreamBackend();
					BinaryOutputStream stream(backend);
					gui.save(&stream, "Stale offset", deferred);
					return backend->takeContents();
				};
				auto inlineHashed = serialize(nullptr);
				DeferredGameSHA1 deferred;
				auto deferredHashed = serialize(&deferred);

				deferred.apply(deferredHashed);
				REQUIRE(deferredHashed.size() == inlineHashed.size());
                std::string first(inlineHashed.size(), '\0'), second(deferredHashed.size(), '\0');
                inlineHashed.readAt(0, first.data(), first.size()); deferredHashed.readAt(0, second.data(), second.size());
                REQUIRE(first == second);
			}

			{
				GameGUI restored;
				auto stream = input(bytes, true);
				REQUIRE(restored.load(stream.get()));
				std::vector<Uint32> before, after;
				gui.game.checkSum(&before, nullptr, nullptr);
				restored.game.checkSum(&after, nullptr, nullptr);
				REQUIRE(before.size() == after.size());
				// Component zero includes the map format version, upgraded on save.
				REQUIRE(std::equal(before.begin() + 1, before.end(), after.begin() + 1));
				REQUIRE(restored.game.stepCounter == gui.game.stepCounter);
			}
			gui.game.stepCounter = AUTOSAVE_PHASE_TICKS + AUTOSAVE_INTERVAL_TICKS - 1;
			gui.syncStep();
			gui.waitForAutosave();
			REQUIRE(contents(save) == compressedBytes);
	        checkReplayImports(gui, directory);
	        checkImports(bytes, directory);
	#ifndef WIN32
			const pid_t child = fork();
			REQUIRE(child >= 0);
			if (child == 0)
			{
				std::signal(SIGXFSZ, SIG_IGN);
				struct rlimit budget = {0, 0};
				if (setrlimit(RLIMIT_FSIZE, &budget) != 0) _exit(2);
				gui.game.stepCounter = AUTOSAVE_PHASE_TICKS + AUTOSAVE_INTERVAL_TICKS;
				gui.syncStep();
				gui.waitForAutosave();
				_exit(0);
			}
			int status = 0;
			REQUIRE((waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0));
			REQUIRE(contents(save) == compressedBytes);
	#endif
			globals->settings.autosaveGames = false;
			gui.game.stepCounter = AUTOSAVE_PHASE_TICKS + 2 * AUTOSAVE_INTERVAL_TICKS;
			gui.syncStep();
			gui.waitForAutosave();
			REQUIRE(contents(save) == compressedBytes);
			globals->settings.autosaveGames = true;
			auto stream = input(bytes, false);
			MapHeader savedHeader;
			REQUIRE(savedHeader.load(stream.get()));
			REQUIRE(std::any_of(savedHeader.getGameSHA1(), savedHeader.getGameSHA1() + SHA1_BYTE_LEN, [](Uint8 byte) { return byte != 0; }));
			{
				// A joining client keeps its own copy only when its header, SHA1 included, matches the host's.
				Engine engine;
				MapHeader host = savedHeader;
				REQUIRE(host.getFileName() == "games/Auto_save.game");
				REQUIRE(engine.haveMap(host));
				host.getGameSHA1()[0] ^= 1;
				REQUIRE(!engine.haveMap(host));
			}
			const size_t offset = savedHeader.getMapOffset();
			REQUIRE(bytes.substr(offset, 4) == "MapB");
			const auto mapBytes = bytes.substr(offset);
			struct CellBounds : BinaryOutputStream
            {
                using BinaryOutputStream::BinaryOutputStream;
                size_t depth=0,cellDepth=0,start=0,end=0;
                void writeEnterSection(const std::string name) override {
                    ++depth;if(name=="cases") {start=getPosition();cellDepth=depth;}
                }
                void writeEnterSection(unsigned) override {++depth;}
                void writeLeaveSection(size_t count=1) override {
                    while(count--) {if(depth==cellDepth && cellDepth) {end=getPosition();cellDepth=0;} --depth;}
                }
            };
            auto* measured=new MemoryStreamBackend;
            CellBounds bounds(measured);gui.game.map.save(&bounds);
            const size_t cellsStart=bounds.start,cellsEnd=bounds.end;
            REQUIRE(measured->takeContents().substr(0,cellsEnd)==mapBytes.substr(0,cellsEnd));
            const size_t mapEnd=mapBytes.find("MapE");
            REQUIRE((mapEnd!=std::string::npos && cellsStart<cellsEnd && cellsEnd<mapEnd));
            const size_t cuts[] = {0,1,3,4,7,11,12,cellsStart-1,cellsStart,
                cellsStart+(cellsEnd-cellsStart)/2,cellsEnd-1,cellsEnd,cellsEnd+1,mapEnd,mapEnd+3};
			int count = 0;
			for (bool file : {false, true})
				for (size_t cut : cuts)
				{
					Map loaded;
					auto truncated = input(mapBytes.substr(0, cut), file);
					REQUIRE(!loaded.load(truncated.get(), savedHeader, &gui.game));
					auto complete = input(mapBytes, file);
					REQUIRE(loaded.load(complete.get(), savedHeader, &gui.game));
					++count;
				}
			std::cout << "PASS " << count << " truncated map loads rejected, followed by successful reuse" << std::endl;
				const auto replaceSint32 = [](std::string& value, size_t offset, Uint32 replacement)
				{
					for (int byte = 0; byte < 4; ++byte)
						value[offset + byte] = char(replacement >> (24 - 8 * byte));
				};
				for (const auto [widthExponent, heightExponent] : {
					std::pair<Uint32, Uint32>{10, 9}, {9, 10}, {3, 9}, {9, 3},
					{0x7fffffffU, 9}, {9, 0x7fffffffU}})
				{
					std::string malformed = mapBytes;
					replaceSint32(malformed, 4, widthExponent);
					replaceSint32(malformed, 8, heightExponent);
					auto invalid = input(malformed, false);
					Map loaded;
					REQUIRE(!loaded.load(invalid.get(), savedHeader, &gui.game));
					REQUIRE((loaded.getW() == 0 && loaded.getH() == 0));
				}
				REQUIRE(Map::supportedDimensions(4, 4));
				REQUIRE(Map::supportedDimensions(9, 9));
				for (bool file : {false, true})
			{
				std::string malformed = mapBytes;
				malformed.replace(cellsEnd, 4, 4, char(0xFF));
				auto oversized = input(malformed, file);
				Map loaded;
				REQUIRE(!loaded.load(oversized.get(), savedHeader, &gui.game));
			}

		}
	}
}
