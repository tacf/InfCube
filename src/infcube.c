#include <infcube.h>

#include <session_data.h>
#include <renderer/renderer_camera.h>
#include <renderer/tesselator.h>
#include <renderer/frustum.h>
#include <renderer/renderer_block.h>
#include <renderer/texture/texture_fire.h>
#include <renderer/texture/texture_gears.h>
#include <renderer/texture/texture_lava.h>
#include <renderer/texture/texture_water.h>
#include <renderer/texture/texture_water_flow.h>
#include <world/block/blocks.h>
#include <world/world.h>
#include <gui/screen_pause.h>
#include <gui/screen_block_select.h>
#include <gui/screen_death.h>
#include <gui/screen_title.h>
#include <gui/container/screen_inventory.h>
#include <particle/particle_water_drop.h>
#include <model/models.h>
#include <player/gamemode/gamemode_creative.h>
#include <player/gamemode/gamemode_survival.h>
#include <entity/entity_item.h>
#include <entity/entity_arrow.h>
#include <item/items.h>

#include <util/time.h>
#include <util/array_list.h>
#include <util/math_helper.h>

#include <GL/gl.h>
#include <GL/glu.h>

//gets rid of annoying warning for windows static linking
extern void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);

#include <stdio.h>

void infcube_create(infcube_t *infcube, uint16_t width, uint16_t height, uint8_t fullscreen) {
    infcube->fullscreen = fullscreen;
    infcube->width = width;
    infcube->height = height;
    infcube->running = 0;
    infcube->ticks = 0;
    infcube->has_mouse = 0;
    infcube->pending_mouse_button_up = 0;
    infcube->hit_result = (hit_result_t){ .null = 1 };
    infcube->last_click = 0;
    infcube->raining = 0;

    if(SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        printf("Couldn't init SDL2: %s\n", SDL_GetError());
        exit(1);
    }
    int window_width = width;
    int window_height = height;
    SDL_Rect usable_bounds;
    if(SDL_GetDisplayUsableBounds(0, &usable_bounds) == 0) {
        int max_w = usable_bounds.w;
        int max_h = usable_bounds.h;
        if(window_width > max_w || window_height > max_h) {
            float scale_w = (float)max_w / (float)window_width;
            float scale_h = (float)max_h / (float)window_height;
            float scale = scale_w < scale_h ? scale_w : scale_h;
            window_width = (int)((float)window_width * scale);
            window_height = (int)((float)window_height * scale);
        }
        if(window_width >= max_w) window_width = max_w - SDL_max(64, max_w / 10);
        if(window_height >= max_h) window_height = max_h - SDL_max(64, max_h / 10);
    } else {
        SDL_DisplayMode desktop_mode;
        if(SDL_GetDesktopDisplayMode(0, &desktop_mode) == 0) {
            int max_w = desktop_mode.w;
            int max_h = desktop_mode.h;
            if(window_width > max_w || window_height > max_h) {
                float scale_w = (float)max_w / (float)window_width;
                float scale_h = (float)max_h / (float)window_height;
                float scale = scale_w < scale_h ? scale_w : scale_h;
                window_width = (int)((float)window_width * scale);
                window_height = (int)((float)window_height * scale);
            }
            if(window_width >= max_w) window_width = max_w - SDL_max(64, max_w / 10);
            if(window_height >= max_h) window_height = max_h - SDL_max(64, max_h / 10);
        }
    }
    if(window_width < 640) window_width = 640;
    if(window_height < 480) window_height = 480;

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
    if(fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    infcube->window = SDL_CreateWindow("InfCube", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, flags);
    if(!infcube->window) {
        printf("Couldn't create window: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_GetWindowSize(infcube->window, &infcube->width, &infcube->height);
    SDL_GL_GetDrawableSize(infcube->window, &infcube->frame_width, &infcube->frame_height);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    infcube->context = SDL_GL_CreateContext(infcube->window);
    if(!infcube->context) {
        printf("Couldn't create window context: %s\n", SDL_GetError());
        exit(1);
    }

    glEnable(GL_TEXTURE_2D);
    glShadeModel(GL_SMOOTH);
    glClearDepth(1.0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0);
    glCullFace(GL_BACK);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);

    math_helper_initialize();
    materials_init();
    items_init();
    blocks_init();
    session_data_initialize();
    item_models_init();
    tesselator_create(&g_tesselator);
    infcube->crafting_manager = crafting_manager_create();
    infcube->debug = string_create("");
    infcube->sounds = sounds_create();
    infcube->timer = timer_create(20);
    infcube->progress_bar = progress_bar_create(infcube);
    infcube->models = models_create();
    infcube->game_dir = string_create("./.infcube");
#ifdef SURVIVAL_MODE
    infcube->gamemode = gamemode_survival_create(infcube);
#else
    infcube->gamemode = gamemode_creative_create(infcube);
#endif
    renderer_entity_manager_create(&infcube->entity_manager, infcube);

    game_settings_create(&infcube->settings, (struct infcube_s *)infcube);
    SDL_GL_SetSwapInterval(infcube->settings.limit_framerate ? 1 : 0);
    infcube->textures = textures_create(&infcube->settings);

    texture_animated_t *lava_texture = malloc(sizeof(texture_animated_t));
    *lava_texture = texture_lava_create();
    textures_register_animation(&infcube->textures, lava_texture);
    texture_animated_t *water_texture = malloc(sizeof(texture_animated_t));
    *water_texture = texture_water_create();
    textures_register_animation(&infcube->textures, water_texture);
    texture_animated_t *flowing_texture = malloc(sizeof(texture_animated_t));
    *flowing_texture = texture_water_flow_create();
    textures_register_animation(&infcube->textures, flowing_texture);
    texture_animated_t *fire_texture = malloc(sizeof(texture_animated_t));
    *fire_texture = texture_fire_create(0);
    textures_register_animation(&infcube->textures, fire_texture);
    texture_animated_t *fire1_texture = malloc(sizeof(texture_animated_t));
    *fire1_texture = texture_fire_create(1);
    textures_register_animation(&infcube->textures, fire1_texture);
    texture_animated_t *gears_texture = malloc(sizeof(texture_animated_t));
    *gears_texture = texture_gears_create(0);
    textures_register_animation(&infcube->textures, gears_texture);
    texture_animated_t *gears1_texture = malloc(sizeof(texture_animated_t));
    *gears1_texture = texture_gears_create(1);
    textures_register_animation(&infcube->textures, gears1_texture);

    infcube->font = font_create(&infcube->settings, "default.png", &infcube->textures);

    glViewport(0, 0, infcube->frame_width, infcube->frame_height);

    infcube->renderer = renderer_camera_create(infcube);

    //infcube->gamemode.init_player(&infcube->gamemode, &infcube->player);
    //infcube->gamemode.adjust_player(&infcube->gamemode, &infcube->player);

    renderer_world_create(&infcube->renderer_world, infcube, infcube->world, &infcube->textures);
    //infcube->world->renderer = (struct renderer_world_s *)&infcube->renderer_world;
    //renderer_world_refresh((renderer_world_t *)&infcube->renderer_world);

    int x = infcube->width;
    int y = infcube->height;
    int w = x;
    int h = y;
    for(x = 1; w / (x + 1) >= 320 && h / (x + 1) >= 240; x++);
    w /= x;
    h /= x;
    infcube->hud = screen_hud_create((struct infcube_s *)infcube, w, h);
    //infcube_grab_mouse(infcube);
    if(infcube->world == NULL) {
        screen_t *main_menu = malloc(sizeof(screen_t));
        *main_menu = screen_title_create();
        infcube_set_current_screen(infcube, main_menu);
    }
}

void infcube_prepare_world(infcube_t *infcube, char *progress_text) {
    progress_bar_set_title(&infcube->progress_bar, progress_text);
    progress_bar_set_text(&infcube->progress_bar, "Preparing chunks");

    for(int i = -196; i <= 196; i += 16) {
        progress_bar_set_progress(&infcube->progress_bar, (i + 196) * 100 / 392);
        int x = infcube->world->spawn_x;
        int z = infcube->world->spawn_z;
        if(infcube->world->player != NULL) {
            x = infcube->world->player->x;
            z = infcube->world->player->z;
        }
        for(int j = -196; j <= 196; j += 16) {
            world_get_block(infcube->world, i + x, 64, j + z);
        }
    }
}

void infcube_set_world(infcube_t *infcube, world_t *world, char *progress_text) {
    if(infcube->world != NULL) {
        world_save(infcube->world, 1);

        if(infcube->renderer_world.infcube != NULL) {
            renderer_world_change_world(&infcube->renderer_world, NULL);
        }

        if(infcube->particles.textures != NULL) {
            particles_destroy(&infcube->particles);
            infcube->particles = (particles_t){ 0 };
        }

        world_destroy(infcube->world);
        free(infcube->world);
    }

    infcube->world = world;
    if(world != NULL) {
        infcube->renderer.renderer_overlay.world = world;
        infcube->renderer.renderer_overlay.renderer_block = renderer_block_create(world);
        memset(&infcube->player, 0, sizeof(entity_t));
        world->player = &infcube->player;
        infcube_prepare_world(infcube, progress_text);
        // cheap way to check if player exists
        if(infcube->player.write_nbt == NULL) {
            player_create(&infcube->player, infcube->world);
        }
        infcube->player.mob->player->inputs = inputs_create(&infcube->settings);

        if(infcube->renderer_world.infcube != NULL) {
            renderer_world_change_world(&infcube->renderer_world, world);
        }

        if(infcube->particles.textures != NULL) {
            // clear particles
            infcube->world->particles = &infcube->particles;
        }

        // on respawn
        world_spawn_player(world);
    }
}

void infcube_start_world(infcube_t *infcube, char *world_name) {
    infcube_set_world(infcube, NULL, world_name);
    world_t *world = malloc(sizeof(world_t));
    world_create(world, infcube, "./.infcube/saves", world_name, time(NULL));
    if(world->is_new_world) {
        infcube_set_world(infcube, world, "Generating level");
    } else {
        infcube_set_world(infcube, world, "Loading level");
    }

    progress_bar_set_text(&infcube->progress_bar, "Preparing lights");
    int progress = 0;
    while(array_list_length(infcube->world->lighting_update_list) > 0) {
        progress_bar_set_progress(&infcube->progress_bar, progress++ % 100);
        world_update_lighting(infcube->world);
    }
    infcube->particles = particles_create(infcube->world, &infcube->textures);
}

void infcube_grab_mouse(infcube_t *infcube) {
    if(!infcube->has_mouse) {
        infcube->has_mouse = 1;
        SDL_ShowCursor(0);
        SDL_WarpMouseInWindow(infcube->window, infcube->width / 2, infcube->height / 2);
        infcube->last_click = infcube->ticks + 10000;
    }
}

void infcube_set_current_screen(infcube_t *infcube, screen_t *screen) {
    if(infcube->current_screen != NULL && infcube->current_screen->type == SCREEN_TYPE_ERROR) {
        return;
    }
    if(infcube->current_screen != NULL) {
        //infcube->current_screen->on_close((struct screen_s *)infcube->current_screen);
    }
    if(screen == NULL && infcube->world == NULL) {
        screen = malloc(sizeof(screen_t));
        *screen = screen_title_create();
    } else if(screen == NULL && infcube->player.mob->health <= 0) {
        screen = malloc(sizeof(screen_t));
        *screen = screen_death_create();
    }
    if(screen != NULL) {
        infcube->pending_mouse_button_up = 0;
        infcube->current_screen = screen;
        if(infcube->has_mouse) {
            if(infcube->player.read_nbt != NULL) {
                inputs_reset_keys(&infcube->player.mob->player->inputs);
            }
            infcube->has_mouse = 0;
            SDL_ShowCursor(1);
        }
        int x = infcube->width;
        int y = infcube->height;
        int w = x;
        int h = y;
        for(x = 1; w / (x + 1) >= 320 && h / (x + 1) >= 240; x++);
        w /= x;
        h /= x;

        screen_open(screen, infcube, w, h);
    } else {
        infcube->current_screen = NULL;
        infcube->pending_mouse_button_up = 0;
        infcube_grab_mouse(infcube);
    }
}

void infcube_regenerate_world(infcube_t *infcube, int size) {
    //world_regenerate(infcube->world, size);
    infcube->gamemode.init_world(&infcube->gamemode, infcube->world);
    player_create(&infcube->player, (struct world_s *)infcube->world);
    entity_reset_pos(&infcube->player);
    infcube->gamemode.init_player(&infcube->gamemode, &infcube->player);
    infcube->gamemode.adjust_player(&infcube->gamemode, &infcube->player);
    infcube->player.mob->player->inputs = inputs_create(&infcube->settings);
    renderer_world_destroy(&infcube->renderer_world);
    //infcube->renderer_world = renderer_world_create(infcube, infcube->world, &infcube->textures);
    //infcube->world->renderer = (struct renderer_world_s *)&infcube->renderer_world;
    //renderer_world_refresh((renderer_world_t *)&infcube->renderer_world);
    particles_destroy(&infcube->particles);
    infcube->particles = particles_create(infcube->world, &infcube->textures);
}

void infcube_pause(infcube_t *infcube) {
    if(infcube->current_screen == NULL) {
        screen_t *screen = malloc(sizeof(screen_t));
        *screen = screen_pause_create((struct infcube_s *)infcube);
        infcube_set_current_screen(infcube, screen);
    }
}

void on_mouse_clicked(infcube_t *infcube, int button) {
    if(button != SDL_BUTTON_LEFT || infcube->miss_time <= 0) {
        if(button == SDL_BUTTON_LEFT) {
            infcube->renderer.renderer_overlay.swing_time = -1;
            infcube->renderer.renderer_overlay.is_swinging = 1;
        }

        if(button == SDL_BUTTON_RIGHT) {
            item_stack_t current_item = inventory_player_get_selected(&infcube->player.mob->player->inventory);
            if(current_item.item_id != 0) {
                int size = current_item.stack_size;
                player_t *player = infcube->player.mob->player;
                item_stack_t result_item = item_list[current_item.item_id].on_right_click(&current_item, infcube->world, player);
                if(result_item.stack_size != size || result_item.item_damage != current_item.item_damage || result_item.item_id != current_item.item_id) {
                    inventory_player_set_slot(&player->inventory, player->inventory.selected, result_item);
                    infcube->renderer.renderer_overlay.equipped_progress = 0;
                    if(result_item.stack_size == 0) {
                        item_stack_t null_item = item_stack_create(0, 0, 0);
                        inventory_player_set_slot(&player->inventory, player->inventory.selected, null_item);
                    }
                }
            }
        }

        if(infcube->hit_result.null) {
            if(button == SDL_BUTTON_LEFT && infcube->gamemode.gamemode_type != GAMEMODE_CREATIVE) {
                infcube->miss_time = 10;
            }
        } else {
            if(infcube->hit_result.type == 1) {
                if(button == SDL_BUTTON_LEFT) {
                    //entity_t *hit_entity = infcube->hit_result.entity;

                    // TODO entity damage

                    return;
                }
            } else if(infcube->hit_result.type == 0) {
                int hit_x = infcube->hit_result.x;
                int hit_y = infcube->hit_result.y;
                int hit_z = infcube->hit_result.z;
                int face = infcube->hit_result.face;
                if(button == SDL_BUTTON_LEFT) {
                    world_extinguish_fire(infcube->world, hit_x, hit_y, hit_z, face);
                    if(world_get_block(infcube->world, hit_x, hit_y, hit_z) != blocks.bedrock.id) {
                        infcube->gamemode.start_destroy_block(&infcube->gamemode, hit_x, hit_y, hit_z);
                        return;
                    }
                } else {
                    player_t *player = infcube->player.mob->player;
                    item_stack_t current_item = inventory_player_get_selected(&player->inventory);
                    uint8_t block_id = world_get_block(infcube->world, hit_x, hit_y, hit_z);
                    if(block_id > 0 && block_list[block_id].on_interacted(&block_list[block_id], infcube->world, hit_x, hit_y, hit_z, &infcube->player)) {
                        return;
                    }
                    if(current_item.item_id == 0) {
                        return;
                    }

                    int size = current_item.stack_size;
                    if(item_list[current_item.item_id].on_use(&item_list[current_item.item_id], &current_item, infcube->world, hit_x, hit_y, hit_z, face)) {
                        infcube->renderer.renderer_overlay.swing_time = -1;
                        infcube->renderer.renderer_overlay.is_swinging = 1;
                        inventory_player_set_slot(&player->inventory, player->inventory.selected, current_item);
                    }

                    if(current_item.stack_size == 0) {
                        item_stack_t null_item = item_stack_create(0, 0, 0);
                        inventory_player_set_slot(&player->inventory, player->inventory.selected, null_item);
                        return;
                    }

                    if(current_item.stack_size != size) {
                        infcube->renderer.renderer_overlay.equipped_progress = 0;
                    }
                }
            }
        }
    }
}

void infcube_tick(infcube_t *infcube, SDL_Event *events) {
    glBindTexture(GL_TEXTURE_2D, textures_load(&infcube->textures, "terrain.png"));
    if(!infcube->is_paused) {
        for(int i = 0; i < array_list_length(infcube->textures.animations); i++) {
            texture_animated_t *texture = infcube->textures.animations[i];
            texture->anaglyph = infcube->settings.anaglyph;
            texture->tick(texture);
            memcpy(infcube->textures.texture_buffer, texture->data, 1024);
            glTexSubImage2D(GL_TEXTURE_2D, 0, texture->texture_id % 16 << 4, texture->texture_id / 16 << 4, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, infcube->textures.texture_buffer);
        }
    }

    if(infcube->miss_time > 0) {
        infcube->miss_time--;
    }

    if(infcube->current_screen == NULL && infcube->player.mob->health <= 0) {
        infcube_set_current_screen(infcube, NULL);
    }

    for(int i = 0; i < array_list_length(events); i++) {
        if(infcube->pending_mouse_button_up != 0 && events[i].type == SDL_MOUSEBUTTONUP) {
            if(events[i].button.button == infcube->pending_mouse_button_up) {
                events[i] = (SDL_Event){ 0 };
                infcube->pending_mouse_button_up = 0;
            }
        }
    }

    if(infcube->current_screen == NULL || infcube->current_screen->grabs_mouse) {
        for(int i = 0; i < array_list_length(events); i++) {
            if(events[i].type == SDL_MOUSEWHEEL) {
                int slot = events[i].wheel.y;
                slot = slot > 0 ? 1 : (slot < 0 ? -1 : slot);
                for(infcube->player.mob->player->inventory.selected -= slot; infcube->player.mob->player->inventory.selected < 0; infcube->player.mob->player->inventory.selected += 9);
                while(infcube->player.mob->player->inventory.selected >= 9)
                    infcube->player.mob->player->inventory.selected -= 9;
            }
            if(infcube->current_screen == NULL) {
                if(!infcube->has_mouse && events[i].type == SDL_MOUSEBUTTONDOWN) {
                    infcube_grab_mouse(infcube);
                } else if(events[i].type == SDL_MOUSEBUTTONDOWN) {
                    if(events[i].button.button == SDL_BUTTON_LEFT) {
                        on_mouse_clicked(infcube, SDL_BUTTON_LEFT);
                        infcube->last_click = infcube->ticks;
                    }
                    if(events[i].button.button == SDL_BUTTON_RIGHT) {
                        screen_t *previous_screen = infcube->current_screen;
                        on_mouse_clicked(infcube, SDL_BUTTON_RIGHT);
                        infcube->last_click = infcube->ticks;
                        if(infcube->current_screen != NULL && infcube->current_screen != previous_screen) {
                            infcube->pending_mouse_button_up = SDL_BUTTON_RIGHT;
                            events[i] = (SDL_Event){ 0 };
                        }
                    }
                    if(events[i].button.button == SDL_BUTTON_MIDDLE && !infcube->hit_result.null) {
                        uint8_t block_id = world_get_block(infcube->world, infcube->hit_result.x, infcube->hit_result.y, infcube->hit_result.z);
                        if(block_id == blocks.grass.id) block_id = blocks.dirt.id;
                        if(block_id == blocks.double_slab.id) block_id = blocks.slab.id;
                        if(block_id == blocks.bedrock.id) block_id = blocks.stone.id;
                        inventory_player_get_hotbar_slot(&infcube->player.mob->player->inventory, block_id);
                    }
                }
            } else {
                screen_mouse_event(infcube->current_screen, events[i]);
            }
        }
        for(int i = 0; i < array_list_length(events); i++) {
            if(events[i].type == SDL_KEYDOWN || events[i].type == SDL_KEYUP) {
                player_set_key(infcube->player.mob->player, events[i].key.keysym.scancode, events[i].type == SDL_KEYDOWN);
            }
            if(events[i].type == SDL_KEYDOWN) {
                if(infcube->current_screen != NULL) {
                    screen_keyboard_event(infcube->current_screen, events[i]);
                } else {
                    if(events[i].key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                        infcube_pause(infcube);
                        events[i] = (SDL_Event){ 0 };
                    }
                    if(infcube->gamemode.gamemode_type == GAMEMODE_CREATIVE) {
                        if(events[i].key.keysym.scancode == infcube->settings.load_location_key.key) {
                            entity_reset_pos(&infcube->player);
                        }
                        if(events[i].key.keysym.scancode == infcube->settings.save_location_key.key) {
                            world_set_spawn_position(infcube->world, infcube->player.x, infcube->player.y, infcube->player.z);
                            entity_reset_pos(&infcube->player);
                        }
                    }
                    if(events[i].key.keysym.scancode == SDL_SCANCODE_F5) {
                        infcube->raining = !infcube->raining;
                    }
                    if(events[i].key.keysym.scancode == SDL_SCANCODE_TAB && infcube->gamemode.gamemode_type == GAMEMODE_SURVIVAL && infcube->player.mob->player->arrows > 0) {
                        /*entity_t *arrow = malloc(sizeof(entity_t));
                        entity_arrow_create(arrow, infcube->world, &infcube->player, infcube->player.x, infcube->player.y, infcube->player.z, infcube->player.y_rot, infcube->player.x_rot, 1.2);
                        world_spawn_entity(infcube->world, arrow);
                        infcube->player.mob->player->arrows--;*/
                    }
                    if(events[i].key.keysym.scancode == infcube->settings.inventory_key.key) {
                        //infcube->gamemode.open_inventory((struct gamemode_s *)&infcube->gamemode);
                        screen_t *inv = malloc(sizeof(screen_t));
                        screen_inventory_create(inv, &infcube->player.mob->player->inventory);
                        infcube_set_current_screen(infcube, inv);
                        events[i] = (SDL_Event){ 0 };
                    }
                    if(events[i].key.keysym.scancode == infcube->settings.chat_key.key) {
                        events[i] = (SDL_Event){ 0 };
                    }
                }
                for(int j = 0; j < 9; j++) {
                    if(events[i].key.keysym.scancode == SDL_SCANCODE_1 + j) {
                        infcube->player.mob->player->inventory.selected = j;
                    }
                }
                if(events[i].key.keysym.scancode == infcube->settings.toggle_fog_key.key) {
                    game_settings_toggle_setting(&infcube->settings, 4);
                }
            }
        }
    }

    if(infcube->current_screen == NULL) {
        if(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK && (infcube->ticks - infcube->last_click) >= infcube->timer.ticks_per_second / 4.0) {
            on_mouse_clicked(infcube, SDL_BUTTON_LEFT);
            infcube->last_click = infcube->ticks;
        }
        if(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_RMASK && (infcube->ticks - infcube->last_click) >= infcube->timer.ticks_per_second / 4.0) {
            on_mouse_clicked(infcube, SDL_BUTTON_RIGHT);
            infcube->last_click = infcube->ticks;
        }
    }

    if(!infcube->gamemode.instant_break && infcube->miss_time <= 0) {
        if(infcube->current_screen == NULL && SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK && !infcube->hit_result.null && infcube->hit_result.type == 0) { // and mouse was down
            infcube->gamemode.continue_destroy_block(&infcube->gamemode, infcube->hit_result.x, infcube->hit_result.y, infcube->hit_result.z, infcube->hit_result.face);
        } else {
            infcube->gamemode.stop_destroy_block(&infcube->gamemode);
        }
    }

    if(infcube->current_screen != NULL) {
        if(infcube->current_screen->removed) {
            infcube->current_screen->destroy((struct screen_s *)infcube->current_screen);
            free(infcube->current_screen);
            infcube->current_screen = NULL;
            infcube->pending_mouse_button_up = 0;
        } else {
            screen_do_input(infcube->current_screen, events);
            if(infcube->current_screen != NULL) {
                infcube->current_screen->tick((struct screen_s *)infcube->current_screen);
            }
        }
    }

    /*item_stack_t selected = inventory_player_get_selected(&infcube->player.mob->player->inventory);
    block_t *block = &block_list[blocks.air.id];
    if(selected.item_id > 0) block = &block_list[selected];
    float s = (block == renderer->held_block.block ? 1.0 : 0.0) - renderer->held_block.position;
    if(s < -0.4) s = -0.4;
    if(s > 0.4) s = 0.4;
    renderer->held_block.position += s;
    if(renderer->held_block.position < 0.1) {
        renderer->held_block.block = block;
    }*/

    /*if(infcube->raining) {
        world_t *world = infcube->world;
        int vx = infcube->player.mob.x;
        int vy = infcube->player.mob.y;
        int vz = infcube->player.mob.z;
        for(int i = 0; i < 50; i++) {
            int rx = vx + (int)random_next_int_range(&renderer->random, 8) - 4;
            int rz = vz + (int)random_next_int_range(&renderer->random, 8) - 4;
            int ry = world_get_highest_block(world, rx, rz);
            if(ry <= vy + 4 && ry >= vy - 4) {
                float xo = random_next_double(&renderer->random);
                float zo = random_next_double(&renderer->random);
                particle_t *particle = malloc(sizeof(particle_t));
                *particle = particle_water_drop_create(world, rx + xo, ry + 0.1, rz + zo);
                particles_spawn_particle(&infcube->particles, particle);
            }
        }
    }*/

    infcube->renderer_world.ticks++;
    //world_tick_entities(infcube->world);
    //world_tick(infcube->world);
    if(infcube->world != NULL && !infcube->is_paused) {
        renderer_camera_update(&infcube->renderer);
        renderer_world_update_clouds(&infcube->renderer_world);
        world_update_entities(infcube->world);
        world_restart_time_of_day(infcube->world);
        world_visual_update(infcube->world, floor_double(infcube->player.x), floor_double(infcube->player.y), floor_double(infcube->player.z));
        particles_tick(&infcube->particles);
    }
}

void infcube_run(infcube_t *infcube) {
    infcube->running = 1;
    int frame = 0;
    uint64_t start = time_millis();
    //infcube_grab_mouse(infcube);
    SDL_Event event;
    SDL_Event *events = array_list_create(sizeof(SDL_Event));

    while(infcube->running) {
        if(infcube->timer.elapsed_ticks > 0) events = array_list_clear(events);
        while(SDL_PollEvent(&event)) {
            if(event.type == SDL_QUIT) {
                infcube->running = 0;
            }
            int x = infcube->width;
            int y = infcube->height;
            int w = x;
            int h = y;
            for(x = 1; w / (x + 1) >= 320 && h / (x + 1) >= 240; x++);
            w /= x;
            h /= x;
            if(event.type == SDL_WINDOWEVENT) {
                int ev = event.window.event;
                if(ev == SDL_WINDOWEVENT_RESIZED || ev == SDL_WINDOWEVENT_SIZE_CHANGED || ev == SDL_WINDOWEVENT_MAXIMIZED || ev == SDL_WINDOWEVENT_RESTORED || ev == SDL_WINDOWEVENT_SHOWN || ev == SDL_WINDOWEVENT_ENTER) {
                    SDL_GetWindowSize(infcube->window, &infcube->width, &infcube->height);
                    SDL_GL_GetDrawableSize(infcube->window, &infcube->frame_width, &infcube->frame_height);
                    glViewport(0, 0, infcube->frame_width, infcube->frame_height);
                    screen_hud_destroy(&infcube->hud);
                    infcube->hud = screen_hud_create((struct infcube_s *)infcube, w, h);
                    glClear(GL_COLOR_BUFFER_BIT);
                }

                if(infcube->current_screen != NULL) {
                    screen_open(infcube->current_screen, infcube, w, h);
                }
            }
            SDL_Event copy;
            memcpy(&copy, &event, sizeof(SDL_Event));
            events = array_list_push(events, &copy);
        }

        game_timer_t *timer = &infcube->timer;
        int64_t time = time_millis();
        int64_t d = time - timer->last_system_clock;
        int64_t t2 = time_nano() / 1000000;
        double b;
        if(d > 1000) {
            int64_t d2 = t2 - timer->last_hr_clock;
            b = (double)d / d2;
            timer->adjustment += (b - timer->adjustment) * 0.2;
            timer->last_system_clock = time;
            timer->last_hr_clock = t2;
        }
        if(d < 0) {
            timer->last_system_clock = time;
            timer->last_hr_clock = t2;
        }
        double a = t2 / 1000.0;
        b = (a - timer->last_hr) * timer->adjustment;
        timer->last_hr = a;
        if(b < 0.0) b = 0.0;
        if(b > 1.0) b = 1.0;
        timer->elapsed_delta += b * timer->speed * timer->ticks_per_second;
        timer->elapsed_ticks = timer->elapsed_delta;
        timer->elapsed_delta -= timer->elapsed_ticks;
        if(timer->elapsed_ticks > 10) timer->elapsed_ticks = 10;
        timer->delta = timer->elapsed_delta;
        float delta = timer->delta;

        int ticks = 0;
        for(;;) {
            if(ticks >= infcube->timer.elapsed_ticks) {
                if(infcube->is_paused) {
                    infcube->timer.delta = 1;
                }
                glEnable(GL_TEXTURE);
                if(infcube->world != NULL) {
                    // Spread lighting over several frames to avoid stutter
                    uint64_t light_start = SDL_GetPerformanceCounter();
                    while(world_update_lighting(infcube->world)) {
                        if((SDL_GetPerformanceCounter() - light_start) * 1000.0 / SDL_GetPerformanceFrequency() > 4.0) break;
                    }
                }
                gamemode_set_partial_time(&infcube->gamemode, delta);
                renderer_camera_update_mouse(&infcube->renderer, delta);
                infcube->is_paused = infcube->current_screen != NULL && infcube->current_screen->pauses_game;
                frame++;
                break;
            }
            infcube->ticks++;
            infcube_tick(infcube, events);
            ticks++;
        }

        renderer_camera_t *renderer = &infcube->renderer;

        infcube->gamemode.render(&infcube->gamemode, delta);
        if(renderer->display_active && (SDL_GetWindowFlags(infcube->window) & SDL_WINDOW_INPUT_FOCUS) == 0) {
            infcube_pause(infcube);
        }
        renderer->display_active = (SDL_GetWindowFlags(infcube->window) & SDL_WINDOW_INPUT_FOCUS) > 0;
        if(infcube->has_mouse) {
            int dx = 0;
            int dy = 0;
            int x = 0;
            int y = 0;
            SDL_GetWindowPosition(infcube->window, &x, &y);
            x += infcube->width / 2;
            y += infcube->height / 2;
            SDL_GetGlobalMouseState(&dx, &dy);
            dx -= x;
            dy = -(dy - y);
            SDL_WarpMouseGlobal(x, y);

            entity_turn(&infcube->player, dy * (infcube->settings.invert_mouse ? -1 : 1), dx);
        }

        int x = infcube->width;
        int y = infcube->height;
        int w = x;
        int h = y;
        for(x = 1; w / (x + 1) >= 320 && h / (x + 1) >= 240; x++);
        w /= x;
        h /= x;
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        mx = mx * w / infcube->width;
        my = my * h / infcube->height - 1;

        //screen_hud_render(&infcube->hud, mx, my, delta);

        /*glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glColor4ub(255, 0, 255, 255);
        glOrtho(0.0, w, h, 0.0, 0.0, 1);
        if(infcube->current_screen != NULL) {
           // infcube->current_screen->render((struct screen_s *)infcube->current_screen, mx, my, delta);
        }*/

        //frame++;
        //SDL_GL_SwapWindow(infcube->window);

        while(time_millis() >= start + 1000) {
            char *chunks = string_create_from_int(chunk_updates);
            string_concat(&chunks, " chunk updates");
            string_set_from_int(&infcube->debug, frame);
            string_concat(&infcube->debug, " fps, ");
            string_concat(&infcube->debug, chunks);
            string_free(chunks);
            start += 1000;
            frame = 0;
            chunk_updates = 0;
        }
    }
    array_list_free(events);
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
#include <windows.h>
    extern uint8_t SetProcessDPIAware(void); //gets rid of implicit warning
    SetProcessDPIAware();
#endif
    infcube_t infcube = { 0 };
    memset(&infcube, 0, sizeof(infcube_t));
    infcube_create(&infcube, 1920, 1080, 0);
    infcube_run(&infcube);

    if(infcube.current_screen != NULL) {
        screen_destroy((struct screen_s *)infcube.current_screen);
        free(infcube.current_screen);
    }
    world_destroy(infcube.world);
    free(infcube.world);
    textures_destroy(&infcube.textures);
    renderer_world_destroy(&infcube.renderer_world);
    particles_destroy(&infcube.particles);
    array_list_free(infcube.settings.bindings);
    array_list_free(session_allowed_blocks);
    sounds_destroy(&infcube.sounds);

    SDL_DestroyWindow(infcube.window);
    SDL_Quit();

    return 0;
}