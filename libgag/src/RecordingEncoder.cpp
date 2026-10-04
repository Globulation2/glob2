// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecordingEncoder.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace GAGCore::Recording::Detail
{
namespace
{
void check(int result, const char *operation)
{
	if (result >= 0) return;
	char error[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(result, error, sizeof(error));
	throw std::runtime_error(std::string(operation) + ": " + error);
}
struct Dictionary
{
	AVDictionary *value = nullptr;
	~Dictionary() { av_dict_free(&value); }
};
struct Release
{
	void operator()(AVCodecContext *p) const { avcodec_free_context(&p); }
	void operator()(AVFrame *p) const { av_frame_free(&p); }
	void operator()(AVPacket *p) const { av_packet_free(&p); }
	void operator()(AVBufferRef *p) const { av_buffer_unref(&p); }
	void operator()(SwsContext *p) const { sws_freeContext(p); }
	void operator()(SwrContext *p) const { swr_free(&p); }
};
template<class T> using Owned = std::unique_ptr<T, Release>;
Owned<AVFrame> frame() { auto p = Owned<AVFrame>(av_frame_alloc()); if (!p) throw std::bad_alloc(); return p; }
Owned<AVPacket> packet() { auto p = Owned<AVPacket>(av_packet_alloc()); if (!p) throw std::bad_alloc(); return p; }
Owned<AVCodecContext> codec(const AVCodec *encoder)
{
	if (!encoder) throw std::runtime_error("Required recording encoder is unavailable");
	auto p = Owned<AVCodecContext>(avcodec_alloc_context3(encoder));
	if (!p) throw std::bad_alloc();
	return p;
}
class NativeFile final : public RecordingFile
{
	std::FILE *file;
  public:
	NativeFile(const std::string &path, bool write)
	{
#ifdef _WIN32
		// The legacy MSVCRT used by MinGW does not accept fopen's C11 x mode.
		// Reserve the index atomically before converting its descriptor to a stream.
		if (write && path.ends_with(".session.json"))
		{
			int descriptor = _wopen(std::filesystem::u8path(path).c_str(),
			                       _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
			file = descriptor < 0 ? nullptr : _fdopen(descriptor, "w+b");
			if (descriptor >= 0 && !file) _close(descriptor);
		}
		else
			file = _wfopen(std::filesystem::u8path(path).c_str(), write ? L"w+b" : L"rb");
#else
		file = std::fopen(path.c_str(), write ? (path.ends_with(".session.json") ? "w+bx" : "w+b") : "rb");
#endif
		if (!file) throw std::runtime_error("Cannot open recording file: " + path);
	}
	~NativeFile() override { std::fclose(file); }
	int read(unsigned char *bytes, int size) override
	{
		auto n = std::fread(bytes, 1, size, file);
		return n ? int(n) : std::ferror(file) ? AVERROR(EIO) : AVERROR_EOF;
	}
	int write(const unsigned char *bytes, int size) override
	{
		return std::fwrite(bytes, 1, size, file) == std::size_t(size) ? size : AVERROR(EIO);
	}
	std::int64_t seek(std::int64_t offset, int whence) override
	{
#ifdef _WIN32
		auto tell = [&] { return _ftelli64(file); };
		auto move = [&](std::int64_t n, int w) { return _fseeki64(file, n, w); };
#else
		auto tell = [&] { return ftello(file); };
		auto move = [&](std::int64_t n, int w) { return fseeko(file, n, w); };
#endif
		if (whence == AVSEEK_SIZE)
		{
			auto position = tell();
			if (position < 0 || move(0, SEEK_END)) return AVERROR(EIO);
			auto length = tell();
			if (move(position, SEEK_SET)) return AVERROR(EIO);
			return length;
		}
		return move(offset, whence) ? AVERROR(EIO) : tell();
	}
	void flush() override { if (std::fflush(file)) throw std::runtime_error("Cannot flush recording file"); }
};
class IO
{
	std::unique_ptr<RecordingFile> file;
  public:
	AVIOContext *context = nullptr;
	IO(std::unique_ptr<RecordingFile> file, bool write) : file(std::move(file))
	{
		auto buffer = static_cast<unsigned char *>(av_malloc(32768));
		if (!buffer) throw std::bad_alloc();
		context = avio_alloc_context(buffer, 32768, write, this,
			[](void *p, unsigned char *b, int n) { try { return static_cast<IO *>(p)->file->read(b,n); } catch (...) { return AVERROR(EIO); } },
			[](void *p, const unsigned char *b, int n) { try { return static_cast<IO *>(p)->file->write(b,n); } catch (...) { return AVERROR(EIO); } },
			[](void *p, std::int64_t n, int w) -> std::int64_t { try { return static_cast<IO *>(p)->file->seek(n,w & ~AVSEEK_FORCE); } catch (...) { return AVERROR(EIO); } });
		if (!context) { av_free(buffer); throw std::bad_alloc(); }
	}
	~IO()
	{
		if (context)
		{
			if (context->write_flag) { avio_flush(context); try { file->flush(); } catch (...) {} }
			av_freep(&context->buffer); avio_context_free(&context);
		}
	}
	void flush() { avio_flush(context); check(context->error, "Recording storage"); file->flush(); }
};
struct Output
{
	AVFormatContext *context = nullptr;
	Output() { check(avformat_alloc_output_context2(&context, nullptr, "mp4", nullptr), "Create MP4 writer"); }
	~Output() { avformat_free_context(context); }
};
struct Input
{
	AVFormatContext *context = avformat_alloc_context();
	Input() { if (!context) throw std::bad_alloc(); }
	~Input() { avformat_close_input(&context); }
};
class FFmpegVideoEncoder final : public VideoEncoder
{
	VideoConfiguration configuration;
	VideoDescription info;
	Owned<AVCodecContext> encoder;
	Owned<AVFrame> pixels = frame(), uploaded = frame();
	Owned<AVPacket> encoded = packet();
	Owned<SwsContext> converter;
	std::vector<unsigned char> padded;
	bool hardwareFrames = false;
  public:
	FFmpegVideoEncoder(const VideoConfiguration &configuration, const char *name, const std::string &devicePath = {}) : configuration(configuration)
	{
		encoder = codec(avcodec_find_encoder_by_name(name));
		auto &c = *encoder;
		c.width = configuration.width; c.height = configuration.height;
		c.time_base = {1, configuration.fps}; c.framerate = {configuration.fps, 1};
		c.gop_size = configuration.fps * 2; c.max_b_frames = 0; c.thread_count = 1;
		c.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		c.color_primaries = AVCOL_PRI_BT709; c.color_trc = AVCOL_TRC_BT709;
		c.colorspace = AVCOL_SPC_BT709; c.color_range = AVCOL_RANGE_MPEG;
		const bool software = std::strcmp(name, "libx264") == 0;
		c.pix_fmt = software ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;
		Dictionary options;
		if (software)
		{
			av_dict_set(&options.value, "preset", "ultrafast", 0);
			av_dict_set(&options.value, "tune", "zerolatency", 0);
			av_dict_set_int(&options.value, "crf", configuration.crf, 0);
		}
		else
		{
			c.bit_rate = std::max<std::int64_t>(1000000, std::int64_t(c.width) * c.height * configuration.fps * 12 / 100);
			c.rc_max_rate = c.bit_rate * 2; c.rc_buffer_size = int(std::min<std::int64_t>(INT_MAX, c.bit_rate * 2));
			if (std::strcmp(name, "h264_videotoolbox") == 0)
			{
				av_dict_set(&options.value, "allow_sw", "0", 0);
				av_dict_set(&options.value, "realtime", "1", 0);
			}
			if (std::strcmp(name, "h264_mf") == 0)
			{
				av_dict_set(&options.value, "hw_encoding", "1", 0);
				av_dict_set(&options.value, "rate_control", "ld_vbr", 0);
				av_dict_set(&options.value, "scenario", "camera_record", 0);
			}
			if (std::strcmp(name, "h264_mediacodec") == 0)
			{
				av_dict_set(&options.value, "bitrate_mode", "vbr", 0);
				av_dict_set(&options.value, "ndk_codec", "1", 0);
			}
			if (std::strcmp(name, "h264_nvenc") == 0)
			{
				av_dict_set(&options.value, "preset", "p1", 0); av_dict_set(&options.value, "tune", "ull", 0);
				av_dict_set(&options.value, "rc", "vbr", 0); av_dict_set(&options.value, "zerolatency", "1", 0);
			}
			if (std::strcmp(name, "h264_vaapi") == 0)
			{
				av_dict_set(&options.value, "rc_mode", "VBR", 0);
				AVBufferRef *device = nullptr;
				check(av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VAAPI, devicePath.empty() ? nullptr : devicePath.c_str(), nullptr, 0), "Open VAAPI device");
				Owned<AVBufferRef> ownedDevice(device);
				Owned<AVBufferRef> pool(av_hwframe_ctx_alloc(device));
				if (!pool) throw std::bad_alloc();
				auto frames = reinterpret_cast<AVHWFramesContext *>(pool->data);
				frames->format = AV_PIX_FMT_VAAPI; frames->sw_format = AV_PIX_FMT_NV12;
				frames->width = c.width; frames->height = c.height; frames->initial_pool_size = 4;
				check(av_hwframe_ctx_init(pool.get()), "Create VAAPI frames");
				c.hw_frames_ctx = pool.release(); c.pix_fmt = AV_PIX_FMT_VAAPI;
				hardwareFrames = true;
			}
		}
		int result = avcodec_open2(encoder.get(), encoder->codec, &options.value);

		check(result, "Open H.264 encoder");
		pixels->format = hardwareFrames ? AV_PIX_FMT_NV12 : c.pix_fmt;
		pixels->width = c.width; pixels->height = c.height;
		check(av_frame_get_buffer(pixels.get(), 32), "Allocate video frame");
		converter.reset(sws_getContext(c.width, c.height, AV_PIX_FMT_RGBA, c.width, c.height,
			static_cast<AVPixelFormat>(pixels->format), SWS_FAST_BILINEAR, nullptr, nullptr, nullptr));
		if (!converter) throw std::runtime_error("Cannot create recording color converter");
		check(sws_setColorspaceDetails(converter.get(), sws_getCoefficients(SWS_CS_ITU709), 1,
			sws_getCoefficients(SWS_CS_ITU709), 0, 0, 1<<16, 1<<16), "Set recording colors");
		info.width = c.width; info.height = c.height; info.encoder = name;
		if (c.extradata_size) info.extraData.assign(c.extradata, c.extradata + c.extradata_size);
	}
	const VideoDescription &description() const override { return info; }
	void fallbackReason(std::string reason) { info.fallbackReason = std::move(reason); }
	void submit(const unsigned char *rgba, int width, int height, std::int64_t ptsUs) override
	{
		if (width <= 0 || height <= 0 || width > info.width || height > info.height ||
			info.width - width > 1 || info.height - height > 1)
			throw std::runtime_error("Recording resolution changed without a new segment");
		check(av_frame_make_writable(pixels.get()), "Reuse video frame");
		const unsigned char *source = rgba;
		if (width != info.width || height != info.height)
		{
			padded.resize(std::size_t(info.width) * info.height * 4);
			std::fill(padded.begin(), padded.end(), 0);
			for (std::size_t n = 3; n < padded.size(); n += 4) padded[n] = 255;
			for (int y = 0; y < height; ++y) std::memcpy(padded.data() + std::size_t(y)*info.width*4, rgba + std::size_t(y)*width*4, std::size_t(width)*4);
			source = padded.data();
		}
		const unsigned char *data[] = {source}; int strides[] = {info.width * 4};
		check(sws_scale(converter.get(), data, strides, 0, info.height, pixels->data, pixels->linesize), "Convert recording frame");
		pixels->pts = av_rescale_q(ptsUs, {1,1000000}, encoder->time_base);
		pixels->color_primaries = encoder->color_primaries; pixels->color_trc = encoder->color_trc;
		pixels->colorspace = encoder->colorspace; pixels->color_range = encoder->color_range;
		AVFrame *send = pixels.get();
		if (hardwareFrames)
		{
			av_frame_unref(uploaded.get());
			check(av_hwframe_get_buffer(encoder->hw_frames_ctx, uploaded.get(), 0), "Allocate hardware frame");
			check(av_hwframe_transfer_data(uploaded.get(), pixels.get(), 0), "Upload recording frame");
			check(av_frame_copy_props(uploaded.get(), pixels.get()), "Copy recording frame properties");
			send = uploaded.get();
		}
		check(avcodec_send_frame(encoder.get(), send), "Encode video");
	}
	bool receive(VideoPacket &out) override
	{
		int result = avcodec_receive_packet(encoder.get(), encoded.get());
		if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return false;
		check(result, "Receive video packet");
		out.bytes.assign(encoded->data, encoded->data + encoded->size);
		out.ptsUs = av_rescale_q(encoded->pts, encoder->time_base, {1,1000000});
		out.dtsUs = av_rescale_q(encoded->dts, encoder->time_base, {1,1000000});
		out.durationUs = av_rescale_q(std::max<std::int64_t>(1, encoded->duration), encoder->time_base, {1,1000000});
		out.key = encoded->flags & AV_PKT_FLAG_KEY;
		av_packet_unref(encoded.get());
		return true;
	}
	bool finish() override { check(avcodec_send_frame(encoder.get(), nullptr), "Flush video encoder"); return true; }
};
} // namespace

std::unique_ptr<RecordingFile> openNativeRecordingFile(const std::string &path, bool write)
{
	return std::make_unique<NativeFile>(path, write);
}
std::unique_ptr<VideoEncoder> createVideoEncoder(const VideoConfiguration &configuration, const std::string &reason)
{
	std::vector<const char *> candidates;
	if (!configuration.software)
	{
#if defined(__APPLE__)
		candidates = {"h264_videotoolbox"};
#elif defined(__ANDROID__)
		candidates = {"h264_mediacodec"};
#elif defined(_WIN32)
		candidates = {"h264_mf", "h264_nvenc"};
#elif defined(__linux__)
		candidates = {"h264_vaapi", "h264_nvenc"};
#endif
	}
	std::string failures = reason;
	for (auto name : candidates)
	{
		std::vector<std::string> devices{std::string()};
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
		if (std::strcmp(name,"h264_vaapi") == 0 && avcodec_find_encoder_by_name(name))
		{
			std::error_code error;
			std::vector<std::string> nodes;
			for (std::filesystem::directory_iterator entry("/dev/dri",error), end;
				!error && entry != end; entry.increment(error))
				if (entry->path().filename().string().starts_with("renderD")) nodes.push_back(entry->path().string());
			if (!nodes.empty()) { std::sort(nodes.begin(),nodes.end()); devices = std::move(nodes); }
		}
#endif
		for (const auto &device : devices)
		{
			try { auto encoder = std::make_unique<FFmpegVideoEncoder>(configuration, name, device); encoder->fallbackReason(failures); return encoder; }
			catch (const std::exception &e) { if (!failures.empty()) failures += "; "; failures += std::string(name) + (device.empty() ? "" : " ("+device+")") + ": " + e.what(); }
		}
	}
	auto software = std::make_unique<FFmpegVideoEncoder>(configuration, "libx264");
	software->fallbackReason(std::move(failures));
	return software;
}

struct MediaWriter::Impl
{
	IO storage;
	Output output;
	Owned<AVCodecContext> audioEncoder = codec(avcodec_find_encoder_by_name("aac"));
	Owned<AVFrame> samples = frame();
	Owned<AVPacket> encoded = packet();
	Owned<SwrContext> converter;
	AVStream *videoStream = nullptr, *audioStream = nullptr;
	std::vector<std::int16_t> pending;
	std::int64_t written = 0, audioPts = 0;
	bool finished = false;
	Impl(const std::string &path, const VideoDescription &description, OpenRecordingFile open) : storage(open(path,true),true)
	{
		auto &c = *audioEncoder;
		c.sample_rate = 44100; c.sample_fmt = AV_SAMPLE_FMT_FLTP; c.time_base = {1,44100}; c.bit_rate = 192000;
		av_channel_layout_default(&c.ch_layout, 2); c.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		check(avcodec_open2(audioEncoder.get(), c.codec, nullptr), "Open AAC encoder");
		videoStream = avformat_new_stream(output.context, nullptr); audioStream = avformat_new_stream(output.context, nullptr);
		if (!videoStream || !audioStream) throw std::bad_alloc();
		videoStream->time_base = {1,3000}; audioStream->time_base = c.time_base;
		auto &v = *videoStream->codecpar;
		v.codec_type = AVMEDIA_TYPE_VIDEO; v.codec_id = AV_CODEC_ID_H264;
		v.width = description.width; v.height = description.height; v.format = AV_PIX_FMT_YUV420P;
		v.color_primaries = AVCOL_PRI_BT709; v.color_trc = AVCOL_TRC_BT709; v.color_space = AVCOL_SPC_BT709; v.color_range = AVCOL_RANGE_MPEG;
		v.extradata = static_cast<unsigned char *>(av_mallocz(description.extraData.size() + AV_INPUT_BUFFER_PADDING_SIZE));
		if (!v.extradata) throw std::bad_alloc();
		v.extradata_size = int(description.extraData.size());
		std::copy(description.extraData.begin(), description.extraData.end(), v.extradata);
		check(avcodec_parameters_from_context(audioStream->codecpar, audioEncoder.get()), "Describe AAC stream");
		samples->format = c.sample_fmt; samples->sample_rate = c.sample_rate; samples->nb_samples = c.frame_size;
		check(av_channel_layout_copy(&samples->ch_layout, &c.ch_layout), "Describe audio channels");
		check(av_frame_get_buffer(samples.get(), 0), "Allocate AAC frame");
		AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
		SwrContext *swr = nullptr;
		check(swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLTP, 44100, &stereo, AV_SAMPLE_FMT_S16, 44100, 0, nullptr), "Create audio converter");
		converter.reset(swr); check(swr_init(swr), "Initialize audio converter");
		pending.reserve(std::size_t(c.frame_size)*2);
		output.context->pb = storage.context; output.context->flags |= AVFMT_FLAG_CUSTOM_IO;
		Dictionary options;
		av_dict_set(&options.value, "movflags", "+frag_keyframe+delay_moov+default_base_moof", 0);
		// Microsecond MP4 track ticks overflow a sample duration after about 36
		// minutes of suspension. 3000 Hz retains exact common frame rates and
		// sub-millisecond timing while leaving days of room for timestamp gaps.
		av_dict_set(&options.value, "video_track_timescale", "3000", 0);
		av_dict_set(&options.value, "frag_duration", "1000000", 0);
		int result = avformat_write_header(output.context, &options.value);
		check(result, "Write fragmented MP4 header");
	}
	void drain()
	{
		for (;;)
		{
			int result = avcodec_receive_packet(audioEncoder.get(), encoded.get());
			if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
			check(result, "Receive AAC packet");
			av_packet_rescale_ts(encoded.get(), audioEncoder->time_base, audioStream->time_base);
			encoded->stream_index = audioStream->index;
			check(av_interleaved_write_frame(output.context, encoded.get()), "Write AAC packet");
			av_packet_unref(encoded.get());
		}
	}
	void encode()
	{
		check(av_frame_make_writable(samples.get()), "Reuse AAC frame");
		const unsigned char *input[] = {reinterpret_cast<const unsigned char *>(pending.data())};
		check(swr_convert(converter.get(), samples->data, samples->nb_samples, input, samples->nb_samples), "Convert audio");
		samples->pts = audioPts; audioPts += samples->nb_samples;
		check(avcodec_send_frame(audioEncoder.get(), samples.get()), "Encode audio");
		drain(); pending.clear();
	}
	void append(const std::int16_t *pcm, std::size_t count)
	{
		while (count)
		{
			auto n = std::min(count, std::size_t(samples->nb_samples)*2 - pending.size());
			if (pcm) { pending.insert(pending.end(), pcm, pcm+n); pcm += n; }
			else pending.insert(pending.end(), n, 0);
			written += n/2; count -= n;
			if (pending.size() == std::size_t(samples->nb_samples)*2) encode();
		}
	}
};
MediaWriter::MediaWriter(const std::string &path, const VideoDescription &description, OpenRecordingFile open) : impl(std::make_unique<Impl>(path,description,std::move(open))) {}
MediaWriter::~MediaWriter() = default;
void MediaWriter::video(const VideoPacket &video)
{
	auto p = packet(); check(av_new_packet(p.get(), int(video.bytes.size())), "Allocate H.264 packet");
	std::copy(video.bytes.begin(), video.bytes.end(), p->data);
	p->pts = video.ptsUs; p->dts = video.dtsUs; p->duration = video.durationUs;
	p->flags = video.key ? AV_PKT_FLAG_KEY : 0;
	av_packet_rescale_ts(p.get(), {1,1000000}, impl->videoStream->time_base);
	p->stream_index = impl->videoStream->index;
	check(av_interleaved_write_frame(impl->output.context, p.get()), "Write H.264 packet");
}
void MediaWriter::silenceThrough(std::int64_t end)
{
	// A suspended application may return minutes later. Preserve the timestamp
	// discontinuity instead of encoding minutes of synthetic PCM in a catch-up burst.
	if (end-impl->written > 44100/2)
	{
		if (!impl->pending.empty()) { impl->pending.resize(std::size_t(impl->samples->nb_samples)*2,0); impl->encode(); }
		impl->written = impl->audioPts = end;
		return;
	}
	if (end > impl->written) impl->append(nullptr, std::size_t(end-impl->written)*2);
}
void MediaWriter::audio(const std::int16_t *pcm, std::size_t frames, std::int64_t start)
{
	if (start >= 0 && std::abs(start-impl->written) < 44100/50) start = impl->written;
	const auto end = start + std::int64_t(frames);
	silenceThrough(std::max<std::int64_t>(0,start));
	if (end <= impl->written) return;
	auto skip = impl->written-start;
	impl->append(pcm+skip*2, std::size_t(end-impl->written)*2);
}
void MediaWriter::checkpoint() { impl->storage.flush(); }
void MediaWriter::finish(std::int64_t durationUs)
{
	if (impl->finished) return;
	silenceThrough(av_rescale(durationUs,44100,1000000));
	if (!impl->pending.empty())
	{
		impl->pending.resize(std::size_t(impl->samples->nb_samples)*2,0);
		impl->encode();
	}
	check(avcodec_send_frame(impl->audioEncoder.get(),nullptr), "Flush AAC encoder"); impl->drain();
	check(av_write_trailer(impl->output.context), "Finish fragmented MP4"); impl->storage.flush();
	impl->finished = true;
}
void MediaWriter::finalize(const std::string &source, const std::string &destination,
	const std::vector<MediaChapter> &chapters, OpenRecordingFile open)
{
	IO inputFile(open(source,false),false);
	Input input;
	if (!input.context) throw std::bad_alloc();
	input.context->pb = inputFile.context; input.context->flags |= AVFMT_FLAG_CUSTOM_IO;
	check(avformat_open_input(&input.context,nullptr,av_find_input_format("mov"),nullptr), "Open recorded MP4");
	// Streams are described by our MP4 header; no decoder or stream probing is needed.
	IO outputFile(open(destination,true),true);
	Output output;
	output.context->pb = outputFile.context; output.context->flags |= AVFMT_FLAG_CUSTOM_IO;
	for (unsigned i=0; i<input.context->nb_streams; ++i)
	{
		auto stream = avformat_new_stream(output.context,nullptr); if (!stream) throw std::bad_alloc();
		check(avcodec_parameters_copy(stream->codecpar,input.context->streams[i]->codecpar), "Copy recorded stream");
		if (stream->codecpar->codec_id == AV_CODEC_ID_AAC) stream->codecpar->frame_size = 1024;
		stream->time_base = input.context->streams[i]->time_base;
	}
	output.context->chapters = static_cast<AVChapter **>(av_calloc(chapters.size(),sizeof(AVChapter *)));
	if (!chapters.empty() && !output.context->chapters) throw std::bad_alloc();
	for (const auto &chapter : chapters)
	{
		auto c = static_cast<AVChapter *>(av_mallocz(sizeof(AVChapter))); if (!c) throw std::bad_alloc();
		c->id = output.context->nb_chapters; c->time_base = {1,1000000}; c->start = chapter.startUs; c->end = chapter.endUs;
		av_dict_set(&c->metadata,"title",chapter.title.c_str(),0);
		output.context->chapters[output.context->nb_chapters++] = c;
	}
	// FFmpeg's faststart relocation reopens the destination. Route that second
	// pass through the same storage factory, including worker-owned OPFS files.
	output.context->url = av_strdup(destination.c_str());
	if (!output.context->url) throw std::bad_alloc();
	output.context->opaque = &open;
	output.context->io_open = [](AVFormatContext *context, AVIOContext **io, const char *url, int flags, AVDictionary **) -> int
	{
		try
		{
			auto factory = static_cast<OpenRecordingFile *>(context->opaque);
			auto bridge = std::make_unique<IO>((*factory)(url, bool(flags & AVIO_FLAG_WRITE)), bool(flags & AVIO_FLAG_WRITE));
			*io = bridge->context; bridge.release(); return 0;
		}
		catch (...) { return AVERROR(EIO); }
	};
	output.context->io_close2 = [](AVFormatContext *, AVIOContext *io) -> int
	{
		delete static_cast<IO *>(io->opaque); return 0;
	};
	Dictionary options;
	av_dict_set(&options.value,"movflags","+faststart",0);
	av_dict_set(&options.value,"video_track_timescale","3000",0);
	int result = avformat_write_header(output.context,&options.value);
	check(result,"Write final MP4 header");
	auto p = packet();
	for (;;)
	{
		result = av_read_frame(input.context,p.get());
		if (result == AVERROR_EOF) break;
		check(result,"Read recorded packet");
		auto in = input.context->streams[p->stream_index]; auto out = output.context->streams[p->stream_index];
		if (!chapters.empty())
		{
			auto end = av_rescale_q(chapters.back().endUs,{1,1000000},in->time_base);
			if (p->pts >= end) { av_packet_unref(p.get()); continue; }
			p->duration = std::min(p->duration,end-p->pts);
		}
		av_packet_rescale_ts(p.get(),in->time_base,out->time_base); p->pos = -1;
		check(av_interleaved_write_frame(output.context,p.get()),"Finalize recorded packet"); av_packet_unref(p.get());
	}
	check(av_write_trailer(output.context),"Finish final MP4"); outputFile.flush();
}
std::int64_t MediaWriter::duration(const std::string &source, OpenRecordingFile open)
{
	IO file(open(source,false),false); Input input;
	if (!input.context) throw std::bad_alloc();
	input.context->pb = file.context; input.context->flags |= AVFMT_FLAG_CUSTOM_IO;
	check(avformat_open_input(&input.context,nullptr,av_find_input_format("mov"),nullptr),"Open interrupted recording");
	auto p = packet(); std::int64_t duration = 0;
	for (;;)
	{
		int result = av_read_frame(input.context,p.get());
		if (result == AVERROR_EOF) break;
		check(result,"Read interrupted recording");
		auto stream = input.context->streams[p->stream_index];
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && p->pts != AV_NOPTS_VALUE)
			duration = std::max(duration,av_rescale_q(p->pts+p->duration,stream->time_base,{1,1000000}));
		av_packet_unref(p.get());
	}
	if (!duration) throw std::runtime_error("Interrupted recording has no recoverable video fragment");
	return duration;
}
} // namespace GAGCore::Recording::Detail
