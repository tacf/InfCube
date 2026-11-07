#pragma once

#include <util/random.h>

struct infcube_s;

typedef struct {
    //chat line
    random_t random;
    struct infcube_s *infcube;
    int width;
    int height;
    //const char *hovered_player;
    int ticks;
} screen_hud_t;

screen_hud_t screen_hud_create(struct infcube_s *infcube, int width, int height);
void screen_hud_render(screen_hud_t *hud, float mx, float my, float partial_tick);
void screen_hud_destroy(screen_hud_t *hud);