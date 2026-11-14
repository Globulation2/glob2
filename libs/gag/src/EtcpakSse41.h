// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

// etcpak's block encoders built for SSE4.1, beside the portable build every
// x86-64 target compiles by default. EtcpakSse41.cpp is compiled with -msse4.1
// where the toolchain allows it; callers check the CPU before using these.
namespace GAGCore::EtcpakSse41
{
using Encoder = void (*)(const std::uint32_t *source, std::uint64_t *blocks, std::uint32_t count, std::size_t width);
struct Encoders
{
	Encoder bc3 = nullptr, etc2Rgba = nullptr;
};
//! The SSE4.1 encoders when this build has them and the CPU runs them; both null otherwise.
Encoders available();
} // namespace GAGCore::EtcpakSse41
