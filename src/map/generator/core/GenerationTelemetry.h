// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

// Observations of one generation attempt, never inputs to generation decisions. Keep keys stable,
// namespaced by generator/helper, with units in the key where needed. Repeated records preserve
// operation order; subject is a local colony/feature index whose meaning belongs to the key.
class GenerationTelemetry
{
  public:
	using Value = std::variant<std::int64_t, double, bool, std::string>;
	struct Record
	{
		std::string key, kind;
		Value value;
		int subject = -1;
		bool operator==(const Record &) const = default;
	};
	static constexpr size_t kMaxRecords = 4096;
	static constexpr size_t kMaxKeyBytes = 128;
	static constexpr size_t kMaxTextBytes = 512;

	explicit GenerationTelemetry(bool enabled = true) : enabled_(enabled) {}
	template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
	void measure(std::string_view key, T value, int subject = -1)
	{
		if (!enabled_)
			return;
		if constexpr (std::is_same_v<T, bool>)
			append(key, "measurement", value, subject);
		else if constexpr (std::is_floating_point_v<T>)
		{
			if (!std::isfinite(double(value)))
				++invalidValues_;
			else
				append(key, "measurement", double(value), subject);
		}
		else
		{
			if constexpr (std::is_unsigned_v<T>)
				if (value > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
				{
					++invalidValues_;
					return;
				}
			append(key, "measurement", std::int64_t(value), subject);
		}
	}
	void choice(std::string_view key, std::string_view value, int subject = -1)
	{
		if (enabled_)
			append(key, "choice", std::string(value), subject);
	}
	void fallback(std::string_view key, std::string_view detail, int subject = -1)
	{
		if (enabled_)
			append(key, "fallback", std::string(detail), subject);
	}
	void error(std::string_view key, std::string_view detail)
	{
		if (enabled_)
			append(key, "error", std::string(detail), -1);
	}
	/// Records everything `from` recorded, in order, as though it had been recorded here: how a design
	/// cached from an earlier context reports what building it would have.
	void replay(const GenerationTelemetry &from)
	{
		for (const Record &r : from.records_)
			append(r.key, r.kind.c_str(), r.value, r.subject);
	}
	const std::vector<Record> &records() const { return records_; }
	size_t droppedRecords() const { return droppedRecords_; }
	size_t invalidValues() const { return invalidValues_; }
	bool enabled() const { return enabled_; }

  private:
	bool enabled_;
	std::vector<Record> records_;
	size_t droppedRecords_ = 0, invalidValues_ = 0;
	void append(std::string_view key, const char *kind, Value value, int subject)
	{
		if (!enabled_)
			return;
		const auto *text = std::get_if<std::string>(&value);
		// Reject oversized strings rather than splitting UTF-8 or silently colliding keys.
		if (records_.size() >= kMaxRecords || key.empty() || key.size() > kMaxKeyBytes ||
			(text && text->size() > kMaxTextBytes))
		{
			++droppedRecords_;
			return;
		}
		records_.push_back({std::string(key), kind, std::move(value), subject});
	}
};
