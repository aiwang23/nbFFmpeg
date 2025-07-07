//
// Created by wang on 25-7-7.
//

#include "nbFFmpeg.h"

#include <algorithm>
#include <map>

extern "C" {
#include <libavformat/avformat.h>

#include <utility>
}

NbFFmpeg::NbFFmpeg() {
}


NbFFmpeg &NbFFmpeg::input(const std::string &url) {
    iUrl_ = url;
    return *this;
}

NbFFmpeg &NbFFmpeg::output(const std::string &url) {
    oUrl_ = url;

    auto dot = url.find_last_of('.');
    if (dot != std::string::npos) {
        oFormat_ = url.substr(dot + 1);
        std::transform(oFormat_.begin(), oFormat_.end(), oFormat_.begin(), ::tolower);
    }

    return *this;
}

NbFFmpeg &NbFFmpeg::onInput(PacketCallback cb) {
    onInputPacket_ = std::move(cb);
    return *this;
}

NbFFmpeg &NbFFmpeg::onOutput(PacketCallback cb) {
    onOutPutPacket_ = std::move(cb);
    return *this;
}

NbFFmpeg &NbFFmpeg::format(const std::string &fmt) {
    format_ = fmt;
    return *this;
}

NbFFmpeg &NbFFmpeg::audioCodec(const std::string &codec) {
    audioCodec_ = codec;
    return *this;
}

NbFFmpeg &NbFFmpeg::videoCodec(const std::string &codec) {
    videoCodec_ = codec;
    return *this;
}

int NbFFmpeg::run() {
    std::cout << "NbFFmpeg start\n";

    if (!audioCodec_.empty()) {
        std::cout << "[提示] 用户设置了音频编码器: " << audioCodec_ << std::endl;
    }
    if (!videoCodec_.empty()) {
        std::cout << "[提示] 用户设置了视频编码器: " << videoCodec_ << std::endl;
    }

    if (iUrl_.empty() || oUrl_.empty()) {
        std::cout << R"(
Usage: nbFFmpeg().input("in.mp4").output("out.mp3").run();

说明:
  自动推断是否仅提取音频或视频：
    - .mp3/.aac/.m4a 等只提取音频
    - .h264/.h265/.hevc/.264 只提取视频
)";
        return -1;
    }

    errorMsg err;
    AVFormatContext *inFmtCtx = nullptr;
    AVFormatContext *outFmtCtx = nullptr;
    packet pkt;

    auto cleanup = [&]() {
        if (inFmtCtx) avformat_close_input(&inFmtCtx);
        if (outFmtCtx) {
            if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&outFmtCtx->pb);
            }
            avformat_free_context(outFmtCtx);
        }
    };

    bool autoAudioOnly = (oFormat_ == "mp3" || oFormat_ == "aac" || oFormat_ == "m4a" || oFormat_ == "wav");
    bool autoVideoOnly = (oFormat_ == "h264" || oFormat_ == "h265" || oFormat_ == "hevc" || oFormat_ == "264" ||
                          oFormat_ == "265");
    bool doAudioOnly = autoAudioOnly;
    bool doVideoOnly = autoVideoOnly;

    err = avformat_open_input(&inFmtCtx, iUrl_.c_str(), nullptr, nullptr);
    if (err) {
        std::cout << "打开输入失败: " << err << std::endl;
        cleanup();
        return err.code();
    }

    err = avformat_find_stream_info(inFmtCtx, nullptr);
    if (err) {
        std::cout << "查找输入流信息失败: " << err << std::endl;
        cleanup();
        return err.code();
    }

    av_dump_format(inFmtCtx, 0, iUrl_.c_str(), 0);

    if (format_.empty()) {
        err = avformat_alloc_output_context2(&outFmtCtx, nullptr, nullptr, oUrl_.c_str());
    } else {
        err = avformat_alloc_output_context2(&outFmtCtx, nullptr, format_.c_str(), oUrl_.c_str());
    }
    if (err) {
        std::cout << "创建输出上下文失败: " << err << std::endl;
        cleanup();
        return err.code();
    }

    std::map<int, int> streamMapping;
    int outStreamIndex = 0;

    for (unsigned int i = 0; i < inFmtCtx->nb_streams; ++i) {
        AVStream *inStream = inFmtCtx->streams[i];

        if (doAudioOnly && inStream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        if (doVideoOnly && inStream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
            continue;

        AVStream *outStream = avformat_new_stream(outFmtCtx, nullptr);
        if (!outStream) {
            err = AVERROR_UNKNOWN;
            std::cout << "创建输出流失败: " << err << std::endl;
            cleanup();
            return err.code();
        }

        err = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
        if (err) {
            std::cout << "拷贝编码器参数失败: " << err << std::endl;
            cleanup();
            return err.code();
        }

        outStream->codecpar->codec_tag = 0;
        streamMapping[i] = outStreamIndex++;
    }

    av_dump_format(outFmtCtx, 0, oUrl_.c_str(), 1);

    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        err = avio_open(&outFmtCtx->pb, oUrl_.c_str(), AVIO_FLAG_WRITE);
        if (err) {
            std::cout << "打开输出文件失败: " << err << std::endl;
            cleanup();
            return err.code();
        }
    }

    err = avformat_write_header(outFmtCtx, nullptr);
    if (err) {
        std::cout << "写文件头失败: " << err << std::endl;
        cleanup();
        return err.code();
    }

    while (true) {
        err = av_read_frame(inFmtCtx, pkt.get());
        if (err.code() == AVERROR_EOF) {
            err = 0;
            break;
        }
        if (err) {
            std::cout << "读取帧失败: " << err << std::endl;
            break;
        }

        int inIndex = pkt->stream_index;
        if (streamMapping.find(inIndex) == streamMapping.end()) {
            pkt.clear();
            continue;
        }

        int outIndex = streamMapping[inIndex];

        if (onInputPacket_)
            onInputPacket_(pkt);

        AVStream *inStream = inFmtCtx->streams[inIndex];
        AVStream *outStream = outFmtCtx->streams[outIndex];

        pkt->pts = av_rescale_q_rnd(pkt->pts, inStream->time_base, outStream->time_base,
                                    static_cast<enum AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt->dts = av_rescale_q_rnd(pkt->dts, inStream->time_base, outStream->time_base,
                                    static_cast<enum AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt->duration = av_rescale_q(pkt->duration, inStream->time_base, outStream->time_base);
        pkt->pos = -1;
        pkt->stream_index = outIndex;

        if (onOutPutPacket_)
            onOutPutPacket_(pkt);

        err = av_interleaved_write_frame(outFmtCtx, pkt.get());
        if (err) {
            std::cout << "写入帧失败: " << err << std::endl;
            break;
        }

        pkt.clear();
    }

    av_write_trailer(outFmtCtx);
    cleanup();

    std::cout << "NbFFmpeg quit\n";
    return err.code();
}
