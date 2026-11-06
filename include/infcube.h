#pragma once

#include <renderer/entity/renderer_entity_manager.h>
#include <renderer/renderer_camera.h>
#include <renderer/renderer_world.h>
#include <renderer/held_block.h>
#include <gui/screen_hud.h>
#include <gui/screen.h>
#include <gui/progress_bar.h>
#include <world/world.h>
#include <gui/font.h>
#include <item/recipe/crafting_manager.h>
#include <player/hit_result.h>
#include <particle/particles.h>
#include <sound/sounds.h>
#include <model/models.h>
#include <player/gamemode/gamemode.h>

#include <util/timer.h>

#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>

#include <stdint.h>

typedef struct infcube_s {
    renderer_camera_t renderer;
    world_t *world;
    textures_t textures;
    entity_t player;
    game_settings_t settings;
    font_t font;
    renderer_world_t renderer_world;
    game_timer_t timer;
    hit_result_t hit_result;
    screen_t *current_screen;
    progress_bar_t progress_bar;
    particles_t particles;
    gamemode_t gamemode;
    sounds_t sounds;
    models_t models;
    renderer_entity_manager_t entity_manager;
    crafting_manager_t crafting_manager;
    uint8_t fullscreen;
    uint8_t has_mouse;
    uint8_t pending_mouse_button_up;
    char *debug;
    int fps;
    int last_click;
    int32_t width;
    int32_t frame_width;
    int32_t height;
    int32_t frame_height;
    uint8_t running;
    uint64_t ticks;
    uint8_t raining;
    uint8_t is_paused;
    int miss_time;
    screen_hud_t hud;
    SDL_Window *window;
    SDL_GLContext context;
    char *game_dir;
} infcube_t;

void infcube_start_world(infcube_t *infcube, char *world_name);
void infcube_set_world(infcube_t *infcube, world_t *world, char *progress_text);
void infcube_grab_mouse(infcube_t *infcube);
void infcube_set_current_screen(infcube_t *infcube, screen_t *screen);
void infcube_pause(infcube_t *infcube);
void infcube_regenerate_world(infcube_t *infcube, int size);