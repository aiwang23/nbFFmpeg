//
// Created by wang on 2026/4/4.
//

#ifndef NBFFMPEG_ENCODENODE_H
#define NBFFMPEG_ENCODENODE_H
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "Tools.h"
#include "concurrentqueue.h"



struct VideoEncodeConfig {
    std::string codec_name = "libx264";
    int bit_rate = 400000;
    int width = 0;
    int height = 0;
    AVRational time_base = {1, 25};
    AVRational framerate = {25, 1};
    int gop_size = 10;
    int max_b_frames = 1;
    AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;
    std::vector<std::pair<std::string, std::string>> priv_opts;

    int apply(AVCodecContext* c) const {
        c->bit_rate = bit_rate;
        c->width = width;
        c->height = height;
        c->time_base = time_base;
        c->framerate = framerate;
        c->gop_size = gop_size;
        c->max_b_frames = max_b_frames;
        c->pix_fmt = pix_fmt;
        return 0;
    }
};

struct VideoEncodeNodeParament {
    VideoEncodeConfig encode_config;
    const AVCodecParameters* parameters_ = nullptr;
    moodycamel::ConcurrentQueue<AVFrameSharedPtr>& frm_queue; // 数据入口
    moodycamel::ConcurrentQueue<AVPacketSharedPtr>& pkt_queue; // 数据出口

    VideoEncodeNodeParament(VideoEncodeConfig cfg,
                            moodycamel::ConcurrentQueue<AVFrameSharedPtr>& fq,
                            moodycamel::ConcurrentQueue<AVPacketSharedPtr>& pq) : encode_config(cfg),
        frm_queue(fq), pkt_queue(pq) {
    }

    VideoEncodeNodeParament(const AVCodecParameters* parameters,
                            moodycamel::ConcurrentQueue<AVFrameSharedPtr>& fq,
                            moodycamel::ConcurrentQueue<AVPacketSharedPtr>& pq) : parameters_(parameters),
        frm_queue(fq), pkt_queue(pq) {
    }
};

int videoEncodeNode(const VideoEncodeNodeParament& parament);
#endif //NBFFMPEG_ENCODENODE_H
