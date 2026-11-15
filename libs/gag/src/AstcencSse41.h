// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// astcenc built a second time for SSE4.1 inside a namespace, beside the
// portable SSE2 build x86-64 targets compile by default, as EtcpakSse41.h does
// for etcpak. Native x86 GCC and Clang builds compile AstcencSse41*.cpp with
// -msse4.1 and GLOB2_SSE41_COPY (CMakeLists.txt); elsewhere they are empty.
namespace GAGCore::AstcencSse41
{
using Codec = bool (*)(const unsigned char *in, int width, int height, unsigned char *out);
struct Codecs
{
	Codec encode = nullptr, decode = nullptr;
};
//! The SSE4.1 codec when this build has it and the CPU runs it; null otherwise.
Codecs available();
} // namespace GAGCore::AstcencSse41
