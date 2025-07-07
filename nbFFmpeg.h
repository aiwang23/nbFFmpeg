//
// Created by wang on 25-7-7.
//

#ifndef NBFFMPEG_H
#define NBFFMPEG_H
#include <functional>
#include <string>
#include "types.h"

class NbFFmpeg {
public:
    using PacketCallback = std::function<void(packet)>;

    explicit NbFFmpeg();

    ~NbFFmpeg() = default;

    NbFFmpeg &input(const std::string &url);

    NbFFmpeg &output(const std::string &url);

    NbFFmpeg &onInput(PacketCallback cb);

    NbFFmpeg &onOutput(PacketCallback cb);

    NbFFmpeg &format(const std::string &fmt);

    NbFFmpeg &audioCodec(const std::string &codec);

    NbFFmpeg &videoCodec(const std::string &codec);


    int run();

private:
    std::string iUrl_;
    PacketCallback onInputPacket_;
    std::string oUrl_;
    std::string oFormat_;
    PacketCallback onOutPutPacket_;
    std::string format_; // 手动指定的封装格式（如 "mp4"）

    std::string audioCodec_; // 音频编码器名
    std::string videoCodec_; // 视频编码器名
};
#endif //NBFFMPEG_H
