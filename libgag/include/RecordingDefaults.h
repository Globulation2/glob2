// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace GAGCore::Recording
{
#if defined(GLOB2_RECORDING_VP9)
inline constexpr bool WebM = true;
inline constexpr const char *VideoCodec = "vp9", *SoftwareEncoder = "libvpx-vp9";
#elif defined(GLOB2_RECORDING_AV1)
inline constexpr bool WebM = true;
inline constexpr const char *VideoCodec = "av1", *SoftwareEncoder = "libaom-av1";
#else
inline constexpr bool WebM = false;
inline constexpr const char *VideoCodec = "h264", *SoftwareEncoder = "libx264";
#endif
inline constexpr const char *Extension = WebM ? ".webm" : ".mp4";
inline constexpr const char *AudioCodec = WebM ? "opus" : "aac";
inline constexpr int MaxCrf = WebM ? 63 : 51;
inline constexpr int DefaultFps = 30;
inline constexpr int DefaultCrf = 23;
inline constexpr unsigned DefaultChapterTicks = 10000;
} // namespace GAGCore::Recording
