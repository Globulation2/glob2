// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecordingMetadata.h"
#include <array>
#include <iomanip>
#include <sstream>
#include <utility>

namespace GAGCore::Recording::Detail
{
std::string json(const std::string &value)
{
	std::ostringstream out;
	out << '"';
	for (unsigned char c : value)
	{
		if (c == '"' || c == '\\')
			out << '\\' << c;
		else if (c < 32)
			out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c)
				<< std::dec;
		else
			out << c;
	}
	out << '"';
	return out.str();
}
std::string ffescape(const std::string &value)
{
	std::string out;
	for (char c : value)
	{
		if (c == '\\' || c == '=' || c == ';' || c == '#' || c == '\n')
			out += '\\';
		out += c;
	}
	return out;
}

void publishCompletedRecording(const std::filesystem::path &work,
							   const std::filesystem::path &video)
{
	using Pair = std::pair<std::filesystem::path, std::filesystem::path>;
	const std::array<Pair, 3> files{
		{{work / "manifest.json", std::filesystem::path(video).concat(".json")},
		 {work / "events.jsonl", std::filesystem::path(video).concat(".events.jsonl")},
		 {work / "final.mp4", video}}};
	std::size_t published = 0;
	try
	{
		// The reservation directory is beside the final output on the same filesystem.
		// Hard links publish a complete inode atomically and fail if the name exists.
		// Unsupported filesystems fail safely, retaining the finished intermediate.
		for (const auto &[source, destination] : files)
		{
			std::filesystem::create_hard_link(source, destination);
			++published;
		}
	}
	catch (...)
	{
		while (published)
		{
			const auto &[source, destination] = files[--published];
			std::error_code error;
			if (std::filesystem::equivalent(source, destination, error) && !error)
				std::filesystem::remove(destination, error);
		}
		throw;
	}
}
} // namespace GAGCore::Recording::Detail
