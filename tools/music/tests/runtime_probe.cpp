// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone production-code probe, built by test_runtime.py on each host.
#include "MusicLibrary.h"
#include "MusicStream.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifndef _WIN32
#include <sys/resource.h>
#endif
static void require(bool value, const char *text)
{
	if (!value)
		throw std::runtime_error(text);
}
static std::vector<unsigned char> bytes(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(file), {}};
}
static std::vector<std::int16_t> render(Music::Preview &player, unsigned frames, unsigned block)
{
	std::vector<std::int16_t> result(frames * 2);
	unsigned done = 0;
	while (done < frames)
	{
		auto count = std::min(block, frames - done);
		player.render(result.data() + done * 2, count);
		done += count;
	}
	return result;
}
int main(int argc, char **argv)
{
	try
	{
		if (argc == 4 && std::string(argv[3]) == "--reject")
		{
			Music::Library lib(argv[2]);
			bool rejected = false;
			try
			{
				lib.importZip(bytes(argv[1]));
			}
			catch (...)
			{
				rejected = true;
			}
			require(rejected && lib.list().empty(),
					"hostile archive rejected without installation");
			return 0;
		}
		require(argc == 3, "usage: probe fixture-directory install-directory");
		auto archive = bytes(std::string(argv[1]) + "/set.zip");
		Music::Library library(argv[2]);
		{
			Music::ImportJob cancelled(library, archive);
		}
		require(library.list().empty(), "cancellation installs nothing");
		auto start = std::chrono::steady_clock::now();
		Music::ImportJob job(library, archive);
		unsigned slices = 0;
		while (!job.finished())
		{
			job.advance();
			++slices;
		}
		require(job.error().empty(), job.error().c_str());
		require(job.installed().size() == 1, "one installed set");
		auto installed = job.installed().front();
		require(installed.info.title == "Moss lantern", "embedded title");
		require(library.importZip(archive).size() == 1, "identical import succeeds");
		require(library.list().size() == 1, "deduplicated");
		std::array<std::vector<unsigned char>, 3> loose;
		for (unsigned i = 0; i < 3; ++i)
			loose[i] = bytes(std::string(argv[1]) + "/a" + std::to_string(i + 1) + ".opus");
		require(library.importTracks(loose).directory == installed.directory,
				"loose tracks use embedded identity and deduplicate");
		// Simulate a destination becoming unwritable after staging was created.
		auto blockedRoot = std::filesystem::path(argv[2]) / "blocked";
		Music::Library blocked(blockedRoot);
		{
			Music::ImportJob failed(blocked, loose);
			auto target = blockedRoot / "data/zik";
			std::filesystem::remove(target);
			{
				std::ofstream obstruction(target);
				obstruction << "preserve me";
			}
			while (!failed.finished())
				failed.advance();
			require(!failed.error().empty() && failed.installed().empty(),
					"storage failure reports no installed sets");
			std::filesystem::remove(target);
			std::filesystem::create_directory(target);
		}
		require(blocked.list().empty(), "storage failure leaves no partial installation");

		auto paths = library.paths(installed.directory);
		Music::Preview a, b;
		require(a.open(paths) && b.open(paths), "open previews");
		a.playing = b.playing = true;
		require(render(a, 9000, 1024) == render(b, 9000, 73),
				"playback independent of buffer sizes");
		a.setMood(2);
		b.setMood(2);
		require(render(a, 3000, 1024) == render(b, 3000, 73), "fade starts identically");
		a.setMood(1);
		b.setMood(1);
		require(render(a, 40000, 1024) == render(b, 40000, 73),
				"queued rapid mood changes remain aligned");
		require(a.seekTo(a.duration() - .05) && b.seekTo(b.duration() - .05), "seek before loop");
		require(render(a, 12000, 1024) == render(b, 12000, 73),
				"loop and trim independent of buffer sizes");
		a.playing = false;
		auto position = a.position();
		auto silence = render(a, 3000, 1024);
		require(a.position() == position &&
					std::all_of(silence.begin(), silence.end(), [](auto x) { return x == 0; }),
				"pause retains position");
		// Decode a full loop while retaining only one fixed-size output buffer.
		a.playing = true;
		std::array<std::int16_t, 2048> pcm{};
		for (std::int64_t n = 0; n < installed.info.frames; n += 1024)
			a.render(pcm.data(), 1024);
		require(!a.failed(), "full-loop decode");
		archive[archive.size() / 2] ^= 1;
		bool rejected = false;
		try
		{
			library.importZip(archive);
		}
		catch (...)
		{
			rejected = true;
		}
		require(rejected, "corrupt ZIP rejected");
		auto elapsed =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::cout << "{\"seconds\":" << a.duration()
				  << ",\"previewObjectBytes\":" << sizeof(Music::Preview)
				  << ",\"removedFadeTableBytes\":" << Music::GameFadeFrames * 2 * sizeof(int)
				  << ",\"validationSlices\":" << slices << ",\"elapsedSeconds\":" << elapsed;
#ifndef _WIN32
		rusage usage{};
		getrusage(RUSAGE_SELF, &usage);
		std::cout << ",\"peakRssNativeUnits\":" << usage.ru_maxrss;
#endif
		std::cout << "}\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
