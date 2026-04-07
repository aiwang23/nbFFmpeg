#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Tools.h"
#include "concurrentqueue.h"
#include "RecordNode.h"
#include "EncodeNode.h"
#include <thread>

extern "C" {
#include <libavdevice/avdevice.h>
}


int main() {
    avdevice_register_all();

    std::string url = "/dev/video0";
    FrameQueue frm_queue;
    std::vector<std::pair<std::string, std::string>> opts = {{"framerate", "30"}};
    std::thread record_node_thread(recordNode, RecordNodeParament{RecordConfig{url, opts}, frm_queue});

    PacketQueue pkt_queue;
    VideoEncodeConfig config;
    config.width = 1280;
    config.height = 720;
    std::thread encode_node_thread(videoEncodeNode, VideoEncodeNodeParament{config, frm_queue, pkt_queue});

    record_node_thread.join();
}
