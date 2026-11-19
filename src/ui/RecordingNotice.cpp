// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/RecordingNotice.h"
#include <ApplicationHost.h>
#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace Glob2UI
{
namespace Recording = GAGCore::Recording;
namespace
{
std::string text(const char *key)
{
	return GAGCore::Toolkit::getStringTable()->getString(key);
}
std::string formatted(const char *key, const std::string &value)
{
	return GAGCore::FormattableString(text(key)).arg(value);
}
std::string fileName(const std::string &path)
{
	auto value = std::filesystem::u8path(path).filename().u8string();
	return std::string(value.begin(), value.end());
}
} // namespace

std::string recordingDisplayPath(const std::string &path)
{
	return recordingsDownload() ? fileName(path) : path;
}

std::string recordingLocationLine(const Recording::Status &status, const Recording::Completion &last)
{
	switch (status.state)
	{
	case Recording::State::Starting:
	case Recording::State::Recording:
		if (status.path.empty())
			return {};
		return recordingsDownload() ? formatted("[recording browser to %0]", fileName(status.path))
									: formatted("[recording to %0]", status.path);
	case Recording::State::Finalizing:
		return formatted("[recording saving %0]", recordingDisplayPath(status.path));
	default:
		break;
	}
	// A failure is reported by the controls' error line.
	if (last.serial == 0 || last.failed || last.outputs.empty())
		return {};
	return recordingsDownload() ? formatted("[recording downloaded %0]", fileName(last.outputs.front()))
								: formatted("[recording saved %0]", last.outputs.front());
}

std::string recordingFinishedMessage(const Recording::Completion &done)
{
	if (done.failed)
		return formatted("[recording failed %0]", done.error);
	if (done.outputs.empty())
		return {};
	return recordingsDownload() ? formatted("[recording downloaded %0]", fileName(done.outputs.front()))
								: formatted("[recording saved %0]", done.outputs.front());
}

std::string recordingFileDetails(const Recording::RecordingFileEntry &file)
{
	std::string details;
	auto add = [&](const std::string &part)
	{
		if (!details.empty())
			details += " \xC2\xB7 ";
		details += part;
	};
	if (file.modified > 0)
	{
		const std::time_t clock = std::time_t(file.modified);
		std::tm local{};
#if defined(_WIN32)
		localtime_s(&local, &clock);
#else
		localtime_r(&clock, &local);
#endif
		char stamp[32];
		std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &local);
		add(stamp);
	}
	if (file.durationUs > 0)
	{
		const auto seconds = (file.durationUs + 500000) / 1000000;
		char value[32];
		if (seconds >= 3600)
			std::snprintf(value, sizeof value, "%lld:%02lld:%02lld", (long long)(seconds / 3600),
						  (long long)(seconds / 60 % 60), (long long)(seconds % 60));
		else
			std::snprintf(value, sizeof value, "%lld:%02lld", (long long)(seconds / 60), (long long)(seconds % 60));
		add(value);
	}
	if (file.bytes > 0)
	{
		char value[32];
		const double megabytes = double(file.bytes) / (1024.0 * 1024.0);
		if (megabytes >= 1024)
			std::snprintf(value, sizeof value, "%.1f GB", megabytes / 1024);
		else if (megabytes >= 0.1)
			std::snprintf(value, sizeof value, "%.1f MB", megabytes);
		else
			std::snprintf(value, sizeof value, "%llu KB", (unsigned long long)((file.bytes + 1023) / 1024));
		add(value);
	}
	return details;
}

std::optional<Recording::Completion> takeRecordingEnding(std::optional<std::uint64_t> &seen,
														 const Recording::Completion &current)
{
	if (!seen)
	{
		seen = current.serial;
		return std::nullopt;
	}
	if (current.serial == *seen)
		return std::nullopt;
	seen = current.serial;
	return current;
}

void deliverFinishedRecordings()
{
	if (!recordingsDownload())
		return;
	static std::optional<std::uint64_t> seen;
	if (auto done = takeRecordingEnding(seen, Recording::recorder().completion()); done && !done->failed)
		for (const auto &output : done->outputs)
			GAGCore::ApplicationHost::exportFilePath(output);
}
} // namespace Glob2UI
