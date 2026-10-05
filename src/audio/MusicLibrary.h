// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <map>
namespace Music
{
struct Metadata
{
	std::string id, origin, title, artist, description, license, credits, sources;
	bool aiGenerated = false;
	std::int64_t frames = 0;
	std::vector<unsigned char> cover;
	std::array<std::vector<float>, 3> waveforms;
};
Metadata metadata(const std::filesystem::path &path, unsigned expectedMood,
				  bool validateAudio = true);
struct Installed
{
	std::string directory;
	Metadata info;
};
class ImportJob;
class Library
{
  public:
	explicit Library(std::filesystem::path userRoot);
	std::vector<Installed> list() const;
	std::vector<Installed> importZip(const std::vector<unsigned char> &bytes);
	Installed importTracks(const std::array<std::vector<unsigned char>, 3> &bytes);
	void remove(const std::string &directory);
	std::array<std::string, 3> paths(const std::string &directory) const;

  private:
	friend class ImportJob;
	Installed installTracks(const std::array<std::vector<unsigned char>, 3> &bytes,
							bool verifyAudio);
	std::filesystem::path root;
};
// Host-polled validation: at most a few milliseconds of decoding per frame.
class ImportJob
{
  public:
	ImportJob(Library &destination, std::array<std::vector<unsigned char>, 3> tracks);
	ImportJob(Library &destination, const std::vector<unsigned char> &archive);
	~ImportJob();
	void advance();
	bool finished() const;
	const std::string &error() const;
	const std::vector<Installed> &installed() const;

  private:
	struct State;
	std::unique_ptr<State> state;
};
} // namespace Music
