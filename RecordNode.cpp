//
// Created by wang on 2026/4/4.
//

#include "RecordNode.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
}


int recordNode(const RecordNodeParament& parament) {
    AVFormatContext* format_context = nullptr;
    const AVInputFormat* input_format = av_find_input_format("v4l2");
    int stream_idx = -1;
    AVCodecContext* codec_context = nullptr;

    DictGuard options = {};
    for (const auto& [key, val] : parament.config.opts) {
        TRY(av_dict_set(&options.ptr, key.c_str(), val.c_str(), 0));
    }

    TRY(avformat_open_input(&format_context, parament.config.url.c_str(), input_format, &options.ptr));
    AVFormatContextPtr format_ctx_ptr(format_context, AVFormatContextCloser);

    TRY(avformat_find_stream_info(format_ctx_ptr.get(), nullptr));
    TRY_CODE(av_find_best_stream(format_ctx_ptr.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, -1), stream_idx);

    if (parament.frm_queue) {
        const AVCodec * codec = avcodec_find_decoder(format_context->streams[stream_idx]->codecpar->codec_id);
        codec_context = avcodec_alloc_context3(codec);
        TRY(avcodec_parameters_to_context(codec_context, format_context->streams[stream_idx]->codecpar));
        TRY(avcodec_open2(codec_context, codec, nullptr));
    }

    while (true) {
        AVPacketSharedPtr pkt_ptr(av_packet_alloc(), [](AVPacket* pkt) { if (pkt) av_packet_free(&pkt); });

        TRY_READ(av_read_frame(format_ctx_ptr.get(), pkt_ptr.get()));

        if (pkt_ptr->stream_index == stream_idx) {
            if (parament.pkt_queue) {
                // 说明是 Packet 模式
                parament.pkt_queue->enqueue(pkt_ptr);
            } else if (parament.frm_queue) {
                // 说明是 Frame 模式
                TRY(avcodec_send_packet(codec_context, pkt_ptr.get()));

                AVFrameSharedPtr frm_ptr(av_frame_alloc(), [](AVFrame* frm) { if (frm) av_frame_free(&frm); });
                TRY(avcodec_receive_frame(codec_context, frm_ptr.get()));
                parament.frm_queue->enqueue(frm_ptr);
            }
        }
    }
    return 0;
}
