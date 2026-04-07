//
// Created by wang on 2026/4/4.
//

#ifndef NBFFMPEG_RECORDNODE_H
#define NBFFMPEG_RECORDNODE_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "concurrentqueue.h"
#include "Tools.h"

struct RecordConfig {
    std::string url;
    std::vector<std::pair<std::string, std::string>> opts;
};

struct RecordNodeParament {
    RecordConfig config;
    moodycamel::ConcurrentQueue<AVPacketSharedPtr>* pkt_queue = nullptr;
    moodycamel::ConcurrentQueue<AVFrameSharedPtr>* frm_queue = nullptr;

    // 构造函数 1
    RecordNodeParament(RecordConfig cfg, moodycamel::ConcurrentQueue<AVPacketSharedPtr>& pkt_q)
        : config(std::move(cfg)), pkt_queue(&pkt_q) {}

    // 构造函数 2
    RecordNodeParament(RecordConfig cfg, moodycamel::ConcurrentQueue<AVFrameSharedPtr>& frm_q)
        : config(std::move(cfg)), frm_queue(&frm_q) {}
};

int recordNode(const RecordNodeParament& parament);

#endif //NBFFMPEG_RECORDNODE_H
