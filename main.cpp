#include <iostream>

#include "nbFFmpeg.h"

// "C:\\Users\\wang\\Videos\\rain.mp4"
int main() {
    return NbFFmpeg()
            .input("C:\\Users\\wang\\Videos\\test.mp4")
            .output("out.mp4")
            // .output("out.flv")
            // .output("out.mp3")
            // .output("out.aac")
            // .output("out.h264")
            // .onInput([](const packet &pkt) { printf("Got input packet.\n"); })
            // .onOutput([](const packet &pkt) { printf("Wrote packet.\n"); })
            // .audioCodec("aac")
            // .videoCodec("h264")
            .run();
}
