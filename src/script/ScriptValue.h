// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
namespace Script
{
struct Value
{
 enum Kind { Null, Boolean, Number, String, Array, Object } kind = Null;
 double number = 0;
 std::string text;
 std::vector<Value> items;
 std::vector<std::pair<std::string, Value>> fields;
 Value() = default;
 Value(bool v): kind(Boolean), number(v) {}
 Value(double v): kind(Number), number(v) {}
 Value(int v): Value(double(v)) {}
 Value(unsigned v): Value(double(v)) {}
 Value(const std::string& v): kind(String), text(v) {}
 Value(const char* v): Value(std::string(v)) {}
 static Value array() { Value v; v.kind=Array; return v; }
 static Value object() { Value v; v.kind=Object; return v; }
 Value& set(const std::string& key, Value value);
 const Value& get(const std::string& key) const;
 int integer(const std::string& key, int minimum, int maximum) const;
 std::string string(const std::string& key) const;
 std::string encode() const;
 static Value decode(const std::string& bytes);
};
using QueryBudget=std::function<void(std::size_t work,std::size_t nativeBytes)>;
// Fixed profile weights cover native container overhead across supported ABIs.
constexpr std::size_t NativeValueCost=128,NativeFieldCost=192;
constexpr std::size_t NativeDataLimit=32*1024*1024;
constexpr unsigned ProfileVersion = 1;
constexpr std::size_t SourceLimit = 128 * 1024;
constexpr std::size_t StateLimit = 1024 * 1024;
constexpr unsigned DepthLimit = 256;
constexpr std::uint64_t FuelLimit = 1000000;
std::string config(const std::string& source);
std::string sourceFromConfig(const std::string& config);
}
