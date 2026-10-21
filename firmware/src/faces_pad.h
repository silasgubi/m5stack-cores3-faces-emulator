#pragma once
#include <stdint.h>

enum FacesBit : uint8_t {
    FACES_UP = 1 << 0,
    FACES_DOWN = 1 << 1,
    FACES_LEFT = 1 << 2,
    FACES_RIGHT = 1 << 3,
    FACES_A = 1 << 4,
    FACES_B = 1 << 5,
    FACES_SELECT = 1 << 6,
    FACES_START = 1 << 7,
};

bool facesPadInit();
// Buttons pressed right now (bit set = pressed).
uint8_t facesPadRead();
