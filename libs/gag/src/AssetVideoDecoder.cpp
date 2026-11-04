// SPDX-License-Identifier: GPL-3.0-or-later
#include "AssetVideoDecoder.h"

#ifdef GLOB2_H264_ASSETS
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

namespace GAGCore::AssetVideoDecoder {
namespace {
struct CodecDeleter {
    void operator()(AVCodecContext *value) const { avcodec_free_context(&value); }
    void operator()(AVPacket *value) const { av_packet_free(&value); }
    void operator()(AVFrame *value) const { av_frame_free(&value); }
};

void requireVideo(int result, const char *message) {
    if (result >= 0) return;
    char detail[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(result, detail, sizeof(detail));
    throw std::runtime_error(std::string(message) + ": " + detail);
}

bool startsIndependentGroup(const uint8_t *bytes, int size) {
    bool sps = false, pps = false;
    for (int i = 0; i + 3 < size; ++i) {
        if (bytes[i] || bytes[i + 1] || bytes[i + 2] != 1) continue;
        const int nal = bytes[i + 3] & 31;
        if (nal == 5) return sps && pps;
        if (nal >= 1 && nal <= 4) return false;
        sps |= nal == 7;
        pps |= nal == 8;
    }
    return false;
}

int colorMatrix(AVColorSpace space) {
    switch (space) {
    case AVCOL_SPC_BT709: return SWS_CS_ITU709;
    case AVCOL_SPC_FCC: return SWS_CS_FCC;
    case AVCOL_SPC_SMPTE240M: return SWS_CS_SMPTE240M;
    case AVCOL_SPC_BT2020_NCL: return SWS_CS_BT2020;
    default: return SWS_CS_DEFAULT;
    }
}

} // namespace

bool supported() {
    return avcodec_find_decoder(AV_CODEC_ID_H264) != nullptr;
}

void decode(std::span<const uint8_t> bytes, unsigned width, unsigned height,
        unsigned frameCount, const FrameSink& output) {
    if (!validDimensions(width, height, frameCount) || bytes.empty() || bytes.size() > MaxEncodedBytes)
        throw std::runtime_error("Invalid H.264 asset group description");
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    std::unique_ptr<AVCodecContext, CodecDeleter> decoder(avcodec_alloc_context3(codec));
    std::unique_ptr<AVCodecParserContext, decltype(&av_parser_close)> parser(av_parser_init(AV_CODEC_ID_H264), av_parser_close);
    std::unique_ptr<AVPacket, CodecDeleter> packet(av_packet_alloc());
    std::unique_ptr<AVFrame, CodecDeleter> frame(av_frame_alloc()), rgb(av_frame_alloc());
    if (!codec || !decoder || !parser || !packet || !frame || !rgb)
        throw std::runtime_error("Cannot allocate H.264 asset decoder");
    // The scheduler owns parallelism. Keep codec state private and allow only
    // the modest alignment slack FFmpeg requires around coded dimensions.
    decoder->thread_count = 1;
    decoder->max_pixels = int64_t(width + 64) * (height + 64);
    decoder->err_recognition = AV_EF_EXPLODE | AV_EF_BITSTREAM | AV_EF_BUFFER;
    requireVideo(avcodec_open2(decoder.get(), codec, nullptr), "Cannot open H.264 asset decoder");

    rgb->width = int(width);
    rgb->height = int(height);
    rgb->format = AV_PIX_FMT_RGBA;
    requireVideo(av_frame_get_buffer(rgb.get(), 32), "Cannot allocate H.264 color conversion buffer");
    std::unique_ptr<SwsContext, decltype(&sws_freeContext)> scaler(nullptr, sws_freeContext);
    unsigned decoded = 0;
    const auto receive = [&] {
        for (;;) {
            const int status = avcodec_receive_frame(decoder.get(), frame.get());
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return;
            requireVideo(status, "Cannot decode H.264 asset frame");
            if (decoded >= frameCount || frame->width != int(width) ||
                    frame->height != int(height) || (frame->flags & AV_FRAME_FLAG_CORRUPT))
                throw std::runtime_error("Unexpected H.264 asset frame dimensions, count or damage");

            scaler.reset(sws_getCachedContext(scaler.release(), frame->width, frame->height,
                AVPixelFormat(frame->format), rgb->width, rgb->height, AVPixelFormat(rgb->format),
                SWS_BILINEAR, nullptr, nullptr, nullptr));
            if (!scaler) throw std::runtime_error("Unsupported H.264 asset pixel format");
            const int *matrix = sws_getCoefficients(colorMatrix(frame->colorspace));
            requireVideo(sws_setColorspaceDetails(scaler.get(), matrix, frame->color_range == AVCOL_RANGE_JPEG,
                matrix, 1, 0, 1 << 16, 1 << 16), "Cannot set H.264 asset color conversion");
            if (sws_scale(scaler.get(), frame->data, frame->linesize, 0, frame->height, rgb->data, rgb->linesize) != int(height))
                throw std::runtime_error("Incomplete H.264 asset color conversion");

            for (unsigned y = 0; y < height; ++y) {
                auto *row = rgb->data[0] + y * rgb->linesize[0];
                for (unsigned x = 0; x < width; ++x)
                    row[4 * x + 3] = 255;
            }
            output(decoded++, rgb->data[0], rgb->linesize[0]);
            av_frame_unref(frame.get());
        }
    };

    unsigned packets = 0;
    const auto send = [&](const uint8_t *data, int length) {
        if (packets >= frameCount || (!packets && !startsIndependentGroup(data, length)))
            throw std::runtime_error("Invalid H.264 asset group framing");
        ++packets;
        requireVideo(av_new_packet(packet.get(), length), "Cannot allocate H.264 asset packet");
        std::memcpy(packet->data, data, size_t(length));
        requireVideo(avcodec_send_packet(decoder.get(), packet.get()), "Cannot submit H.264 asset packet");
        av_packet_unref(packet.get());
        receive();
    };

    // FFmpeg's parser can read up to AV_INPUT_BUFFER_PADDING_SIZE beyond the
    // logical input. The asset scheduler's file buffer has no such padding.
    std::vector<uint8_t> input(bytes.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    std::memcpy(input.data(), bytes.data(), bytes.size());
    auto *next = input.data();
    int remaining = int(bytes.size());
    while (remaining) {
        uint8_t *data = nullptr;
        int length = 0;
        const int consumed = av_parser_parse2(parser.get(), decoder.get(), &data, &length, next, remaining,
            AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        requireVideo(consumed, "Cannot parse H.264 asset group");
        if (consumed > remaining || (!consumed && !length))
            throw std::runtime_error("Invalid H.264 asset parser progress");
        next += consumed;
        remaining -= consumed;
        if (length) send(data, length);
    }
    uint8_t *last = nullptr;
    int length = 0;
    requireVideo(av_parser_parse2(parser.get(), decoder.get(), &last, &length, nullptr, 0,
        AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0), "Cannot finish H.264 asset group parser");
    if (length) send(last, length);
    requireVideo(avcodec_send_packet(decoder.get(), nullptr), "Cannot finish H.264 asset group decoder");
    receive();
    if (decoded != frameCount)
        throw std::runtime_error("Incomplete H.264 asset group");
}
}
#endif
