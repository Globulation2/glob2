// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace GAGCore::Recording::Detail
{
// No FFmpeg objects cross this boundary. Browser encoders may complete later.
struct VideoConfiguration
{
	int width = 0, height = 0, fps = 30, crf = 23;
	bool software = false;
};
struct VideoPacket
{
	std::vector<unsigned char> bytes;
	std::int64_t ptsUs = 0, dtsUs = 0, durationUs = 0;
	bool key = false;
};
struct VideoDescription
{
	int width = 0, height = 0;
	std::vector<unsigned char> extraData;
	std::string encoder, fallbackReason;
};
class VideoEncoder
{
  public:
	virtual ~VideoEncoder() = default;
	virtual const VideoDescription &description() const = 0;
	virtual void submit(const unsigned char *rgba, int width, int height,
						std::int64_t ptsUs) = 0;
	virtual bool receive(VideoPacket &packet) = 0;
	virtual bool ready() const { return true; }
	// May return false until asynchronous packets have been delivered.
	virtual bool finish() = 0;
};
std::unique_ptr<VideoEncoder> createVideoEncoder(const VideoConfiguration &configuration, const std::string &reason = {});

// Seekable streaming storage: native files and worker-owned OPFS handles.
class RecordingFile
{
  public:
	virtual ~RecordingFile() = default;
	virtual int read(unsigned char *bytes, int size) = 0;
	virtual int write(const unsigned char *bytes, int size) = 0;
	virtual std::int64_t seek(std::int64_t offset, int whence) = 0;
	virtual void flush() = 0;
};
using OpenRecordingFile = std::function<std::unique_ptr<RecordingFile>(const std::string &, bool)>;
std::unique_ptr<RecordingFile> openNativeRecordingFile(const std::string &path, bool write);
struct MediaChapter
{
	std::int64_t startUs, endUs;
	std::string title;
};
class MediaWriter
{
  public:
	MediaWriter(const std::string &path, const VideoDescription &description,
				OpenRecordingFile open = openNativeRecordingFile);
	~MediaWriter();
	MediaWriter(const MediaWriter &) = delete;
	MediaWriter &operator=(const MediaWriter &) = delete;
	void video(const VideoPacket &packet);
	// Input timestamps and sample counts are stereo frames, at 48000 Hz.
	void audio(const std::int16_t *pcm, std::size_t frames, std::int64_t startFrame);
	void silenceThrough(std::int64_t frame);
	void finish(std::int64_t durationUs);
	void checkpoint();
	static void finalize(const std::string &source, const std::string &destination,
						 const std::vector<MediaChapter> &chapters,
						 OpenRecordingFile open = openNativeRecordingFile);
	static std::int64_t duration(const std::string &source, OpenRecordingFile open = openNativeRecordingFile);
  private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace GAGCore::Recording::Detail
