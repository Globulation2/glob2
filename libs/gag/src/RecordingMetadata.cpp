// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecordingMetadata.h"
#include <array>
#include <cerrno>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif

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

namespace
{
// Android refuses hard links in app data for apps targeting API 28 or later, and
// some filesystems never offer them. Those refusals select the rename fallback;
// any other error (an existing name, a full disk) still fails the publication.
bool hardLinksRefused(const std::error_code &error)
{
	return error == std::errc::permission_denied ||
		   error == std::errc::operation_not_permitted ||
		   error == std::errc::operation_not_supported ||
		   error == std::errc::function_not_supported;
}

// Atomically moves source to destination, failing if destination exists.
void renameNoReplace(const std::filesystem::path &source,
					 const std::filesystem::path &destination)
{
#if defined(__linux__) && defined(SYS_renameat2)
	if (::syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(),
				  1u /* RENAME_NOREPLACE */) == 0)
		return;
	if (errno != ENOSYS && errno != EINVAL)
		throw std::filesystem::filesystem_error(
			"rename without replacing", source, destination,
			std::error_code(errno, std::generic_category()));
#elif defined(__APPLE__)
	if (::renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL) == 0)
		return;
	if (errno != ENOTSUP && errno != EINVAL)
		throw std::filesystem::filesystem_error(
			"rename without replacing", source, destination,
			std::error_code(errno, std::generic_category()));
#endif
	// No atomic no-replace rename here: the reserved work directory already owns
	// this output name, so only a foreign writer racing for it can be replaced.
	if (std::filesystem::exists(destination))
		throw std::filesystem::filesystem_error(
			"rename without replacing", source, destination,
			std::make_error_code(std::errc::file_exists));
	std::filesystem::rename(source, destination);
}
} // namespace

void publishCompletedRecording(const std::filesystem::path &work,
							   const std::filesystem::path &video, bool tryHardLinks)
{
	using Pair = std::pair<std::filesystem::path, std::filesystem::path>;
	const std::array<Pair, 3> files{
		{{work / "manifest.json", std::filesystem::path(video).concat(".json")},
		 {work / "events.jsonl", std::filesystem::path(video).concat(".events.jsonl")},
		 {work / ("final" + video.extension().string()), video}}};
	enum class Published { Linked, Copied, Moved };
	std::array<Published, 3> how{};
	std::size_t published = 0;
	try
	{
		// The reservation directory is beside the final output on the same filesystem.
		// Hard links publish a complete inode atomically and fail if the name exists.
		// Where links are refused, metadata is copied without replacing and the video,
		// the completion point, is renamed atomically without replacing.
		bool linksRefused = !tryHardLinks;
		for (const auto &[source, destination] : files)
		{
			const bool isVideo = published + 1 == files.size();
			if (!linksRefused)
			{
				std::error_code error;
				std::filesystem::create_hard_link(source, destination, error);
				if (!error)
				{
					how[published++] = Published::Linked;
					continue;
				}
				if (!hardLinksRefused(error))
					throw std::filesystem::filesystem_error("create_hard_link", source,
															destination, error);
				linksRefused = true;
			}
			if (isVideo)
			{
				renameNoReplace(source, destination);
				how[published++] = Published::Moved;
			}
			else
			{
				std::filesystem::copy_file(source, destination,
										   std::filesystem::copy_options::none);
				how[published++] = Published::Copied;
			}
		}
	}
	catch (...)
	{
		while (published)
		{
			const auto &[source, destination] = files[--published];
			std::error_code error;
			if (how[published] == Published::Moved)
				std::filesystem::rename(destination, source, error);
			else if (how[published] == Published::Copied ||
					 (std::filesystem::equivalent(source, destination, error) && !error))
				std::filesystem::remove(destination, error);
		}
		throw;
	}
}
} // namespace GAGCore::Recording::Detail
