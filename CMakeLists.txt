cmake_minimum_required(VERSION 3.31)
project(nbFFmpeg)

set(CMAKE_CXX_STANDARD 17)

add_library(nbFFmpeg
        nbFFmpeg.cpp
        nbFFmpeg.h
        types.h
        concurrentqueue.h
)

target_include_directories(nbFFmpeg PUBLIC
        "C:\\Soft\\FFmpeg\\ffmpeg-n6.1-latest-win64-lgpl-shared-6.1\\include"
)

target_link_directories(nbFFmpeg PUBLIC
        "C:\\Soft\\FFmpeg\\ffmpeg-n6.1-latest-win64-lgpl-shared-6.1\\lib"
)

target_link_libraries(nbFFmpeg PUBLIC
        # ffmpeg
        avformat
        avcodec
        avdevice
        avfilter
        swresample
        swscale
        avutil
)

# test
add_executable(nbFFmpeg_test main.cpp)

target_link_libraries(nbFFmpeg_test
        nbFFmpeg
)