// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <utility>
#include <vector>
#include <cstddef>
namespace Hive
{
enum class ReportSource
{
	Remote, StandingOrderPaused, ConnectionChanged, FreshInstruction,
	ConfirmationFailed, PreviousSending, OrderReceived, OrderNotConfirmed,
	UpdateFailed, Stopped, StopFailed
};
struct Report
{
	std::string text;
	ReportSource source = ReportSource::Remote;
	std::string parameter;
	Report(std::string text, ReportSource source = ReportSource::Remote, std::string parameter = {})
		: text(std::move(text)), source(source), parameter(std::move(parameter)) {}
	Report(const char *text) : text(text) {}
};
// Companion presentation metadata. The public raw vector remains unchanged.
// Only explicitly tagged internal appends acquire local provenance. Any mismatch
// in container identity, length or the retained per-entry snapshot is untagged.
class ReportHistory
{
	std::vector<Report> metadata;
	const std::string *boundData = nullptr;
  public:
	static constexpr std::size_t Limit = 100;
	void append(std::vector<std::string> &raw, std::string text,
		ReportSource source = ReportSource::Remote, std::string parameter = {})
	{
		if (boundData != raw.data() || metadata.size() != raw.size())
		{
			metadata.clear();
			for (const auto &value : raw) metadata.emplace_back(value);
		}
		for (std::size_t i = 0; i < raw.size(); ++i)
			if (metadata[i].text != raw[i]) metadata[i] = Report(raw[i]);
		raw.push_back(text);
		metadata.emplace_back(std::move(text), source, std::move(parameter));
		if (raw.size() > Limit)
		{
			const auto count = raw.size() - Limit;
			raw.erase(raw.begin(), raw.begin() + count);
			metadata.erase(metadata.begin(), metadata.begin() + count);
		}
		boundData = raw.data();
	}
	Report at(const std::vector<std::string> &raw, std::size_t index) const
	{
		if (index >= raw.size()) return Report("");
		if (boundData != raw.data() || metadata.size() != raw.size() || metadata[index].text != raw[index])
			return Report(raw[index]);
		return metadata[index];
	}
};
} // namespace Hive
