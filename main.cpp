#include <iostream>

#include "nbFFmpeg.h"

// "C:\\Users\\wang\\Videos\\rain.mp4"
int main() {
    return NbFFmpeg()
            .input("C:\\Users\\wang\\Videos\\rain.mp4")
            // .output("out.mp4")
            // .output("out.aac")
            .output("out.h264")
            // .onInput([](const packet &pkt) { printf("Got input packet.\n"); })
            // .onOutput([](const packet &pkt) { printf("Wrote packet.\n"); })
            .audioCodec("aac")
            .videoCodec("libx264")
            .run();
}
