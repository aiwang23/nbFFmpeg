//
// Created by wang on 25-5-21.
//

#ifndef TYPES_H
#define TYPES_H
#include <iostream>
#include <string>

extern "C" {
#include <libavutil/frame.h>
#include <libavcodec/packet.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include  <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

class errorMsg {
public:
    errorMsg() = default;

    // 构造函数
    explicit errorMsg(int code) : code_(code) {
        if (code < 0) {
            av_strerror(code, msg_, sizeof(msg_));
        } else {
            msg_[0] = '\0';
        }
    }

    explicit errorMsg(std::string msg, int code = -1) {
        msg.resize(64);
        std::memcpy(msg_, msg.c_str(), msg.size());
        code_ = code;
    }

    // 移动构造函数
    errorMsg(errorMsg &&other) noexcept
        : code_(other.code_) {
        std::memcpy(msg_, other.msg_, sizeof(msg_));
        other.msg_[0] = '\0'; // 可选：清空源对象的字符串
        other.code_ = 0; // 可选：重置状态
    }

    // 删除拷贝构造和拷贝赋值（可选，如果你只想支持移动）
    errorMsg(const errorMsg &) = delete;

    errorMsg &operator=(const errorMsg &) = delete;

    // 移动赋值运算符
    errorMsg &operator=(errorMsg &&other) noexcept {
        if (this != &other) {
            code_ = other.code_;
            std::memcpy(msg_, other.msg_, sizeof(msg_));
            other.msg_[0] = '\0';
            other.code_ = 0;
        }
        return *this;
    }


    // 成员函数
    std::string msg() const {
        return msg_;
    }

    bool isFailed() const {
        return code_ < 0;
    }

    int code() const {
        return code_;
    }

    operator std::string() const {
        return msg();
    }

    operator bool() const {
        return isFailed();
    }

    errorMsg &operator=(int code) {
        code_ = code;
        if (code < 0) {
            av_strerror(code, msg_, sizeof(msg_));
        } else {
            msg_[0] = '\0'; // 成功时清空消息
        }
        return *this;
    }

    void dump() const {
        fprintf(stderr, "code: %d, msg: %s\n", code_, msg_);
    }

    void clear() {
        code_ = 0;
        msg_[0] = '\0';
    }

private:
    char msg_[AV_ERROR_MAX_STRING_SIZE] = {0};
    int code_ = 0;
};


class packet {
public:
    // 默认构造：分配新 AVPacket
    explicit packet() {
        pkt_ = av_packet_alloc();
    }

    // 构造：包裹已有 AVPacket（不接管所有权）
    explicit packet(AVPacket *pkt) : pkt_(pkt) {
    }

    // 析构函数：释放 AVPacket（仅当我们拥有它）
    ~packet() {
        if (own_ && pkt_) {
            av_packet_free(&pkt_);
        }
    }

    // 拷贝构造（深拷贝）
    packet(const packet &other) {
        pkt_ = av_packet_alloc();
        av_packet_ref(pkt_, other.pkt_);
    }

    // 拷贝赋值（深拷贝）
    packet &operator=(const packet &other) {
        if (this != &other) {
            if (!pkt_) {
                pkt_ = av_packet_alloc();
            } else {
                av_packet_unref(pkt_);
            }
            av_packet_ref(pkt_, other.pkt_);
        }
        return *this;
    }

    // 移动构造
    packet(packet &&other) noexcept {
        pkt_ = other.pkt_;
        own_ = other.own_;
        other.pkt_ = nullptr;
        other.own_ = false;
    }

    // 移动赋值
    packet &operator=(packet &&other) noexcept {
        if (this != &other) {
            if (own_ && pkt_) {
                av_packet_free(&pkt_);
            }

            pkt_ = other.pkt_;
            own_ = other.own_;
            other.pkt_ = nullptr;
            other.own_ = false;
        }
        return *this;
    }

    // 获取裸指针（只读）
    const AVPacket *get() const {
        return pkt_;
    }

    AVPacket *get() {
        return pkt_;
    }

    // 直接访问 operator-> 支持 pkt->pts 这种写法
    AVPacket *operator->() {
        return pkt_;
    }

    const AVPacket *operator->() const {
        return pkt_;
    }

    // 是否有效
    bool isValid() const {
        return pkt_ != nullptr;
    }

    // 清除内容但保留结构体
    void clear() {
        if (pkt_) {
            av_packet_unref(pkt_);
        }
    }

private:
    AVPacket *pkt_ = nullptr;
    bool own_ = true; // 是否我们自己分配的，决定是否负责释放
};

class frame {
public:
    // 默认构造：分配新 AVFrame
    frame() {
        frm_ = av_frame_alloc();
    }

    // 析构：释放 AVFrame
    ~frame() {
        if (frm_) {
            av_frame_free(&frm_);
        }
    }

    void allocBuffer(AVPixelFormat fmt, int w, int h) {
        av_frame_unref(frm_);
        frm_->format = fmt;
        frm_->width = w;
        frm_->height = h;
        av_frame_get_buffer(frm_, 32);
    }

    // 拷贝构造：深拷贝
    frame(const frame &other) {
        frm_ = av_frame_alloc();
        if (frm_ && other.frm_) {
            av_frame_ref(frm_, other.frm_);
        }
    }

    // 拷贝赋值：深拷贝
    frame &operator=(const frame &other) {
        if (this != &other) {
            if (!frm_) {
                frm_ = av_frame_alloc();
            } else {
                av_frame_unref(frm_);
            }
            if (other.frm_) {
                av_frame_ref(frm_, other.frm_);
            }
        }
        return *this;
    }

    // 移动构造
    frame(frame &&other) noexcept {
        frm_ = other.frm_;
        other.frm_ = nullptr;
    }

    // 移动赋值
    frame &operator=(frame &&other) noexcept {
        if (this != &other) {
            if (frm_) {
                av_frame_free(&frm_);
            }
            frm_ = other.frm_;
            other.frm_ = nullptr;
        }
        return *this;
    }

    // 获取裸指针（只读）
    AVFrame *get() const {
        return frm_;
    }

    // 操作符 -> 支持 frm->pts
    AVFrame *operator->() {
        return frm_;
    }

    const AVFrame *operator->() const {
        return frm_;
    }

    // 判断是否有效
    bool isValid() const {
        return frm_ != nullptr;
    }

    // 清除内容但不释放结构
    void clear() {
        if (frm_) {
            av_frame_unref(frm_);
        }
    }

private:
    AVFrame *frm_ = nullptr;
};

#endif //TYPES_H
