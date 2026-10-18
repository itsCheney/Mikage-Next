// H.264 decoding for siglus_rs: some repacks store their .g00 images as UCI
// blobs, which are H.264 intra frames (often High 10). siglus_rs has no
// decoder of its own, so this one, on the FFmpeg the other runtimes use, is
// linked into SiglusRuntime and registers itself when the framework loads.

#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>

#include "siglus.h"

static int32_t mikage_siglus_decode_h264(const uint8_t *data, size_t length, uint32_t width, uint32_t height,
                                         uint16_t *y, uint16_t *u, uint16_t *v) {
    if (!data || length == 0 || length > INT32_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
        return -1;
    }
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        return -2;
    }
    AVCodecContext *context = avcodec_alloc_context3(codec);
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int32_t result = -3;
    if (!context || !packet || !frame) {
        goto done;
    }
    context->thread_count = 1;
    if (avcodec_open2(context, codec, NULL) < 0) {
        goto done;
    }

    // FFmpeg reads past the end of its input, so give it padded memory.
    uint8_t *buffer = av_malloc(length + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buffer) {
        goto done;
    }
    memcpy(buffer, data, length);
    memset(buffer + length, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    if (av_packet_from_data(packet, buffer, (int)length) < 0) {
        av_free(buffer);
        goto done;
    }
    if (avcodec_send_packet(context, packet) < 0 || avcodec_send_packet(context, NULL) < 0 ||
        avcodec_receive_frame(context, frame) < 0) {
        result = -4;
        goto done;
    }
    // 4:2:0 frames have even sizes: an odd-sized image is coded one pixel
    // larger, so take its top-left part.
    if ((uint32_t)frame->width != width + (width & 1) || (uint32_t)frame->height != height + (height & 1)) {
        result = -5;
        goto done;
    }
    const AVPixFmtDescriptor *format = av_pix_fmt_desc_get(frame->format);
    if (!format || format->nb_components < 3 || (format->flags & AV_PIX_FMT_FLAG_RGB) ||
        format->log2_chroma_w != 1 || format->log2_chroma_h != 1 || format->comp[0].depth > 16) {
        result = -6;
        goto done;
    }

    int depth = format->comp[0].depth;
    uint16_t *planes[3] = {y, u, v};
    for (int plane = 0; plane < 3; plane++) {
        uint32_t plane_width = plane == 0 ? width : (width + 1) / 2;
        uint32_t plane_height = plane == 0 ? height : (height + 1) / 2;
        const uint8_t *source = frame->data[plane];
        uint16_t *target = planes[plane];
        for (uint32_t row = 0; row < plane_height; row++) {
            const uint8_t *line = source + (size_t)row * frame->linesize[plane];
            if (depth > 8) {
                memcpy(target, line, plane_width * sizeof(uint16_t));
            } else {
                for (uint32_t column = 0; column < plane_width; column++) {
                    target[column] = line[column];
                }
            }
            target += plane_width;
        }
    }
    result = depth;

done:
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&context);
    return result;
}

__attribute__((constructor)) static void mikage_siglus_register_h264_decoder(void) {
    siglus_mikage_set_h264_decoder(mikage_siglus_decode_h264);
}
