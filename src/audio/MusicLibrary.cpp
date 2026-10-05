// SPDX-License-Identifier: GPL-3.0-or-later
#include "MusicLibrary.h"
#include "MusicStream.h"
#include <opusfile.h>
#include <zlib.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>
namespace Music
{
namespace fs = std::filesystem;
namespace
{
std::string pathText(const fs::path &path)
{
	auto bytes = path.u8string();
	return {bytes.begin(), bytes.end()};
}
using Opus = std::unique_ptr<OggOpusFile, decltype(&op_free)>;
constexpr size_t MaxTrack = 16 * 1024 * 1024, MaxArchive = 64 * 1024 * 1024;
const char *Moods[] = {"calm", "building", "combat"};
void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}
std::string tag(const OpusTags *tags, const char *name, size_t maximum = 4096)
{
	require(tags && opus_tags_query_count(tags, name) == 1, "Missing or duplicate music metadata.");
	const char *value = opus_tags_query(tags, name, 0);
	require(value && std::char_traits<char>::length(value) <= maximum,
			"Music metadata is too large.");
	return value;
}
void requireSpan(size_t offset, size_t length, size_t end)
{
	// ZIP offsets are attacker-controlled uint32 values; addition can wrap on wasm32.
	require(offset <= end && length <= end - offset, "Invalid ZIP bounds.");
}
std::uint16_t u16(const std::vector<unsigned char> &b, size_t p)
{
	requireSpan(p, 2, b.size());
	return b[p] | b[p + 1] << 8;
}
std::uint32_t u32(const std::vector<unsigned char> &b, size_t p)
{
	requireSpan(p, 4, b.size());
	return u16(b, p) | std::uint32_t(u16(b, p + 2)) << 16;
}
std::vector<unsigned char> readFile(const fs::path &p)
{
	auto size = fs::file_size(p);
	require(size <= MaxTrack, "Music file is too large.");
	std::ifstream in(p, std::ios::binary);
	std::vector<unsigned char> b(size);
	require(bool(in.read(reinterpret_cast<char *>(b.data()), b.size())),
			"Could not read music file.");
	return b;
}
} // namespace
Metadata metadata(const fs::path &path, unsigned mood, bool validateAudio)
{
	require(mood < 3 && fs::file_size(path) <= MaxTrack, "Music file is too large.");
	int error = 0;
	Opus file(op_open_file(pathText(path).c_str(), &error), op_free);
	require(file && op_seekable(file.get()) && op_link_count(file.get()) == 1 &&
				op_channel_count(file.get(), 0) == 2,
			"Expected one stereo Opus stream.");
	auto *tags = op_tags(file.get(), 0);
	size_t tagBytes = 0;
	for (int i = 0; i < tags->comments; ++i)
	{
		require(tags->comment_lengths[i] >= 0 &&
					std::char_traits<char>::length(tags->user_comments[i]) ==
						size_t(tags->comment_lengths[i]),
				"Embedded NUL in music metadata.");
		tagBytes += tags->comment_lengths[i];
	}
	require(tagBytes <= 1024 * 1024, "Embedded metadata is too large.");
	Metadata m;
	require(tag(tags, "GLOB2_SCHEMA") == "1", "Unsupported music metadata version.");
	require(tag(tags, "GLOB2_MOOD") == Moods[mood], "Music moods are missing or mixed up.");
	m.id = tag(tags, "GLOB2_RELEASE", 36);
	require(std::regex_match(
				m.id, std::regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")),
			"Invalid release identifier.");
	m.origin = tag(tags, "GLOB2_ORIGIN", 2048);
	m.title = tag(tags, "ALBUM", 512);
	m.artist = tag(tags, "ARTIST", 512);
	m.description = tag(tags, "DESCRIPTION", 16000);
	m.license = tag(tags, "LICENSE", 64);
	m.credits = tag(tags, "COPYRIGHT", 16000);
	m.sources = tag(tags, "SOURCE", 164000);
	auto ai = tag(tags, "GLOB2_AI_GENERATED", 1);
	require(ai == "0" || ai == "1", "Invalid AI disclosure.");
	m.aiGenerated = ai == "1";
	require(m.license == "CC0-1.0" || m.license == "CC-BY-4.0" || m.license == "CC-BY-SA-4.0",
			"Unsupported music licence.");
	m.frames = op_pcm_total(file.get(), -1);
	require(m.frames >= Rate * 10 && m.frames <= Rate * 900,
			"Music must last between 10 seconds and 15 minutes.");
	require(tag(tags, "GLOB2_FRAMES", 16) == std::to_string(m.frames),
			"Music duration does not match its metadata.");
	if (auto *picture = opus_tags_query(tags, "METADATA_BLOCK_PICTURE", 0))
	{
		OpusPictureTag parsed{};
		require(opus_picture_tag_parse(&parsed, picture) == 0, "Invalid embedded cover art.");
		bool valid = parsed.format == OP_PIC_FORMAT_JPEG && parsed.data_length <= 256 * 1024 &&
					 parsed.width <= 512 && parsed.height <= 512;
		if (valid)
			m.cover.assign(parsed.data, parsed.data + parsed.data_length);
		opus_picture_tag_clear(&parsed);
		require(valid, "Cover art exceeds the supported limits.");
	}
	if (!validateAudio)
		return m;
	// Validate all packets, not only the duration advertised by the last page.
	std::array<std::int16_t, Chunk * 2> pcm{};
	std::int64_t frames = 0;
	for (;;)
	{
		int n = op_read_stereo(file.get(), pcm.data(), pcm.size());
		require(n >= 0, "Corrupt music audio.");
		if (!n)
			break;
		frames += n;
		require(frames <= m.frames, "Music duration exceeds its metadata.");
	}
	require(frames == m.frames, "Music audio is truncated.");
	return m;
}
Library::Library(fs::path userRoot) : root(std::move(userRoot) / "data/zik")
{
	fs::create_directories(root);
}
std::array<std::string, 3> Library::paths(const std::string &directory) const
{
	require(std::regex_match(directory, std::regex("community-[0-9a-f-]{36}")),
			"Invalid installed music directory.");
	return {pathText(root / directory / "a1.opus"), pathText(root / directory / "a2.opus"),
			pathText(root / directory / "a3.opus")};
}
std::vector<Installed> Library::list() const
{
	std::vector<Installed> out;
	for (const auto &entry : fs::directory_iterator(root))
	{
		auto name = entry.path().filename().string();
		if (!entry.is_directory() || !std::regex_match(name, std::regex("community-[0-9a-f-]{36}")))
			continue;
		try
		{
			out.push_back({name, metadata(entry.path() / "a1.opus", 0, false)});
		}
		catch (const std::exception &)
		{ /* leave damaged files available for external recovery */
		}
	}
	return out;
}
Installed Library::importTracks(const std::array<std::vector<unsigned char>, 3> &bytes)
{
	return installTracks(bytes, true);
}
Installed Library::installTracks(const std::array<std::vector<unsigned char>, 3> &bytes,
								 bool verifyAudio)
{
	// A private staging directory is never discovered as a complete set.
	auto stage =
		root /
		(".import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directory(stage);
	try
	{
		Metadata main;
		for (unsigned i = 0; i < 3; ++i)
		{
			require(!bytes[i].empty() && bytes[i].size() <= MaxTrack,
					"Missing or oversized mood file.");
			auto path = stage / ("a" + std::to_string(i + 1) + ".opus");
			std::ofstream file(path, std::ios::binary);
			require(
				bool(file.write(reinterpret_cast<const char *>(bytes[i].data()), bytes[i].size())),
				"Could not store music. Check free disk space.");
			file.close();
			require(bool(file), "Could not finish storing music.");
			auto m = metadata(path, i, verifyAudio);
			if (i == 0)
				main = std::move(m);
			else
				require(m.id == main.id && m.origin == main.origin && m.frames == main.frames,
						"Tracks belong to different releases or have different lengths.");
		}
		auto name = "community-" + main.id;
		auto dest = root / name;
		if (fs::exists(dest))
		{
			for (unsigned i = 0; i < 3; ++i)
				require(
					readFile(dest / ("a" + std::to_string(i + 1) + ".opus")) == bytes[i],
					"Another release already uses this identifier; existing music was preserved.");
			fs::remove_all(stage);
		}
		else
			fs::rename(stage, dest);
		return {name, main};
	}
	catch (...)
	{
		fs::remove_all(stage);
		throw;
	}
}
static std::map<std::string, std::array<std::vector<unsigned char>, 3>>
unpackZip(const std::vector<unsigned char> &bytes)
{
	require(bytes.size() >= 22 && bytes.size() <= MaxArchive,
			"Invalid or oversized music archive.");
	size_t end = bytes.size() - 22;
	for (;;)
	{
		if (u32(bytes, end) == 0x06054b50 && end + 22 + u16(bytes, end + 20) == bytes.size())
			break;
		require(end > 0 && bytes.size() - end < 65558, "Missing ZIP directory.");
		--end;
	}
	require(u16(bytes, end + 4) == 0 && u16(bytes, end + 6) == 0 &&
				u16(bytes, end + 8) == u16(bytes, end + 10),
			"Split archives are unsupported.");
	unsigned count = u16(bytes, end + 10);
	require(count > 0 && count <= 30, "An archive must contain 1–10 complete sets.");
	const size_t directory = u32(bytes, end + 16);
	size_t pos = directory, total = 0;
	requireSpan(directory, u32(bytes, end + 12), end);
	require(u32(bytes, end + 12) == end - directory, "Invalid ZIP directory bounds.");
	std::map<std::string, std::array<std::vector<unsigned char>, 3>> sets;
	for (unsigned i = 0; i < count; ++i)
	{
		requireSpan(pos, 46, end);
		require(u32(bytes, pos) == 0x02014b50, "Invalid ZIP entry.");
		auto flags = u16(bytes, pos + 8), method = u16(bytes, pos + 10);
		auto packed = u32(bytes, pos + 20), unpacked = u32(bytes, pos + 24),
			 attrs = u32(bytes, pos + 38);
		auto n = u16(bytes, pos + 28), extra = u16(bytes, pos + 30), comment = u16(bytes, pos + 32);
		requireSpan(pos + 46, size_t(n) + extra + comment, end);
		require(!(flags & ~std::uint16_t(0x080e)) && (method == 0 || method == 8) &&
					(attrs >> 16 & 0170000) != 0120000,
				"Encrypted files, links, and this compression method are unsupported.");
		std::string name(reinterpret_cast<const char *>(bytes.data() + pos + 46), n);
		std::smatch match;
		require(std::regex_match(name, match, std::regex("([A-Za-z0-9_-]{1,80}/)?a([123])\\.opus")),
				"Unexpected or unsafe archive path.");
		auto group = match[1].str();
		unsigned mood = unsigned(match[2].str()[0] - '1');
		auto &target = sets[group][mood];
		require(target.empty(), "Duplicate music archive member.");
		require(unpacked > 0 && unpacked <= MaxTrack && (total += unpacked) <= MaxArchive,
				"Archive expands beyond the music limits.");
		size_t local = u32(bytes, pos + 42);
		requireSpan(local, 30, directory);
		require(u32(bytes, local) == 0x04034b50 && u16(bytes, local + 6) == flags &&
					u16(bytes, local + 8) == method,
				"Invalid local ZIP header.");
		size_t headerExtra = size_t(u16(bytes, local + 26)) + u16(bytes, local + 28);
		requireSpan(local + 30, headerExtra, directory);
		size_t data = local + 30 + headerExtra;
		requireSpan(data, packed, directory);
		require(u16(bytes, local + 26) == n &&
					std::equal(name.begin(), name.end(), bytes.begin() + local + 30),
				"ZIP filenames disagree.");
		target.resize(unpacked);
		if (method == 0)
		{
			require(packed == unpacked, "Invalid stored ZIP length.");
			std::copy_n(bytes.data() + data, unpacked, target.data());
		}
		else
		{
			z_stream stream{};
			stream.next_in = const_cast<Bytef *>(bytes.data() + data);
			stream.avail_in = packed;
			stream.next_out = target.data();
			stream.avail_out = unpacked;
			require(inflateInit2(&stream, -MAX_WBITS) == Z_OK, "Could not open ZIP decoder.");
			int result = inflate(&stream, Z_FINISH);
			bool valid =
				result == Z_STREAM_END && stream.total_out == unpacked && stream.total_in == packed;
			inflateEnd(&stream);
			require(valid, "Corrupt ZIP compression.");
		}
		require(crc32(0, target.data(), target.size()) == u32(bytes, pos + 16),
				"Music archive checksum failed.");
		pos += 46 + n + extra + comment;
	}
	require(pos == end, "Invalid ZIP directory size.");
	// Validate completeness for every set before installing the first.
	for (const auto &[name, trio] : sets)
		for (const auto &track : trio)
			require(!track.empty(), "An archive contains an incomplete set.");
	return sets;
}

std::vector<Installed> Library::importZip(const std::vector<unsigned char> &bytes)
{
	ImportJob job(*this, bytes);
	while (!job.finished())
		job.advance();
	if (!job.error().empty())
		throw std::runtime_error(job.error());
	return job.installed();
}
struct ImportJob::State
{
	Library &destination;
	fs::path temporary;
	Library staging;
	std::map<std::string, std::array<std::vector<unsigned char>, 3>> sets;
	std::map<std::string, std::array<std::vector<unsigned char>, 3>>::iterator next;
	std::vector<Installed> result;
	std::string problem;
	Opus decoder{nullptr, op_free};
	Installed current;
	unsigned mood = 0;
	std::int64_t decoded = 0;
	bool done = false, opened = false;
	State(Library &dest)
		: destination(dest),
		  temporary(dest.root.parent_path() /
					(".music-stage-" +
					 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
		  staging(temporary)
	{
	}
	~State()
	{
		decoder.reset();
		std::error_code ec;
		fs::remove_all(temporary, ec);
	}
};
ImportJob::ImportJob(Library &destination, std::array<std::vector<unsigned char>, 3> tracks)
	: state(std::make_unique<State>(destination))
{
	state->sets[""] = std::move(tracks);
	state->next = state->sets.begin();
}
ImportJob::ImportJob(Library &destination, const std::vector<unsigned char> &archive)
	: state(std::make_unique<State>(destination))
{
	state->sets = unpackZip(archive);
	state->next = state->sets.begin();
}
ImportJob::~ImportJob() = default;
bool ImportJob::finished() const
{
	return state->done;
}
const std::string &ImportJob::error() const
{
	return state->problem;
}
const std::vector<Installed> &ImportJob::installed() const
{
	return state->result;
}
void ImportJob::advance()
{
	auto &s = *state;
	if (s.done)
		return;
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4);
	try
	{
		while (s.next != s.sets.end())
		{
			if (!s.opened)
			{
				s.current = s.staging.installTracks(s.next->second, false);
				s.opened = true;
				s.mood = 0;
			}
			if (!s.decoder)
			{
				int error = 0;
				s.decoder.reset(
					op_open_file(s.staging.paths(s.current.directory)[s.mood].c_str(), &error));
				require(bool(s.decoder), "Could not validate music.");
				s.decoded = 0;
			}
			std::array<std::int16_t, Chunk * 2> pcm{};
			int n = op_read_stereo(s.decoder.get(), pcm.data(), pcm.size());
			require(n >= 0, "Corrupt music audio.");
			s.decoded += n;
			require(s.decoded <= s.current.info.frames, "Music duration exceeds metadata.");
			if (!n)
			{
				require(s.decoded == s.current.info.frames, "Music audio is truncated.");
				s.decoder.reset();
				if (++s.mood == 3)
				{
					s.result.push_back(s.current);
					++s.next;
					s.opened = false;
				}
			}
			if (std::chrono::steady_clock::now() >= deadline)
				return;
		}
		// Check every collision before making any set visible.
		std::set<std::string> names;
		for (const auto &entry : s.result)
		{
			require(names.insert(entry.directory).second, "Duplicate release identity in archive.");
			auto dest = s.destination.root / entry.directory;
			if (fs::exists(dest))
				for (unsigned i = 0; i < 3; ++i)
					require(readFile(dest / ("a" + std::to_string(i + 1) + ".opus")) ==
								readFile(s.staging.paths(entry.directory)[i]),
							"A different release already uses this identifier.");
		}
		std::vector<fs::path> moved;
		try
		{
			for (const auto &entry : s.result)
			{
				auto dest = s.destination.root / entry.directory;
				if (!fs::exists(dest))
				{
					fs::rename(s.staging.root / entry.directory, dest);
					moved.push_back(dest);
				}
			}
		}
		catch (...)
		{
			for (const auto &dest : moved)
			{
				std::error_code ec;
				fs::remove_all(dest, ec);
			}
			throw;
		}
		s.done = true;
	}
	catch (const std::exception &e)
	{
		s.problem = e.what();
		s.done = true;
		s.result.clear();
	}
}
void Library::remove(const std::string &directory)
{
	paths(directory);
	fs::remove_all(root / directory);
}
} // namespace Music
