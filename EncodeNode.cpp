//
// Created by wang on 2026/4/4.
//

#include "EncodeNode.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
}

int videoEncodeNode(const VideoEncodeNodeParament& parament) {
    VideoEncodeConfig config = parament.encode_config;

    const AVCodec* codec = avcodec_find_encoder_by_name(config.codec_name.c_str());
    if (not codec) return -1;

    AVCodecContextPtr codec_context_ptr(avcodec_alloc_context3(codec), AVCodecContextCloser);
    AVCodecContext* codec_context = codec_context_ptr.get();

    if (not parament.parameters_)
        config.apply(codec_context);
    else
        TRY(avcodec_parameters_to_context(codec_context, parament.parameters_));

    DictGuard options = {};
    for (const auto& [key, val] : config.priv_opts) {
        av_dict_set(&options.ptr, key.c_str(), val.c_str(), 0);
    }

    TRY(avcodec_open2(codec_context, codec, &options.ptr));

    while (true) {
        AVFrameSharedPtr frm_ptr;
        parament.frm_queue.try_dequeue(frm_ptr);
        TRY(avcodec_send_frame(codec_context, frm_ptr.get()));

        AVPacketSharedPtr pkt_ptr(av_packet_alloc(), [](AVPacket* pkt) { if (pkt) av_packet_free(&pkt); });
        TRY(avcodec_receive_packet(codec_context, pkt_ptr.get()));
        parament.pkt_queue.enqueue(pkt_ptr);
    }

    return 0;
}
