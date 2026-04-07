cmake_minimum_required(VERSION 3.31)
project(nbFFmpeg)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(3rd/concurrentqueue)
add_subdirectory(3rd/fmt)

find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED
        libavcodec
        libavformat
        libavutil
        libswscale
        libswresample
        libavdevice
)

add_executable(nbFFmpeg main.cpp
        Tools.h
        RecordNode.cpp
        RecordNode.h
        EncodeNode.cpp
        EncodeNode.h
)

target_include_directories(nbFFmpeg PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(nbFFmpeg PRIVATE ${FFMPEG_LIBRARIES})

target_link_libraries(nbFFmpeg PRIVATE concurrentqueue fmt)