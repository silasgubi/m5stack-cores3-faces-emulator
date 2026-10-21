// Nofrendo controller input from the Faces Gamepad.
// Format expected by osd_getinput (cgs/nes_osd.c): active-low bits,
// 0 up, 1 down, 2 left, 3 right, 4 select, 5 start, 6 A, 7 B.

#include "faces_pad.h"

extern "C" {

void controller_init() {}

uint32_t controller_read_input() {
    const uint8_t p = facesPadRead();
    uint32_t v = 0xFFFFFFFF;
    if (p & FACES_UP) v ^= 1 << 0;
    if (p & FACES_DOWN) v ^= 1 << 1;
    if (p & FACES_LEFT) v ^= 1 << 2;
    if (p & FACES_RIGHT) v ^= 1 << 3;
    if (p & FACES_SELECT) v ^= 1 << 4;
    if (p & FACES_START) v ^= 1 << 5;
    if (p & FACES_A) v ^= 1 << 6;
    if (p & FACES_B) v ^= 1 << 7;
    return v;
}

}  // extern "C"
