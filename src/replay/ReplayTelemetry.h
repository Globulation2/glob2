// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <string_view>
#include <tuple>
namespace GAGCore { class InputStream; class OutputStream; }
class Game;
namespace ReplayTelemetry
{
// Diagnostic data is bounded independently of the gameplay order stream.
class Stream
{
	using MetadataView = std::tuple<std::string_view, std::string_view, std::string_view>;
	struct Metadata
	{
		std::string name, unit, meaning;
		MetadataView view() const { return {name, unit, meaning}; }
	};
	using Schema = std::vector<Metadata>;
	using SchemaView = std::vector<MetadataView>;
	using SchemaPtr = std::shared_ptr<const Schema>;
	struct SchemaLess
	{
		using is_transparent = void;
		bool operator()(const SchemaPtr &, const SchemaPtr &) const;
		bool operator()(const SchemaPtr &, const SchemaView &) const;
		bool operator()(const SchemaView &, const SchemaPtr &) const;
	};
	struct StoredValue
	{
		std::string value;
		Uint32 updated = 0;
		bool operator==(const StoredValue &) const = default;
	};
	struct StoredRow
	{
		int team = 0, player = 0;
		std::string name;
		bool available = false;
		SchemaPtr schema;
		std::vector<StoredValue> values;
		// Accepted rows use canonical immutable schemas: pointer equality is
		// descriptor equality. retainSchemas must run before a row enters current.
		bool operator==(const StoredRow &) const = default;
	};
	struct StoredFrame
	{
		Uint32 step = 0;
		std::vector<StoredRow> changes;
	};
	// One immutable ordered descriptor list per distinct schema. Rows still
	// retain every value and update stamp; this is not a delta or wire format.
	std::set<SchemaPtr, SchemaLess> schemas;
	std::map<int, StoredRow> current;
	std::vector<StoredFrame> frames;
	SchemaPtr schemaFor(const SchemaView &, const SchemaPtr *hint = nullptr) const;
	void retainSchemas(StoredFrame &, std::map<int, StoredRow> *latest = nullptr);
	size_t bytes = 0, cursor = 0;
	bool present = false, truncated = false;
	Uint32 until = 0;

  public:
	void capture(const Game &, Uint32 step);
	void write(GAGCore::OutputStream *) const;
	void read(GAGCore::InputStream *);
	void apply(Game &, Uint32 step);
	bool available() const { return present; }
};
} // namespace ReplayTelemetry
