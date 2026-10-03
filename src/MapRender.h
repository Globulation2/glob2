// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct Scene;
namespace MapRender
{
inline constexpr int DefaultPixels = 4096;
inline constexpr int MaximumPixels = 8192;
inline constexpr size_t MaximumFieldValues = 16 * 1024 * 1024;
struct Field
{
	int width = 0, height = 0;
	std::vector<std::int64_t> values;
	int red = 0, green = 192, blue = 255;
};
Field readField(const std::string& path);
void validate(const Field& field, int width, int height);
unsigned char alpha(std::int64_t value, std::int64_t maximum);
void toPng(const Scene& scene, const std::string& path, int maximumPixels = DefaultPixels, const Field* field = nullptr);
}
