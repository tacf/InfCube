#pragma once

#include <world/block/block.h>

#include <stdint.h>

typedef struct {
    struct infcube_s *infcube;
    block_t *block;
    float position;
    float last_position;
    int offset;
    uint8_t moving;
} held_block_t;