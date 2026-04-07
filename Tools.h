//
// Created by wang on 2026/4/4.
//

#ifndef NBFFMPEG_TOOLS_H
#define NBFFMPEG_TOOLS_H
#include <fmt/base.h>

#include "concurrentqueue.h"


extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
}

inline int check_error_impl(int code,
                            const char* func,
                            const char* file,
                            int line,
                            std::string* out_msg = nullptr) {
    if (code < 0 && code != AVERROR(EAGAIN)) {
        char err_msg[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(code, err_msg, sizeof(err_msg));

        if (out_msg) {
            *out_msg = err_msg;
        }

        fmt::println(stderr, "[ERROR] {}:{}\nfunc: {}\ncode: {}\nmsg : {}\n",
                     file, line, func, code, err_msg);
    }
    return code;
}

#define CHECK(func) \
check_error_impl((func), #func, __FILE__, __LINE__)

#define CHECK_MSG(func, msg) \
check_error_impl((func), #func, __FILE__, __LINE__, &(msg))

#define CHECK_CODE(func, code) \
((code) = check_error_impl((func), #func, __FILE__, __LINE__))

#define CHECK_MSG_CODE(func, msg, code) \
((code) = check_error_impl((func), #func, __FILE__, __LINE__, &(msg)))

#define TRY(func) \
do { \
    int _code = check_error_impl((func), #func, __FILE__, __LINE__); \
    if (_code < 0) return _code; \
} while (0)

#define TRY_CODE(func, code) \
do { \
    code = check_error_impl((func), #func, __FILE__, __LINE__); \
    if (code < 0) return code; \
} while (0)

// 专门为 av_read_frame 设计的宏
#define TRY_READ(func) \
do { \
    int code = (func); \
    if (code < 0) { \
        if (code == AVERROR(EAGAIN)) continue; \
    check_error_impl(code, #func, __FILE__, __LINE__); \
    return code; \
    } \
} while (0)

// 专门为 av_read_frame 设计的宏
#define TRY_READ_CODE(func, code) \
do { \
    code = (func); \
    if (code < 0) { \
        if (code == AVERROR(EAGAIN)) continue; \
    check_error_impl(code, #func, __FILE__, __LINE__); \
    return code; \
    } \
} while (0)

struct DictGuard {
    AVDictionary* ptr = nullptr;
    ~DictGuard() { if (ptr) av_dict_free(&ptr); }
    // 禁用拷贝以防双重释放
    DictGuard(const DictGuard&) = delete;
    DictGuard& operator=(const DictGuard&) = delete;
};

inline auto AVFormatContextCloser = [](AVFormatContext* s) { if (s) avformat_close_input(&s); };
using AVFormatContextPtr = std::unique_ptr<AVFormatContext, decltype(AVFormatContextCloser)>;

inline auto AVCodecContextCloser = [](AVCodecContext* c) { if (c) avcodec_free_context(&c); };
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, decltype(AVCodecContextCloser)>;

using AVPacketSharedPtr = std::shared_ptr<AVPacket>;
using AVFrameSharedPtr = std::shared_ptr<AVFrame>;
using PacketQueue = moodycamel::ConcurrentQueue<AVPacketSharedPtr>;
using FrameQueue = moodycamel::ConcurrentQueue<AVFrameSharedPtr>;

#endif //NBFFMPEG_TOOLS_H
