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
    onOutputPacket_ = std::move(cb);
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

NbFFmpeg &NbFFmpeg::setQueueSize(int size) {
    maxQueueSize_ = size;
    return *this;
}

int NbFFmpeg::run() {
    std::cout << "NbFFmpeg start" << std::endl;

    // 检查输入参数
    if (checkAndPrintUsage() == 1) {
        return 1;
    }

    errorMsg err;
    AVFormatContext *inFmtCtx = nullptr;
    AVFormatContext *outFmtCtx = nullptr;
    packet pkt;

    auto cleanup = [&]() {
        if (inFmtCtx) {
            avformat_close_input(&inFmtCtx);
            inFmtCtx = nullptr;
        }
        if (outFmtCtx) {
            if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&outFmtCtx->pb);
            }
            avformat_free_context(outFmtCtx);
            outFmtCtx = nullptr;
        }
    };

    // 打开输入文件
    if (openInput(inFmtCtx)) {
        cleanup();
        return -1;
    }
    // 创建输出文件
    if (createOutput(outFmtCtx)) {
        cleanup();
        return -1;
    }
    // 映射 输入码流 -> 输出码流
    if (setupRemuxOrEncodeStreams(inFmtCtx, outFmtCtx)) {
        cleanup();
        return -1;
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

    // 开始主循环
    if (!audioCodec_.empty() || !videoCodec_.empty()) {
        // 转码
        if (err = transcodeLoop(inFmtCtx, outFmtCtx); err) {
            std::cout << "转码失败" << std::endl;
            cleanup();
            return -1;
        }
    } else {
        running_ = true;
        threads_.emplace_back(&NbFFmpeg::readThread, this, inFmtCtx);
        threads_.emplace_back(&NbFFmpeg::forwardThread, this, inFmtCtx, outFmtCtx);
        threads_.emplace_back(&NbFFmpeg::writeThread, this, outFmtCtx);
    }

    threadStatus_.decodeDone = true;
    threadStatus_.encodeDone = true;
    waitUntilFinished();

    av_write_trailer(outFmtCtx);
    cleanup();

    std::cout << "NbFFmpeg quit\n";
    return err.code();
}

int NbFFmpeg::setupEncoder(int inIndex, AVStream *inStream, AVStream *outStream, bool isAudio) {
    StreamContext ctx{};

    // 1. 创建解码器上下文
    const AVCodec *decoderCodec = avcodec_find_decoder(inStream->codecpar->codec_id);
    if (!decoderCodec) {
        std::cout << "找不到解码器（输入流）: stream " << inIndex << std::endl;
        return -1;
    }

    ctx.decoder = avcodec_alloc_context3(decoderCodec);
    if (!ctx.decoder) {
        std::cout << "无法分配解码器上下文: stream " << inIndex << std::endl;
        return -1;
    }

    if (avcodec_parameters_to_context(ctx.decoder, inStream->codecpar) < 0) {
        std::cout << "无法将参数复制到解码器上下文: stream " << inIndex << std::endl;
        return -1;
    }

    if (avcodec_open2(ctx.decoder, decoderCodec, nullptr) < 0) {
        std::cout << "打开解码器失败: stream " << inIndex << std::endl;
        return -1;
    }

    // 2. 查找编码器（由用户指定）
    std::string codecName = isAudio ? audioCodec_ : videoCodec_;
    const AVCodec *encoderCodec = avcodec_find_encoder_by_name(codecName.c_str());
    if (!encoderCodec) {
        std::cout << "找不到编码器: " << codecName << "，尝试用AVCodecID再查找" << std::endl;

        AVCodecID codecID = getAVCodecID(codecName);
        if (codecID != AV_CODEC_ID_NONE) {
            encoderCodec = avcodec_find_encoder(codecID);
        }
        if (!encoderCodec) {
            std::cout << "还是找不到编码器: " << codecName << std::endl;
            return -1;
        }
    }

    ctx.encoder = avcodec_alloc_context3(encoderCodec);
    if (!ctx.encoder) {
        std::cout << "无法分配编码器上下文: " << codecName << std::endl;
        return -1;
    }

    if (isAudio) {
        // ------------------ 音频编码器设置 ------------------
        ctx.encoder->sample_rate = ctx.decoder->sample_rate;
        ctx.encoder->channel_layout = ctx.decoder->channel_layout;
        ctx.encoder->channels = ctx.decoder->channels;
        ctx.encoder->sample_fmt = encoderCodec->sample_fmts ? encoderCodec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        ctx.encoder->time_base = {1, ctx.encoder->sample_rate};
        ctx.encoder->bit_rate = 128000; // 可配置的比特率
    } else {
        // ------------------ 视频编码器设置 ------------------
        ctx.encoder->width = ctx.decoder->width;
        ctx.encoder->height = ctx.decoder->height;
        ctx.encoder->pix_fmt = encoderCodec->pix_fmts ? encoderCodec->pix_fmts[0] : AV_PIX_FMT_YUV420P;
        ctx.encoder->time_base = av_inv_q(inStream->r_frame_rate); // 输入帧率的倒数
        ctx.encoder->framerate = inStream->r_frame_rate;

        ctx.encoder->bit_rate = 2 * 1000 * 1000; // 默认 2Mbps，可调整

        // 编码优化参数（如使用 libx264）
        if (codecName == "libx264" || codecName == "libx265") {
            av_opt_set(ctx.encoder->priv_data, "preset", "ultrafast", 0); // 可选：ultrafast, veryfast, medium...
        }

        // 可设置 GOP（关键帧间隔）
        ctx.encoder->gop_size = 12;
        ctx.encoder->max_b_frames = 2;
    }

    // 4. 打开编码器
    if (avcodec_open2(ctx.encoder, encoderCodec, nullptr) < 0) {
        std::cout << "打开编码器失败: " << codecName << std::endl;
        return -1;
    }

    // 5. 从 encoder 中填充输出流参数
    if (avcodec_parameters_from_context(outStream->codecpar, ctx.encoder) < 0) {
        std::cout << "无法从编码器设置输出流参数" << std::endl;
        return -1;
    }

    outStream->time_base = ctx.encoder->time_base;

    // 6. 保存上下文
    streamCtxMap_[inIndex] = ctx;

    std::cout << "成功为 " << (isAudio ? "音频" : "视频") << "流设置转码器: " << codecName << std::endl;
    return 0;
}

int NbFFmpeg::transcodeLoop(AVFormatContext *inFmtCtx, AVFormatContext *outFmtCtx) {
    errorMsg err;
    packet pkt;
    frame frm;

    while (true) {
        // 1. 读入一个 AVPacket
        err = av_read_frame(inFmtCtx, pkt.get());
        if (err.code() == AVERROR_EOF) {
            break; // 所有帧读完
        }
        if (err) {
            std::cout << "读取帧失败: " << err << std::endl;
            return err.code();
        }

        int inIndex = pkt->stream_index;

        // 2. 跳过未映射的流
        if (streamCtxMap_.find(inIndex) == streamCtxMap_.end()) {
            pkt.clear();
            continue;
        }

        StreamContext &ctx = streamCtxMap_[inIndex];
        AVStream *inStream = inFmtCtx->streams[inIndex];
        AVStream *outStream = outFmtCtx->streams[streamMapping_[inIndex]];

        // 3. 回调处理
        if (onInputPacket_) onInputPacket_(pkt);

        // 4. 解码
        err = avcodec_send_packet(ctx.decoder, pkt.get());
        if (err) {
            std::cout << "解码失败 send_packet: " << err << std::endl;
            return err.code();
        }

        while (true) {
            err = avcodec_receive_frame(ctx.decoder, frm.get());
            if (err.code() == AVERROR(EAGAIN) || err.code() == AVERROR_EOF)
                break;
            if (err) {
                std::cout << "解码失败 receive_frame: " << err << std::endl;
                return err.code();
            }

            // 5. 编码
            err = avcodec_send_frame(ctx.encoder, frm.get());
            if (err) {
                std::cout << "编码失败 send_frame: " << err << std::endl;
                return err.code();
            }

            while (true) {
                packet outPkt;
                err = avcodec_receive_packet(ctx.encoder, outPkt.get());
                if (err.code() == AVERROR(EAGAIN) || err.code() == AVERROR_EOF) {
                    err = 0;
                    break;
                }
                if (err) {
                    std::cout << "编码失败 receive_packet: " << err << std::endl;
                    return err.code();
                }

                outPkt->stream_index = streamMapping_[inIndex];
                av_packet_rescale_ts(outPkt.get(), ctx.encoder->time_base, outStream->time_base);

                if (onOutputPacket_) onOutputPacket_(outPkt);
                err = av_interleaved_write_frame(outFmtCtx, outPkt.get());
                if (err) {
                    std::cout << "写入帧失败: " << err << std::endl;
                    return err.code();
                }
            }

            frm.clear();
        }

        pkt.clear();
    }

    // 6. 刷新所有编码器缓存
    for (auto &[inIndex, ctx]: streamCtxMap_) {
        err = avcodec_send_frame(ctx.encoder, nullptr); // NULL -> flush
        if (err) continue;

        while (true) {
            packet outPkt;
            err = avcodec_receive_packet(ctx.encoder, outPkt.get());
            if (err.code() == AVERROR_EOF || err.code() == AVERROR(EAGAIN)) {
                err = 0;
                break;
            }
            int outIndex = streamMapping_[inIndex];
            AVStream *outStream = outFmtCtx->streams[outIndex];

            av_packet_rescale_ts(outPkt.get(), ctx.encoder->time_base, outStream->time_base);
            outPkt->stream_index = outIndex;

            if (onOutputPacket_) onOutputPacket_(outPkt);
            av_interleaved_write_frame(outFmtCtx, outPkt.get());
        }
    }

    return err.code();
}

int NbFFmpeg::checkAndPrintUsage() {
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
        return 1;
    }

    return 0;
}

int NbFFmpeg::openInput(AVFormatContext *&inFmtCtx) {
    errorMsg err;

    err = avformat_open_input(&inFmtCtx, iUrl_.c_str(), nullptr, nullptr);
    if (err) {
        std::cout << "打开输入失败: " << err << std::endl;
        return err.code();
    }

    err = avformat_find_stream_info(inFmtCtx, nullptr);
    if (err) {
        std::cout << "查找输入流信息失败: " << err << std::endl;
        avformat_close_input(&inFmtCtx);
        return err.code();
    }

    av_dump_format(inFmtCtx, 0, iUrl_.c_str(), 0);
    return err.code();
}

int NbFFmpeg::createOutput(AVFormatContext *&outFmtCtx) {
    errorMsg err;

    if (format_.empty()) {
        err = avformat_alloc_output_context2(&outFmtCtx, nullptr, nullptr, oUrl_.c_str());
    } else {
        err = avformat_alloc_output_context2(&outFmtCtx, nullptr, format_.c_str(), oUrl_.c_str());
    }

    if (err) {
        std::cout << "创建输出上下文失败: " << err << std::endl;
        return err.code();
    }

    return err.code();
}

int NbFFmpeg::setupRemuxOrEncodeStreams(AVFormatContext *inFmtCtx, AVFormatContext *outFmtCtx) {
    errorMsg err;
    bool autoAudioOnly = (oFormat_ == "mp3" || oFormat_ == "aac" || oFormat_ == "m4a" || oFormat_ == "wav");
    bool autoVideoOnly = (oFormat_ == "h264" || oFormat_ == "h265" || oFormat_ == "hevc" || oFormat_ == "264" ||
                          oFormat_ == "265");

    for (unsigned int i = 0, outStreamIndex = 0; i < inFmtCtx->nb_streams; ++i) {
        AVStream *inStream = inFmtCtx->streams[i];

        if (autoAudioOnly && inStream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        if (autoVideoOnly && inStream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
            continue;

        AVStream *outStream = avformat_new_stream(outFmtCtx, nullptr);
        if (!outStream) {
            err = AVERROR_UNKNOWN;
            std::cout << "创建输出流失败: " << err << std::endl;
            return err.code();
        }

        // 如果用户设置了音频编码器，走 setupEncoder 流程
        if (!audioCodec_.empty() && inStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (err = setupEncoder(i, inStream, outStream, true); err) {
                std::cout << "设置音频编码器失败: stream " << i << std::endl;
                return -1;
            }
        } else if (!videoCodec_.empty() && inStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (err = setupEncoder(i, inStream, outStream, false); err) {
                std::cout << "设置音频编码器失败: stream " << i << std::endl;
                return -1;
            }
        } else {
            // 否则使用参数拷贝
            err = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
            if (err) {
                std::cout << "拷贝编码器参数失败: " << err << std::endl;
                return err.code();
            }
            outStream->codecpar->codec_tag = 0;
        }

        streamMapping_[i] = outStreamIndex++;
    }

    return 0;
}

AVCodecID NbFFmpeg::getAVCodecID(const std::string &name) {
    static const std::map<std::string, AVCodecID> codecMap = {
        {"aac", AV_CODEC_ID_AAC},
        {"libfdk_aac", AV_CODEC_ID_AAC},
        {"libmp3lame", AV_CODEC_ID_MP3},
        {"mp3", AV_CODEC_ID_MP3},
        {"libopus", AV_CODEC_ID_OPUS},
        {"opus", AV_CODEC_ID_OPUS},
        {"libvorbis", AV_CODEC_ID_VORBIS},
        {"vorbis", AV_CODEC_ID_VORBIS},
        {"libx264", AV_CODEC_ID_H264},
        {"h264", AV_CODEC_ID_H264},
        {"libx265", AV_CODEC_ID_HEVC},
        {"hevc", AV_CODEC_ID_HEVC},
        {"libvpx", AV_CODEC_ID_VP8},
        {"libvpx-vp9", AV_CODEC_ID_VP9},
        {"vp8", AV_CODEC_ID_VP8},
        {"vp9", AV_CODEC_ID_VP9},
        {"mpeg4", AV_CODEC_ID_MPEG4},
        {"h263", AV_CODEC_ID_H263},
        {"flac", AV_CODEC_ID_FLAC},
        {"alac", AV_CODEC_ID_ALAC},
        {"pcm_s16le", AV_CODEC_ID_PCM_S16LE},
        {"pcm_s16be", AV_CODEC_ID_PCM_S16BE},
        {"pcm_u8", AV_CODEC_ID_PCM_U8},
        {"pcm_f32le", AV_CODEC_ID_PCM_F32LE},
        {"pcm_f64le", AV_CODEC_ID_PCM_F64LE}
    };

    auto it = codecMap.find(name);
    if (it != codecMap.end()) {
        return it->second;
    } else {
        return AV_CODEC_ID_NONE; // 表示查找失败
    }
}

void NbFFmpeg::readThread(AVFormatContext *inFmtCtx) {
    errorMsg err;
    threadStatus_.readDone = false;

    while (running_) {
        packet pkt; // 注意: 每轮新建，避免持久占用 AVPacket*

        err = av_read_frame(inFmtCtx, pkt.get());
        if (err.code() == AVERROR_EOF) {
            std::cout << "读取结束（EOF）" << std::endl;
            break;
        }
        if (err) {
            std::cout << "读取帧失败: " << err << std::endl;
            break;
        }

        int inIndex = pkt->stream_index;
        if (streamMapping_.find(inIndex) == streamMapping_.end()) {
            // 无需处理的流，清除资源后跳过
            pkt.clear();
            continue;
        }

        // 外部回调
        if (onInputPacket_) {
            onInputPacket_(pkt);
        }

        // 控制队列最大容量
        // int waitCount = 0;
        // while (inputPacketQueue_.size_approx() >= maxQueueSize_) {
        //     if (++waitCount > 1000) {  // 最多等 10 毫秒（1000 × 10 微秒）
        //         std::cout << "等待太久，跳过帧" << std::endl;
        //         break;
        //     }
        //     std::this_thread::sleep_for(std::chrono::microseconds(10));
        // }

        inputPacketQueue_.enqueue(std::move(pkt));
    }

    threadStatus_.readDone = true;
}

void NbFFmpeg::forwardThread(AVFormatContext *inFmtCtx, AVFormatContext *outFmtCtx) {
    threadStatus_.forwardDone = false;

    while (running_ && (!threadStatus_.readDone
                        || writePacketQueue_.size_approx() > 0
                        || inputPacketQueue_.size_approx() > 0)) {
        packet pkt;

        if (!inputPacketQueue_.try_dequeue(pkt)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(11));
            continue;
        }

        int inIndex = pkt->stream_index;
        if (streamMapping_.find(inIndex) == streamMapping_.end()) {
            // 非目标流，跳过
            continue;
        }

        int outIndex = streamMapping_[inIndex];
        // 重标定时间戳
        av_packet_rescale_ts(pkt.get(),
                             inFmtCtx->streams[inIndex]->time_base,
                             outFmtCtx->streams[outIndex]->time_base);
        pkt->pos = -1;
        pkt->stream_index = outIndex;

        // 写队列满了就丢旧包（高实时策略）
        // int waitCount = 0;
        // while (writePacketQueue_.size_approx() >= maxQueueSize_) {
        //     if (++waitCount > 1000) {  // 最多等 10 毫秒（1000 × 10 微秒）
        //         std::cout << "等待太久，跳过帧" << std::endl;
        //         break;
        //     }
        //     std::this_thread::sleep_for(std::chrono::microseconds(10));
        // }

        writePacketQueue_.enqueue(std::move(pkt));
    }

    threadStatus_.forwardDone = true;
}


void NbFFmpeg::writeThread(AVFormatContext *outFmtCtx) {
    errorMsg err;
    threadStatus_.writeDone = false;

    while (running_ && (!threadStatus_.forwardDone
                        || writePacketQueue_.size_approx() > 0)) {
        packet pkt;

        if (!writePacketQueue_.try_dequeue(pkt)) {
            std::this_thread::sleep_for(std::chrono::microseconds(5));
            continue;
        }

        if (onOutputPacket_)
            onOutputPacket_(pkt);

        err = av_interleaved_write_frame(outFmtCtx, pkt.get());
        if (err.code() == AVERROR(EAGAIN)) {
            std::this_thread::sleep_for(std::chrono::microseconds(5));
        } else if (err) {
            std::cout << "写入帧失败: " << err << std::endl;
            break;
        }
    }

    threadStatus_.writeDone = true;
}


void NbFFmpeg::forceStop() {
    running_ = false;
    for (auto &t: threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

void NbFFmpeg::waitUntilFinished() {
    while (!threadStatus_.allDone()) {
        std::this_thread::sleep_for(std::chrono::microseconds(20));
    }
    forceStop();
}
