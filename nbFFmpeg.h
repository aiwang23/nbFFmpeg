//
// Created by wang on 25-7-7.
//

#ifndef NBFFMPEG_H
#define NBFFMPEG_H
#include <functional>
#include <map>
#include <string>
#include <thread>

#include "concurrentqueue.h"
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

    NbFFmpeg &setQueueSize(int size);

    int run();

private:
    int setupEncoder(int inIndex, AVStream *inStream, AVStream *outStream, bool isAudio);

    int transcodeLoop(AVFormatContext *inFmtCtx, AVFormatContext *outFmtCtx);

    int checkAndPrintUsage();

    int openInput(AVFormatContext *&inFmtCtx);

    int createOutput(AVFormatContext *&outFmtCtx);

    int setupRemuxOrEncodeStreams(AVFormatContext *inFmtCtx, AVFormatContext *outFmtCtx);

    static AVCodecID getAVCodecID(const std::string &name);

    void readThread(AVFormatContext *inFmtCtx); // av_read_frame
    void decodeThread(); // 解码 packet → frame
    void encodeThread(); // 编码 frame → packet
    void forwardThread(AVFormatContext *inFmtCtx, AVFormatContext *outFmtCtx); // packet 转发、PTS调整等
    void writeThread(AVFormatContext *outFmtCtx); // 写入 muxer / 推流
    void forceStop(); // 强行停止所有线程
    void waitUntilFinished(); // 等待所有线程完成

private:
    std::string iUrl_;
    std::string oUrl_;
    std::string oFormat_; // 用户指定的输出文件后缀
    std::string format_; // 手动指定封装格式
    std::string audioCodec_; // 音频编码器
    std::string videoCodec_; // 视频编码器

    PacketCallback onInputPacket_;
    PacketCallback onOutputPacket_;

    struct StreamContext {
        AVCodecContext *decoder = nullptr;
        AVCodecContext *encoder = nullptr;
    };

    std::map<int, int> streamMapping_;
    std::map<int, StreamContext> streamCtxMap_;

    std::atomic_bool running_ = false;
    std::vector<std::thread> threads_;

    moodycamel::ConcurrentQueue<packet> inputPacketQueue_;
    moodycamel::ConcurrentQueue<frame> decodedFrameQueue_;
    moodycamel::ConcurrentQueue<packet> encodedPacketQueue_;
    moodycamel::ConcurrentQueue<packet> writePacketQueue_;
    int maxQueueSize_ = 50;

    struct ThreadStatus {
        std::atomic_bool readDone{false};
        std::atomic_bool decodeDone{false};
        std::atomic_bool encodeDone{false};
        std::atomic_bool forwardDone{false};
        std::atomic_bool writeDone{false};

        bool allDone() const {
            return readDone && decodeDone && encodeDone && forwardDone && writeDone;
        }
    };

    ThreadStatus threadStatus_;
};
#endif //NBFFMPEG_H
