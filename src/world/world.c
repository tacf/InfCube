#include <world/world.h>

#include <renderer/renderer_world.h>
#include <world/block/block.h>
#include <world/block/blocks.h>
#include <sound/sounds.h>
#include <infcube.h>
#include <world/chunk/chunk.h>
#include <world/material/materials.h>
#include <world/chunk/chunk_provider_load.h>

#include <util/array_list.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define PATH_SEPARATOR '/'
#define mkdir_recursive _mkdir
#else
#include <unistd.h>
#include <libgen.h>
#define PATH_SEPARATOR '/'
#define mkdir_recursive mkdir
#endif

void world_create(world_t *world, struct infcube_s *infcube, char *saves_dir, char *world_name, int64_t seed) {
    memset(world, 0, sizeof(world_t));
    
    world->infcube = infcube;
    world->lighting_update_list = array_list_create(sizeof(chunk_metadata_t *));
    world->loaded_entity_list = array_list_create(sizeof(entity_t *));
    world->next_tick_data_list = array_list_create(sizeof(next_tick_data_t));
    world->loaded_tile_entity_list = array_list_create(sizeof(tile_entity_t *));
    world->renderer_world_list = array_list_create(sizeof(renderer_world_t *));
    world->world_time = 0;
    world->sky_color = 0xFF99CCFF;
    world->fog_color = 0xFFB0D0FF;
    world->cloud_color = 0xFFFFFFFF;
    world->skylight_subtracted = 0;
    world->random = random_create(seed);
    world->random_number = random_next_int(&world->random);
    world->random_seed = seed;
    world->size_on_disk = 0;
    world->is_new_world = 1;
    world->chunks_generated_this_frame = 0;
    world->visual_update_random = random_create(seed);
    world->player_nbt = (nbt_base_t){ .null = 1 };
    strncpy(world->save_file, saves_dir, sizeof(world->save_file) - 1);
    strcat(world->save_file, "/");
    strcat(world->save_file, world_name);
    world->spawn_x = 64;
    world->spawn_y = 64;
    world->spawn_z = 64;

    for(int i = 0; i <= 15; i++) {
        float value = 1.0 - (float)i / 15.0;
        world->light_brightness_table[i] = (1.0 - value) / (value * 3.0 + 1.0) * 0.95 + 0.05;
    }

    // checks if file exists already
    struct stat sb;
    char folder[290];
    snprintf(folder, sizeof(folder), "%s/level.dat", world->save_file);
    if(stat(folder, &sb) == 0) {
        world->is_new_world = 0;
    }

    if(!world->is_new_world) {
        FILE *file = fopen(folder, "rb");
        printf("Opening existing world: %s\n", folder);
        nbt_base_t nbt = progress_bar_read(file);
        fclose(file);
        nbt = nbt_tag_compound_get_compound_tag(&nbt, "Data");
        world->random_seed = nbt_tag_compound_get_int(&nbt, "RandomSeed");
        world->spawn_x = nbt_tag_compound_get_int(&nbt, "SpawnX");
        world->spawn_y = nbt_tag_compound_get_int(&nbt, "SpawnY");
        world->spawn_z = nbt_tag_compound_get_int(&nbt, "SpawnZ");
        world->world_time = nbt_tag_compound_get_long(&nbt, "Time");
        world->size_on_disk = nbt_tag_compound_get_long(&nbt, "SizeOnDisk");
        world->player_nbt = nbt_tag_compound_get_compound_tag(&nbt, "Player");
    }

    if(world->random_seed == 0) {
        world->random_seed = seed;
        world->spawn_x = 0;
        world->spawn_y = 64;
        world->spawn_z = 0;
    }

    chunk_provider_generate_create(&world->chunk_provider_gen, world, world->random_seed);
    chunk_provider_load_create(&world->chunk_provider, &world->chunk_provider_gen, world, world->save_file);
    world_save(world, 0);
}

nbt_base_t world_get_nbt_tag(char *game_dir, char *world_name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/saves/%s/level.dat", game_dir, world_name);
    FILE *file = fopen(path, "rb");
    if(!file) {
        return (nbt_base_t){ .null = 1 };
    }
    nbt_base_t nbt = progress_bar_read(file);
    nbt_base_t data_nbt = nbt_tag_compound_get_compound_tag(&nbt, "Data");

    fclose(file);

    return data_nbt;
}

void world_spawn_player(world_t *world) {
    if(!world->player_nbt.null) {
        world->player->read_nbt(world->player, &world->player_nbt);
    }
    world_spawn_entity(world, world->player);
}

char *private_dirname(char *path) {
#ifdef _WIN32
    static char buffer[1024];
    strncpy(buffer, path, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';

    char *last_separator = strrchr(buffer, PATH_SEPARATOR);
    if(last_separator) {
        *last_separator = '\0';
    } else {
        return ".";
    }
    return buffer;
#else
    return dirname(path);
#endif
}

int private_create_directories_world(const char *path) {
    char path_copy[1024];
    strncpy(path_copy, path, sizeof(path_copy));
    path_copy[sizeof(path_copy) - 1] = '\0';

    char dir_buffer[1024];
    strncpy(dir_buffer, path_copy, sizeof(dir_buffer));
    dir_buffer[sizeof(dir_buffer) - 1] = '\0';

    char *target_dir = private_dirname(dir_buffer);
    if(target_dir == NULL || target_dir[0] == '\0') {
        return 0;
    }

    char temp_path[1024];
    strncpy(temp_path, target_dir, sizeof(temp_path));
    temp_path[sizeof(temp_path) - 1] = '\0';

    char *current = temp_path;
    char *next = NULL;

    while((next = strchr(current, '/')) != NULL) {
        *next = '\0';

        if(strlen(temp_path) > 0 && strcmp(temp_path, ".") != 0) {
#ifdef _WIN32
            if(!CreateDirectory(temp_path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                fprintf(stderr, "Failed to create world directory: %s\n", temp_path);
                return -1;
            }
#else
            if(mkdir(temp_path, 0755) != 0 && errno != EEXIST) {
                perror("mkdir");
                return -1;
            }
#endif
        }

        *next = '/';
        current = next + 1;
    }

#ifdef _WIN32
    if(strlen(temp_path) > 0 && strcmp(temp_path, ".") != 0) {
        if(!CreateDirectory(temp_path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            fprintf(stderr, "Failed to create world directory: %s\n", temp_path);
            return -1;
        }
    }
#else
    if(strlen(temp_path) > 0 && strcmp(temp_path, ".") != 0) {
        if(mkdir(temp_path, 0755) != 0 && errno != EEXIST) {
            perror("mkdir");
            return -1;
        }
    }
#endif

    return 0;
}

void world_save(world_t *world, uint8_t check_entities) {
    char path[290];
    snprintf(path, sizeof(path), "%s/level.dat", world->save_file);
    //printf("%s\n", path);
    FILE *file = fopen(path, "wb");
    if(!file) {
        private_create_directories_world(path);
        file = fopen(path, "wb");
        if(!file) {
            printf("Failed to open %s for writing.\n", path);
            return;
        }
    }

    nbt_base_t nbt = nbt_tag_compound_create();
    nbt_tag_compound_set_long(&nbt, "RandomSeed", world->random_seed);
    nbt_tag_compound_set_int(&nbt, "SpawnX", world->spawn_x);
    nbt_tag_compound_set_int(&nbt, "SpawnY", world->spawn_y);
    nbt_tag_compound_set_int(&nbt, "SpawnZ", world->spawn_z);
    nbt_tag_compound_set_long(&nbt, "Time", world->world_time);
    nbt_tag_compound_set_long(&nbt, "LastPlayed", time(NULL));
    if(world->player != NULL) {
        nbt_base_t player_nbt = nbt_tag_compound_create();
        world->player->write_nbt(world->player, &player_nbt);
        nbt_tag_compound_set_compound_tag(&nbt, "Player", &player_nbt);
    }

    chunk_provider_load_save_chunks(&world->chunk_provider, check_entities);
    
    nbt_tag_compound_set_long(&nbt, "SizeOnDisk", world->size_on_disk);

    nbt_base_t base_nbt = nbt_tag_compound_create();
    nbt_tag_compound_set_tag(&base_nbt, "Data", &nbt);

    progress_bar_write(file, &base_nbt);
    fclose(file);

    nbt_tag_compound_free(&base_nbt);
}

uint8_t world_get_block(world_t *world, int x, int y, int z) {
    if(x < -WORLD_MAX_SIZE || x >= WORLD_MAX_SIZE 
        || y >= CHUNK_SIZE_HEIGHT
        || z < -WORLD_MAX_SIZE || z > WORLD_MAX_SIZE) {
        return 0;
    }

    // Lake of lava at bottom of world
    if(y <= 0) return BLOCK_LAVA;

    return chunk_get_block_id(world_get_chunk(world, x >> 4, z >> 4), 
                              x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1));
}

uint8_t world_block_exists(world_t *world, int x, int y, int z) {
    return y >= 0 && y < CHUNK_SIZE_HEIGHT ? world_chunk_exists(world, x >> 4, z >> 4) : 0;
}

uint8_t world_chunk_exists(world_t *world, int x, int z) {
    return world->chunk_provider.chunk_exists(&world->chunk_provider, x, z);
}

struct chunk_s *world_get_chunk(world_t *world, int x, int z) {
    return world->chunk_provider.chunk_provide(&world->chunk_provider, x, z);
}

uint8_t world_set_block_no_update(world_t *world, int x, int y, int z, uint8_t block_id) {
    if(x < -WORLD_MAX_SIZE || x >= WORLD_MAX_SIZE
        || y < 0 || y >= CHUNK_SIZE_HEIGHT
        || z < -WORLD_MAX_SIZE || z > WORLD_MAX_SIZE) {
        return 0;
    }
    chunk_t *chunk = world_get_chunk(world, x >> 4, z >> 4);
    return chunk_set_block(chunk, x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1), block_id);
}

material_t *world_get_block_material(world_t *world, int x, int y, int z) {
    uint8_t block_id = world_get_block(world, x, y, z);
    return block_id == 0 ? &materials.air : block_list[block_id].material;
}

uint8_t world_get_block_metadata(world_t *world, int x, int y, int z) {
    if(x < -WORLD_MAX_SIZE || x >= WORLD_MAX_SIZE
        || y < 0 || y >= CHUNK_SIZE_HEIGHT
        || z < -WORLD_MAX_SIZE || z > WORLD_MAX_SIZE) {
        return 0;
    }
    chunk_t *chunk = world_get_chunk(world, x >> 4, z >> 4);
    return chunk_get_block_metadata(chunk,x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1));
}

uint8_t world_set_block_metadata(world_t *world, int x, int y, int z, uint8_t data) {
    if(x < -WORLD_MAX_SIZE || x >= WORLD_MAX_SIZE
        || y < 0 || y >= CHUNK_SIZE_HEIGHT
        || z < -WORLD_MAX_SIZE || z > WORLD_MAX_SIZE) {
        return 0;
    }
    chunk_t *chunk = world_get_chunk(world, x >> 4, z >> 4);
    chunk_set_block_metadata(chunk, x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1), data);
    return 1;
}

uint8_t world_set_block_with_update(world_t *world, int x, int y, int z, uint8_t block_id) {
    if(!world_set_block_no_update(world, x, y, z, block_id)) return 0;

    for(int i = 0; i < array_list_length(world->renderer_world_list); i++) {
        renderer_world_t *renderer = *(renderer_world_t **)array_list_get(world->renderer_world_list, i);
        renderer_world_update_block(renderer, x, y, z);
    }

    world_update_neighbors_at(world, x, y, z, block_id);
    return 1;
}

void world_mark_blocks_dirty_vertical(world_t *world, int x, int z, int y1, int y2) {
    if(y1 > y2) {
        int swap = y2;
        y2 = y1;
        y1 = swap;
    }

    for(int i = 0; i < array_list_length(world->renderer_world_list); i++) {
        renderer_world_t *renderer = *(renderer_world_t **)array_list_get(world->renderer_world_list, i);
        renderer_world_update_blocks(renderer, x, y1, z, x, y2, z);
    }
}

void world_swap(world_t *world, int x0, int y0, int z0, int x1, int y1, int z1) {
    uint8_t t1 = world_get_block(world, x0, y0, z0);
    uint8_t m1 = world_get_block_metadata(world, x0, y0, z0);
    uint8_t t2 = world_get_block(world, x1, y1, z1);
    uint8_t m2 = world_get_block_metadata(world, x1, y1, z1);
    world_set_block_no_update(world, x0, y0, z0, t2);
    world_set_block_metadata(world, x0, y0, z0, m2);
    world_set_block_no_update(world, x1, y1, z1, t1);
    world_set_block_metadata(world, x1, y1, z1, m1);
    world_update_neighbors_at(world, x0, y0, z0, t2);
    world_update_neighbors_at(world, x1, y1, z1, t1);
}

void world_update_neighbors_at(world_t *world, int x, int y, int z, uint8_t block_id) {
    world_update_block(world, x - 1, y, z, block_id);
    world_update_block(world, x + 1, y, z, block_id);
    world_update_block(world, x, y - 1, z, block_id);
    world_update_block(world, x, y + 1, z, block_id);
    world_update_block(world, x, y, z - 1, block_id);
    world_update_block(world, x, y, z + 1, block_id);
}

void world_update_block(world_t *world, int x, int y, int z, uint8_t block_id) {
    block_t *block = &block_list[world_get_block(world, x, y, z)];
    if(block->id != blocks.air.id) { 
        block->on_neighbor_changed(block, (struct world_s *)world, x, y, z, block_id);
    }
}

uint8_t world_can_block_see_sky(world_t *world, int x, int y, int z) {
    return chunk_can_block_see_sky(world_get_chunk(world, x >> 4, z >> 4), x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1));
}

uint8_t private_world_get_block_light_value(world_t *world, int x, int y, int z, uint8_t check_neighbors) {
    if(x < -WORLD_MAX_SIZE || x >= WORLD_MAX_SIZE
        || z < -WORLD_MAX_SIZE || z > WORLD_MAX_SIZE) {
        return 15;
    }

    if(check_neighbors) {
        uint8_t block_id = world_get_block(world, x, y, z);
        if(block_id == BLOCK_SLAB || block_id == BLOCK_FARMLAND) {
            // I have no idea if these cardinal directions are right but here we are
            uint8_t light_top = private_world_get_block_light_value(world, x, y + 1, z, 0);
            uint8_t light_east = private_world_get_block_light_value(world, x + 1, y, z, 0);
            uint8_t light_west = private_world_get_block_light_value(world, x - 1, y, z, 0);
            uint8_t light_north = private_world_get_block_light_value(world, x, y, z + 1, 0);
            uint8_t light_south = private_world_get_block_light_value(world, x, y, z - 1, 0);

            if(light_east > light_top) light_top = light_east;
            if(light_west > light_top) light_top = light_west;
            if(light_north > light_top) light_top = light_north;
            if(light_south > light_top) light_top = light_south;

            return light_top;
        }
    }

    if(y < 0) return 0;
    if(y >= CHUNK_SIZE_HEIGHT) {
        int16_t light = 15 - world->skylight_subtracted;
        if(light < 0) light = 0;
        return light;
    }
    chunk_t *chunk = world_get_chunk(world, x >> 4, z >> 4);
    return chunk_get_block_light_value(chunk, x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1), world->skylight_subtracted);
}

uint8_t world_get_block_light_value(world_t *world, int x, int y, int z) {
    return private_world_get_block_light_value(world, x, y, z, 1);
}

uint8_t world_can_existing_block_see_sky(world_t *world, int x, int y, int z) {
    if(x < -WORLD_MAX_SIZE || x >= WORLD_MAX_SIZE
        || y < 0
        || z < -WORLD_MAX_SIZE || z > WORLD_MAX_SIZE) {
        return 0;
    }
    if(y >= CHUNK_SIZE_HEIGHT) return 1;
    if(!world_chunk_exists(world, x >> 4, z >> 4)) return 0;

    chunk_t *chunk = world_get_chunk(world, x >> 4, z >> 4);
    return chunk_can_block_see_sky(chunk, x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1));
}

int world_get_height_value(world_t *world, int x, int z) {
    if(x < -WORLD_MAX_SIZE || x >= WORLD_MAX_SIZE
        || z < -WORLD_MAX_SIZE || z > WORLD_MAX_SIZE) {
        return 0;
    }
    if(!world_chunk_exists(world, x >> 4, z >> 4)) return 0;

    chunk_t *chunk = world_get_chunk(world, x >> 4, z >> 4);
    return chunk_get_height_value(chunk, x & (CHUNK_SIZE_WIDTH - 1), z & (CHUNK_SIZE_WIDTH - 1));
}

void world_light_changed(world_t *world, uint8_t light_type, int x, int y, int z, int value) {
    if(!world_block_exists(world, x, y, z)) return;

    if(light_type == LIGHT_TYPE_SKY) {
        if(world_can_existing_block_see_sky(world, x, y, z)) {
            value = 15;
        }
    }else if(light_type == LIGHT_TYPE_BLOCK) {
        uint8_t block_id = world_get_block(world, x, y, z);
        if(block_list[block_id].light_value > value) {
            value = block_list[block_id].light_value;
        }
    }

    if(world_get_saved_light_value(world, light_type, x, y, z) != value) {
        world_schedule_light_update(world, light_type, x, y, z, x, y, z);
    }
}

int world_get_saved_light_value(world_t *world, uint8_t light_type, int x, int y, int z) {
    if(x < -WORLD_MAX_SIZE || x >= WORLD_MAX_SIZE
        || y < 0 || y >= CHUNK_SIZE_HEIGHT
        || z < -WORLD_MAX_SIZE || z > WORLD_MAX_SIZE) {
        return light_type;
    }

    if(!world_chunk_exists(world, x >> 4, z >> 4)) return 0;

    chunk_t *chunk = world_get_chunk(world, x >> 4, z >> 4);
    return chunk_get_saved_light_value(chunk, light_type, x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1));
}

float world_get_brightness(world_t *world, int x, int y, int z) {
    int light_level = world_get_block_light_value(world, x, y, z);
    return world->light_brightness_table[light_level];
}

uint8_t world_is_daytime(world_t *world) {
    return world->skylight_subtracted < 8;
}

hit_result_t world_clip(world_t *world, vec3_t v0, vec3_t v1) {
    if(isnan(v0.x) || isnan(v0.y) || isnan(v0.z)) return (hit_result_t){ .null = 1 };
    if(isnan(v1.x) || isnan(v1.y) || isnan(v1.z)) return (hit_result_t){ .null = 1 };
    int i0x = floor_double(v0.x), i0y = floor_double(v0.y), i0z = floor_double(v0.z);
    int i1x = floor_double(v1.x), i1y = floor_double(v1.y), i1z = floor_double(v1.z);
    
    int i = 20;
    while(i-- >= 0) {
        if(isnan(v0.x) || isnan(v0.y) || isnan(v0.z)) return (hit_result_t){ .null = 1 };
        if(i0x == i1x && i0y == i1y && i0z == i1z) return (hit_result_t){ .null = 1 };
        vec3_t a = { 999.0, 999.0, 999.0 };
        if(i1x > i0x) a.x = i0x + 1.0;
        if(i1x < i0x) a.x = i0x;
        if(i1y > i0y) a.y = i0y + 1.0;
        if(i1y < i0y) a.y = i0y;
        if(i1z > i0z) a.z = i0z + 1.0;
        if(i1z < i0z) a.z = i0z;
        vec3_t b = { 999.0, 999.0, 999.0 };
        vec3_t d = vec3_subtract(v1, v0);
        if(a.x != 999.0) b.x = (a.x - v0.x) / d.x;
        if(a.y != 999.0) b.y = (a.y - v0.y) / d.y;
        if(a.z != 999.0) b.z = (a.z - v0.z) / d.z;
        int c;
        if(b.x < b.y && b.x < b.z) {
            c = i1x > i0x ? 4 : 5;
            v0.x = a.x;
            v0.y += d.y * b.x;
            v0.z += d.z * b.x;
        }else if(b.y < b.z) {
            c = i1y > i0y ? 0 : 1;
            v0.y = a.y;
            v0.x += d.x * b.y;
            v0.z += d.z * b.y;
        }else {
            c = i1z > i0z ? 2 : 3;
            v0.z = a.z;
            v0.x += d.x * b.z;
            v0.y += d.y * b.z;
        }
        
        vec3_t v00 = { floor_double(v0.x), floor_double(v0.y), floor_double(v0.z) };
        i0x = v00.x;
        i0y = v00.y;
        i0z = v00.z;
        if(c == 5) {
            i0x--;
            v00.x++;
        }
        if(c == 1) {
            i0y--;
            v00.y++;
        }
        if(c == 3) {
            i0z--;
            v00.z++;
        }
        uint8_t block_id = world_get_block(world, i0x, i0y, i0z);
        block_t *block = &block_list[block_id];
        if(block_id != blocks.air.id && block->is_collidable) {
            hit_result_t pos = block->clip(block, world, i0x, i0y, i0z, v0, v1);
            if(!pos.null) return pos;
        }
    }
    return (hit_result_t){ .null = 1 };
}

void world_play_sound_at_entity(world_t *world, entity_t *entity, uint8_t sound, float volume, float pitch) {
    for(int i = 0; i < array_list_length(world->renderer_world_list); i++) {
        float attenuation = 16;
        if(volume > 1) attenuation *= volume;

        if(entity_distance_to_sqr(world->player->mob->entity, entity) < attenuation * attenuation) {
            renderer_world_t *renderer = *(renderer_world_t **)array_list_get(world->renderer_world_list, i);
            renderer_world_play_sound(renderer, sound, entity->x, entity->y - entity->y_slide_offset, entity->z, volume, pitch);
        }
    }
}

void world_play_sound(world_t *world, double x, double y, double z, uint8_t sound, float volume, float pitch) {
    for(int i = 0; i < array_list_length(world->renderer_world_list); i++) {
        float attenuation = 16;
        if(volume > 1) attenuation *= volume;

        double xx = x - world->player->x;
        double yy = y - world->player->y;
        double zz = z - world->player->z;
        if(xx * xx + yy * yy + zz * zz < attenuation * attenuation) {
            renderer_world_t *renderer = *(renderer_world_t **)array_list_get(world->renderer_world_list, i);
            renderer_world_play_sound(renderer, sound, x, y, z, volume, pitch);
        }
    }
}

void world_spawn_particle(world_t *world, uint8_t particle_type, double x, double y, double z, double x_vel, double y_vel, double z_vel) {
    for(int i = 0; i < array_list_length(world->renderer_world_list); i++) {
        renderer_world_t *renderer = *(renderer_world_t **)array_list_get(world->renderer_world_list, i);
        renderer_world_spawn_particle(renderer, particle_type, x, y, z, x_vel, y_vel, z_vel);
    }
}

void world_spawn_entity(world_t *world, entity_t *entity) {
    int x = floor_double(entity->x / CHUNK_SIZE_WIDTH);
    int z = floor_double(entity->z / CHUNK_SIZE_WIDTH);
    if(!world_chunk_exists(world, x, z)) {
        printf("Failed to add entity\n");
        return;
    }
    chunk_t *chunk = world_get_chunk(world, x, z);
    chunk_add_entity(chunk, entity);
    world->loaded_entity_list = array_list_push(world->loaded_entity_list, &entity);

    for(int i = 0; i < array_list_length(world->renderer_world_list); i++) {
        renderer_world_t *renderer = *(renderer_world_t **)array_list_get(world->renderer_world_list, i);
        renderer_world_obtain_entity_skin(renderer, entity);
    }
}

void world_set_entity_dead(world_t *world, entity_t *entity) {
    entity->is_dead = 1;
}

void world_add_renderer(world_t *world, renderer_world_t *renderer) {
    world->renderer_world_list = array_list_push(world->renderer_world_list, &renderer);
}

void world_remove_renderer(world_t *world, renderer_world_t *renderer) {
    int index = array_list_index_of(world->renderer_world_list, renderer);
    if(index == -1) return;
    world->renderer_world_list = array_list_remove(world->renderer_world_list, index);
}

AABB_t *world_get_cubes(world_t *world, AABB_t box) {
    AABB_t *list = array_list_create(sizeof(AABB_t));
    int x0 = floor_double(box.x0);
    int y0 = floor_double(box.y0);
    int z0 = floor_double(box.z0);
    int x1 = floor_double(box.x1 + 1);
    int y1 = floor_double(box.y1 + 1);
    int z1 = floor_double(box.z1 + 1);

    for(int i = x0; i < x1; i++) {
        for(int j = y0; j < y1; j++) {
            for(int k = z0; k < z1; k++) {
                AABB_t bb = { 0 };
                
                block_t *block = &block_list[world_get_block(world, i, j, k)];
                if(block->id != blocks.air.id) {
                    bb = block->get_collision_aabb(block, i, j, k);
                    if(!bb.null && AABB_intersects_inner(box, bb)) {
                        list = array_list_push(list, &bb);
                    }
                }
            }
        }
    }
    return list;
}

vec3_t world_get_sky_color(world_t *world, float partial_tick) {
    float angle = world_get_celestial_angle(world, partial_tick);
    angle = tcos(angle * M_PI * 2.0) * 2.0 + 0.5;

    if(angle < 0.0) angle = 0.0;
    if(angle > 1.0) angle = 1.0;

    float r = (world->sky_color >> 16 & 0xFF) / 255.0;
    float g = (world->sky_color >> 8 & 0xFF) / 255.0;
    float b = (world->sky_color & 0xFF) / 255.0;

    r *= angle;
    g *= angle;
    b *= angle;

    return (vec3_t){ r, g, b };
}

float world_get_celestial_angle(world_t *world, float partial_tick) {
    int time = world->world_time % 24000;
    float angle = (time + partial_tick) / 24000.0 - 0.15;

    return angle;
}

vec3_t world_get_cloud_color(world_t *world, float partial_tick) {
    float angle = world_get_celestial_angle(world, partial_tick);
    angle = tcos(angle * M_PI * 2.0) * 2.0 + 0.5;

    if(angle < 0.0) angle = 0.0;
    if(angle > 1.0) angle = 1.0;

    float r = (world->cloud_color >> 16 & 0xFF) / 255.0;
    float g = (world->cloud_color >> 8 & 0xFF) / 255.0;
    float b = (world->cloud_color & 0xFF) / 255.0;

    r *= angle * 0.9 + 0.1;
    g *= angle * 0.9 + 0.1;
    b *= angle * 0.85 + 0.15;

    return (vec3_t){ r, g, b };
}

vec3_t world_get_fog_color(world_t *world, float partial_tick) {
    float angle = world_get_celestial_angle(world, partial_tick);
    angle = tcos(angle * M_PI * 2.0) * 2.0 + 0.5;

    if(angle < 0.0) angle = 0.0;
    if(angle > 1.0) angle = 1.0;

    float r = (world->fog_color >> 16 & 0xFF) / 255.0;
    float g = (world->fog_color >> 8 & 0xFF) / 255.0;
    float b = (world->fog_color & 0xFF) / 255.0;

    r *= angle * 0.94 + 0.06;
    g *= angle * 0.94 + 0.06;
    b *= angle * 0.91 + 0.09;

    return (vec3_t){ r, g, b };
}

float world_get_star_brightness(world_t *world, float partial_tick) {
    float angle = world_get_celestial_angle(world, partial_tick);
    angle = 1.0 - (tcos(angle * M_PI * 2.0) * 2.0 + (12.0 / 16.0));

    if(angle < 0.0) angle = 0.0;
    if(angle > 1.0) angle = 1.0;

    return angle * angle * 0.5;
}

void world_schedule_block_update(world_t *world, int x, int y, int z, uint8_t block_id) {
    next_tick_data_t next_tick = { 0 };
    next_tick.x = x;
    next_tick.y = y;
    next_tick.z = z;
    next_tick.block_id = block_id;
    if(block_id > 0) {
        next_tick.delay = block_list[block_id].tick_rate;
    }

    world->next_tick_data_list = array_list_push(world->next_tick_data_list, &next_tick);
}

void world_update_entities(world_t *world) {
    for(int i = 0; i < array_list_length(world->loaded_entity_list); i++) {
        entity_t *entity = *(entity_t **)array_list_get(world->loaded_entity_list, i);
        if(!entity->is_dead) {
            int cx = floor_double(entity->x / CHUNK_SIZE_WIDTH);
            int cy = floor_double(entity->y / CHUNK_SIZE_WIDTH);
            int cz = floor_double(entity->z / CHUNK_SIZE_WIDTH);

            entity->last_tick_x = entity->x;
            entity->last_tick_y = entity->y;
            entity->last_tick_z = entity->z;
            entity->x_roto = entity->x_rot;
            entity->y_roto = entity->y_rot;
            if(world_chunk_exists(world, cx, cz)) entity->tick(entity);

            int cx2 = floor_double(entity->x / CHUNK_SIZE_WIDTH);
            int cy2 = floor_double(entity->y / CHUNK_SIZE_WIDTH);
            int cz2 = floor_double(entity->z / CHUNK_SIZE_WIDTH);

            if(cx2 != cx || cy2 != cy || cz2 != cz) {
                if(world_chunk_exists(world, cx, cz)) {
                    chunk_t *old_chunk = world_get_chunk(world, cx, cz);
                    chunk_remove_entity_index(old_chunk, entity, cy);
                }
                if(world_chunk_exists(world, cx2, cz2)) {
                    chunk_t *new_chunk = world_get_chunk(world, cx2, cz2);
                    chunk_add_entity(new_chunk, entity);
                }else {
                    entity->is_dead = 1;
                }
            }
        }

        if(entity->is_dead) {
            int cx = floor_double(entity->x / CHUNK_SIZE_WIDTH);
            int cz = floor_double(entity->z / CHUNK_SIZE_WIDTH);
            
            if(world_chunk_exists(world, cx, cz)) {
                chunk_t *chunk = world_get_chunk(world, cx, cz);
                chunk_remove_entity_index(chunk, entity, floor_double(entity->y / CHUNK_SIZE_WIDTH));
            }

            world->loaded_entity_list = array_list_remove(world->loaded_entity_list, i);
            i--;

            for(int j = 0; j < array_list_length(world->renderer_world_list); j++) {
                renderer_world_t *renderer = *(renderer_world_t **)array_list_get(world->renderer_world_list, j);
                renderer_world_release_entity_skin(renderer, entity);
            }
        }
    }

    for(int i = 0; i < array_list_length(world->loaded_tile_entity_list); i++) {
        tile_entity_t *tile_entity = *(tile_entity_t **)array_list_get(world->loaded_tile_entity_list, i);
        tile_entity->update(tile_entity);
    }
}

uint8_t world_is_aabb_clear(world_t *world, AABB_t box) {
    entity_t **entities = world_get_entities_excluding(world, NULL, box);
    for(int i = 0; i < array_list_length(entities); i++) {
        entity_t *entity = *(entity_t **)array_list_get(entities, i);
        if(entity->prevents_spawning) {
            array_list_free(entities);
            return 0;
        }
    }

    array_list_free(entities);

    return 1;
}

uint8_t world_is_liquid_in_range(world_t *world, AABB_t box) {
    int x0 = floor_double(box.x0);
    int y0 = floor_double(box.y0);
    int z0 = floor_double(box.z0);
    int x1 = floor_double(box.x1 + 1);
    int y1 = floor_double(box.y1 + 1);
    int z1 = floor_double(box.z1 + 1);

    if(box.x0 < 0.0) x0--;
    if(box.y0 < 0.0) y0--;
    if(box.z0 < 0.0) z0--;

    for(int i = x0; i < x1; i++) {
        for(int j = y0; j < y1; j++) {
            for(int k = z0; k < z1; k++) {
                block_t *block = &block_list[world_get_block(world, i, j, k)];
                if(block->id != blocks.air.id && block->material->is_liquid) return 1;
            }
        }
    }

    return 0;
}

uint8_t world_is_bounding_box_burning(world_t *world, AABB_t box) {
    int x0 = floor_double(box.x0);
    int y0 = floor_double(box.y0);
    int z0 = floor_double(box.z0);
    int x1 = floor_double(box.x1 + 1);
    int y1 = floor_double(box.y1 + 1);
    int z1 = floor_double(box.z1 + 1);

    for(int i = x0; i < x1; i++) {
        for(int j = y0; j < y1; j++) {
            for(int k = z0; k < z1; k++) {
                block_t *block = &block_list[world_get_block(world, i, j, k)];
                if(block->id == BLOCK_FIRE || block->id == BLOCK_LAVA || block->id == BLOCK_STILL_LAVA) return 1;
            }
        }
    }

    return 0;
}

uint8_t world_is_material_in_box(world_t *world, AABB_t box, material_t *material) {
    int x0 = floor_double(box.x0);
    int y0 = floor_double(box.y0);
    int z0 = floor_double(box.z0);
    int x1 = floor_double(box.x1 + 1);
    int y1 = floor_double(box.y1 + 1);
    int z1 = floor_double(box.z1 + 1);

    for(int i = x0; i < x1; i++) {
        for(int j = y0; j < y1; j++) {
            for(int k = z0; k < z1; k++) {
                block_t *block = &block_list[world_get_block(world, i, j, k)];
                if(block->id != blocks.air.id && block->material == material) return 1;
            }
        }
    }

    return 0;
}

void world_explode(world_t *world, entity_t *source_entity, double x, double y, double z, float radius) {
    world_play_sound(world, x, y, z, SOUND_RANDOM_EXPLODE, 4.0, (1.0 + (random_next_float(&world->random) - random_next_float(&world->random)) * 0.2) * 0.7);
    vec3_t *affected_blocks = array_list_create(sizeof(vec3_t));

    for(int x_offset = 0; x_offset < CHUNK_SIZE_WIDTH; x_offset++) {
        for(int y_offset = 0; y_offset < CHUNK_SIZE_WIDTH; y_offset++) {
            for(int z_offset = 0; z_offset < CHUNK_SIZE_WIDTH; z_offset++) {
                if(x_offset == 0 || x_offset == CHUNK_SIZE_WIDTH - 1
                   || y_offset == 0 || y_offset == CHUNK_SIZE_WIDTH - 1
                   || z_offset == 0 || z_offset == CHUNK_SIZE_WIDTH - 1) {
                    double x_norm = x_offset / (CHUNK_SIZE_WIDTH - 1) * 2.0 - 1.0;
                    double y_norm = y_offset / (CHUNK_SIZE_WIDTH - 1) * 2.0 - 1.0;
                    double z_norm = z_offset / (CHUNK_SIZE_WIDTH - 1) * 2.0 - 1.0;
                    double distance = sqrt(x_norm * x_norm + y_norm * y_norm + z_norm * z_norm);

                    x_norm /= distance;
                    y_norm /= distance;
                    z_norm /= distance;

                    float strength = radius * (0.7 + random_next_float(&world->random) * 0.6);
                    double x_ray = x;
                    double y_ray = y;
                    double z_ray = z;
                    
                    for(; strength > 0; strength -= 0.22500001) {
                        int block_x = floor_double(x_ray);
                        int block_y = floor_double(y_ray);
                        int block_z = floor_double(z_ray);

                        uint8_t block_id = world_get_block(world, block_x, block_y, block_z);
                        if(block_id > 0) {
                            strength -= ((block_list[block_id].resistance / 5.0) + 0.3) * 0.3;
                        }

                        if(strength > 0) {
                            vec3_t pos = { block_x, block_y, block_z };
                            affected_blocks = array_list_push(affected_blocks, &pos);
                        }

                        x_ray += x_norm * 0.3;
                        y_ray += y_norm * 0.3;
                        z_ray += z_norm * 0.3;
                    }
                }
            }
        }
    }

    radius *= 2;
    int x0 = floor_double(x - radius) - 1;
    int x1 = floor_double(x + radius) + 1;
    int y0 = floor_double(y - radius) - 1;
    int y1 = floor_double(y + radius) + 1;
    int z0 = floor_double(z - radius) - 1;
    int z1 = floor_double(z + radius) + 1;

    entity_t **affected_entities = world_get_entities_excluding(world, source_entity, (AABB_t){ x0, y0, z0, x1, y1, z1 });
    vec3_t origin = { x, y, z };

    for(int i = 0; i < array_list_length(affected_entities); i++) {
        entity_t *entity = *(entity_t **)array_list_get(affected_entities, i);
        double dx = entity->x - x;
        double dy = entity->y - y;
        double dz = entity->z - z;
        double distance = sqrt(dx * dx + dy * dy + dz * dz);

        if(distance <= 1) {
            dx /= distance;
            dy /= distance;
            dz /= distance;

            double density = world_get_block_density(world, origin, entity->bb);
            double impact = (1 - distance) * density;

            entity_hurt(entity, source_entity, (impact * impact + impact) / 2.0 * 8.0 * radius + 1);
            entity->xd += dx * impact;
            entity->yd += dy * impact;
            entity->zd += dz * impact;
        }
    }

    array_list_free(affected_entities);

    for(int i = 0; i < array_list_length(affected_blocks); i++) {
        vec3_t *pos = array_list_get(affected_blocks, i);
        int block_x = pos->x;
        int block_y = pos->y;
        int block_z = pos->z;

        uint8_t block_id = world_get_block(world, block_x, block_y, block_z);
        for(int j = 0; j < 1; j++) {
            double particle_x = block_x + random_next_float(&world->random);
            double particle_y = block_y + random_next_float(&world->random);
            double particle_z = block_z + random_next_float(&world->random);
            double vel_x = particle_x - x;
            double vel_y = particle_y - y;
            double vel_z = particle_z - z;

            double magnitude = sqrt(vel_x * vel_x + vel_y * vel_y + vel_z * vel_z);
            vel_x /= magnitude;
            vel_y /= magnitude;
            vel_z /= magnitude;

            double particle_vel = 0.5 / (magnitude / radius + 0.1);
            particle_vel *= (random_next_float(&world->random) * random_next_float(&world->random) + 0.3);
            vel_x *= particle_vel;
            vel_y *= particle_vel;
            vel_z *= particle_vel;

            world_spawn_particle(world, PARTICLE_EXPLODE, (particle_x + x) / 2.0, (particle_y + y) / 2.0, (particle_z + z) / 2.0, vel_x, vel_y, vel_z);
            world_spawn_particle(world, PARTICLE_SMOKE, particle_x, particle_y, particle_z, vel_x, vel_y, vel_z);
        }

        if(block_id > 0) {
            block_t *block = &block_list[block_id];
            block_spawn_items_chance(block, world, x, y, z, world_get_block_metadata(world, block_x, block_y, block_z), 0.3);
            world_set_block_with_update(world, block_x, block_y, block_z, blocks.air.id);
            block->on_exploded(block, world, x, y, z);
        }
    }

    array_list_free(affected_blocks);
}

float world_get_block_density(world_t *world, vec3_t pos, AABB_t box) {
    double x = 1.0 / ((box.x1 - box.x0) * 2.0 + 1);
    double y = 1.0 / ((box.y1 - box.y0) * 2.0 + 1);
    double z = 1.0 / ((box.z1 - box.z0) * 2.0 + 1);

    int unobstructed_points = 0;
    int total_points = 0;

    for(float x_offset = 0; x_offset <= 1; x_offset = x_offset + x) {
        for(float y_offset = 0; y_offset <= 1; y_offset = y_offset + y) {
            for(float z_offset = 0; z_offset <= 1; z_offset = z_offset + z) {
                vec3_t sample_pos = { box.x0 + x_offset, box.y0 + y_offset, box.z0 + z_offset };
                if(world_clip(world, sample_pos, pos).null == 1) {
                    unobstructed_points++;
                }
                total_points++;
            }
        }
    }
    return (float)unobstructed_points / (float)total_points;
}

void world_extinguish_fire(world_t *world, int x, int y, int z, uint8_t side) {
    if(side == 0) y--;
    if(side == 1) y++;
    if(side == 2) z--;
    if(side == 3) z++;
    if(side == 4) x--;
    if(side == 5) x++;

    if(world_get_block(world, x, y, z) == BLOCK_FIRE) {
        world_play_sound(world, x + 0.5, y + 0.5, z + 0.5, SOUND_RANDOM_FIZZ, 0.5, 2.6 + (random_next_double(&world->random) - random_next_double(&world->random)) * 0.8);
        world_set_block_with_update(world, x, y, z, blocks.air.id);
    }
}

tile_entity_t *world_get_tile_entity(world_t *world, int x, int y, int z) {
    chunk_t *chunk = world_get_chunk(world, x >> 4, z >> 4);
    return chunk != NULL ? chunk_get_tile_entity(chunk, x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1)) : NULL;
}

void world_set_tile_entity(world_t *world, int x, int y, int z, tile_entity_t *tile_entity) {
    chunk_t *chunk = world_get_chunk(world, x >> 4, z >> 4);
    if(chunk != NULL) {
        chunk_set_tile_entity(chunk, x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1), tile_entity);
    }
}

void world_remove_tile_entity(world_t *world, int x, int y, int z) {
    chunk_t *chunk = world_get_chunk(world, x >> 4, z >> 4);
    if(chunk != NULL) {
        chunk_remove_tile_entity(chunk, x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1));
    }
}

void world_update_chunk(world_t *world, int x, int y, int z) {
    if(world_block_exists(world, x, y, z)) {
        chunk_t *chunk = world_get_chunk(world, x >> 4, z >> 4);
        chunk->is_modified = 1;
    }
}

uint8_t world_is_solid(world_t *world, int x, int y, int z) {
    uint8_t block_id = world_get_block(world, x, y, z);
    return block_list[block_id].is_opaque;
}

typedef struct {
    int x;
    int y;
    int z;
    uint8_t value;
} light_node_t;

typedef struct {
    light_node_t *nodes;
    size_t count;
    size_t capacity;
    size_t head;
} light_queue_t;

static light_queue_t light_add_queue;
static light_queue_t light_remove_queue;

static const int light_directions[6][3] = {
    { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }
};

static void light_queue_push(light_queue_t *queue, int x, int y, int z, uint8_t value) {
    if(queue->count == queue->capacity) {
        size_t capacity = queue->capacity == 0 ? 4096 : queue->capacity * 2;
        light_node_t *nodes = realloc(queue->nodes, capacity * sizeof(light_node_t));
        if(nodes == NULL) {
            fprintf(stderr, "Out of memory in light queue\n");
            exit(1);
        }
        queue->nodes = nodes;
        queue->capacity = capacity;
    }
    queue->nodes[queue->count++] = (light_node_t){ x, y, z, value };
}

static uint8_t light_queue_pop(light_queue_t *queue, light_node_t *node) {
    if(queue->head >= queue->count) {
        queue->head = 0;
        queue->count = 0;
        return 0;
    }
    *node = queue->nodes[queue->head++];
    return 1;
}

// Returns the loaded chunk holding the position or NULL
static chunk_t *light_get_chunk(world_t *world, int x, int y, int z) {
    if(x < -WORLD_MAX_SIZE || x >= WORLD_MAX_SIZE
        || y < 0 || y >= CHUNK_SIZE_HEIGHT
        || z < -WORLD_MAX_SIZE || z > WORLD_MAX_SIZE) {
        return NULL;
    }
    if(!world_chunk_exists(world, x >> 4, z >> 4)) return NULL;
    return world_get_chunk(world, x >> 4, z >> 4);
}

static int light_get(chunk_t *chunk, uint8_t light_type, int x, int y, int z) {
    return chunk_get_saved_light_value(chunk, light_type, x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1));
}

// Section range last sent to the renderers, repeats are no-ops so skip them
static int light_last_sections[6];
static uint8_t light_last_valid = 0;

static void light_set(world_t *world, chunk_t *chunk, uint8_t light_type, int x, int y, int z, int value) {
    chunk_set_light_value(chunk, light_type, x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1), value);

    int sections[6] = { (x - 1) >> 4, (y - 1) >> 4, (z - 1) >> 4, (x + 1) >> 4, (y + 1) >> 4, (z + 1) >> 4 };
    if(light_last_valid && memcmp(sections, light_last_sections, sizeof(sections)) == 0) return;
    memcpy(light_last_sections, sections, sizeof(sections));
    light_last_valid = 1;

    for(int i = 0; i < array_list_length(world->renderer_world_list); i++) {
        renderer_world_t *renderer = *(renderer_world_t **)array_list_get(world->renderer_world_list, i);
        renderer_world_update_block(renderer, x, y, z);
    }
}

// Light a position produces by itself
static int light_emission(chunk_t *chunk, uint8_t light_type, int x, int y, int z) {
    int lx = x & (CHUNK_SIZE_WIDTH - 1);
    int lz = z & (CHUNK_SIZE_WIDTH - 1);
    if(light_type == LIGHT_TYPE_SKY) {
        return chunk_can_block_see_sky(chunk, lx, y, lz) ? 15 : 0;
    }
    return block_list[chunk_get_block_id(chunk, lx, y, lz)].light_value;
}

static int light_opacity(chunk_t *chunk, int x, int y, int z) {
    int opacity = block_list[chunk_get_block_id(chunk, x & (CHUNK_SIZE_WIDTH - 1), y, z & (CHUNK_SIZE_WIDTH - 1))].light_opacity;
    return opacity == 0 ? 1 : opacity;
}

// Two pass flood fill, first removes light that lost its source then spreads light from every source
static void light_process_region(world_t *world, chunk_metadata_t *region) {
    uint8_t sync_only = (region->light_type & LIGHT_SYNC_FLAG) != 0;
    uint8_t light_type = region->light_type & ~LIGHT_SYNC_FLAG;
    light_node_t node;

    // Seed from the dirty region
    for(int x = region->x; x <= region->max_x; x++) {
        for(int z = region->z; z <= region->max_z; z++) {
            for(int y = region->y; y <= region->max_y; y++) {
                chunk_t *chunk = light_get_chunk(world, x, y, z);
                if(chunk == NULL) continue;

                // Border sync only spreads the light already stored
                if(sync_only) {
                    if(light_get(chunk, light_type, x, y, z) > 0) {
                        light_queue_push(&light_add_queue, x, y, z, 0);
                    }
                    continue;
                }

                int old_value = light_get(chunk, light_type, x, y, z);
                int emission = light_emission(chunk, light_type, x, y, z);
                // Column relighting overwrites sky values in place, so assume the worst for shadowed cells
                int removed_value = old_value;
                if(light_type == LIGHT_TYPE_SKY && emission < 15) removed_value = 15;
                if(removed_value > emission) {
                    light_queue_push(&light_remove_queue, x, y, z, removed_value);
                }
                if(old_value != emission) {
                    light_set(world, chunk, light_type, x, y, z, emission);
                }
                if(emission > 0) {
                    light_queue_push(&light_add_queue, x, y, z, emission);
                }

                // Lit neighbors may flow into this position
                for(int d = 0; d < 6; d++) {
                    int nx = x + light_directions[d][0];
                    int ny = y + light_directions[d][1];
                    int nz = z + light_directions[d][2];
                    chunk_t *neighbor = light_get_chunk(world, nx, ny, nz);
                    if(neighbor != NULL && light_get(neighbor, light_type, nx, ny, nz) > 0) {
                        light_queue_push(&light_add_queue, nx, ny, nz, 0);
                    }
                }
            }
        }
    }

    // Remove light that is no longer supported
    while(light_queue_pop(&light_remove_queue, &node)) {
        for(int d = 0; d < 6; d++) {
            int nx = node.x + light_directions[d][0];
            int ny = node.y + light_directions[d][1];
            int nz = node.z + light_directions[d][2];
            chunk_t *neighbor = light_get_chunk(world, nx, ny, nz);
            if(neighbor == NULL) continue;

            int neighbor_value = light_get(neighbor, light_type, nx, ny, nz);
            if(neighbor_value == 0) continue;

            int emission = light_emission(neighbor, light_type, nx, ny, nz);
            if(neighbor_value < node.value && emission < neighbor_value) {
                light_set(world, neighbor, light_type, nx, ny, nz, emission);
                light_queue_push(&light_remove_queue, nx, ny, nz, neighbor_value);
                if(emission > 0) {
                    light_queue_push(&light_add_queue, nx, ny, nz, emission);
                }
            }else {
                light_queue_push(&light_add_queue, nx, ny, nz, neighbor_value);
            }
        }
    }

    // Spread light from all sources
    while(light_queue_pop(&light_add_queue, &node)) {
        chunk_t *chunk = light_get_chunk(world, node.x, node.y, node.z);
        if(chunk == NULL) continue;

        int value = light_get(chunk, light_type, node.x, node.y, node.z);
        if(value <= 1) continue;

        for(int d = 0; d < 6; d++) {
            int nx = node.x + light_directions[d][0];
            int ny = node.y + light_directions[d][1];
            int nz = node.z + light_directions[d][2];
            chunk_t *neighbor = light_get_chunk(world, nx, ny, nz);
            if(neighbor == NULL) continue;

            int new_value = value - light_opacity(neighbor, nx, ny, nz);
            if(new_value > light_get(neighbor, light_type, nx, ny, nz)) {
                light_set(world, neighbor, light_type, nx, ny, nz, new_value);
                light_queue_push(&light_add_queue, nx, ny, nz, new_value);
            }
        }
    }
}

uint8_t world_update_lighting(world_t *world) {
    int regions_left = 20000;
    light_last_valid = 0;

    while(array_list_length(world->lighting_update_list) > 0) {
        if(regions_left-- <= 0) return 1;

        int length = array_list_length(world->lighting_update_list);
        chunk_metadata_t *region = *(chunk_metadata_t **)array_list_get(world->lighting_update_list, length - 1);
        world->lighting_update_list = array_list_remove(world->lighting_update_list, length - 1);

        light_process_region(world, region);
        free(region);
    }

    return 0;
}

// Lets light flow across the borders of a newly loaded chunk and its loaded neighbors
void world_schedule_chunk_border_light(world_t *world, int chunk_x, int chunk_z) {
    int x0 = chunk_x * CHUNK_SIZE_WIDTH;
    int z0 = chunk_z * CHUNK_SIZE_WIDTH;
    int x1 = x0 + CHUNK_SIZE_WIDTH - 1;
    int z1 = z0 + CHUNK_SIZE_WIDTH - 1;
    uint8_t types[2] = { LIGHT_TYPE_SKY | LIGHT_SYNC_FLAG, LIGHT_TYPE_BLOCK | LIGHT_SYNC_FLAG };

    for(int i = 0; i < 2; i++) {
        world_schedule_light_update(world, types[i], x0 - 1, 0, z0, x0, CHUNK_SIZE_HEIGHT - 1, z1);
        world_schedule_light_update(world, types[i], x1, 0, z0, x1 + 1, CHUNK_SIZE_HEIGHT - 1, z1);
        world_schedule_light_update(world, types[i], x0, 0, z0 - 1, x1, CHUNK_SIZE_HEIGHT - 1, z0);
        world_schedule_light_update(world, types[i], x0, 0, z1, x1, CHUNK_SIZE_HEIGHT - 1, z1 + 1);
    }
}

void world_schedule_light_update(world_t *world, uint8_t light_type, int x0, int y0, int z0, int x1, int y1, int z1) {
    int update_count = array_list_length(world->lighting_update_list);
    int max_checks = 4;

    if(max_checks > update_count) max_checks = update_count;

    // Skip if a recent update already covers the area
    for(int i = 0; i < max_checks; i++) {
        chunk_metadata_t *region = *(chunk_metadata_t **)array_list_get(world->lighting_update_list, update_count - i - 1);
        if(region->light_type == light_type
            && x0 >= region->x && y0 >= region->y && z0 >= region->z
            && x1 <= region->max_x && y1 <= region->max_y && z1 <= region->max_z) {
            return;
        }
    }

    chunk_metadata_t *new_region = malloc(sizeof(chunk_metadata_t));
    *new_region = (chunk_metadata_t){ light_type, x0, y0, z0, x1, y1, z1 };
    world->lighting_update_list = array_list_push(world->lighting_update_list, &new_region);
}

void world_restart_time_of_day(world_t *world) {
    world->chunk_provider.unload_oldest_chunks(&world->chunk_provider);
    if(!array_list_contains(world->loaded_entity_list, &world->player)) {
        world_spawn_entity(world, world->player->mob->entity);
    }

    float angle = world_get_celestial_angle(world, 1.0);
    angle = 1.0 - (tcos(angle * M_PI * 2.0) * 2.0 + 0.5);
    if(angle < 0) angle = 0;
    if(angle > 1) angle = 1;

    int light = angle * 13;
    if(light != world->skylight_subtracted) {
        world->skylight_subtracted = light;
        
        for(int i = 0; i < array_list_length(world->renderer_world_list); i++) {
            renderer_world_t *renderer = *(renderer_world_t **)array_list_get(world->renderer_world_list, i);
            renderer_world_update_all(renderer);
        }
    }

    world->world_time++;
    if(world->world_time % 100 == 0) {
        world_save(world, 0);
    }

    int next_tick_size = array_list_length(world->next_tick_data_list);
    if(next_tick_size > 200) {
        next_tick_size = 200;
    }

    for(int i = 0; i < next_tick_size; i++) {
        next_tick_data_t next_tick = *(next_tick_data_t *)array_list_get(world->next_tick_data_list, 0);
        world->next_tick_data_list = array_list_remove(world->next_tick_data_list, 0);
        if(next_tick.delay > 0) {
            next_tick.delay--;
            world->next_tick_data_list = array_list_push(world->next_tick_data_list, &next_tick);
        }else if(world_block_exists(world, next_tick.x, next_tick.y, next_tick.z)) {
            uint8_t block_id = world_get_block(world, next_tick.x, next_tick.y, next_tick.z);
            if(block_id == next_tick.block_id && block_id > 0) {
                block_t *block = &block_list[block_id];
                block->update(block, world, next_tick.x, next_tick.y, next_tick.z, &world->random);
            }
        }
    }

    int x = floor_double(world->player->x);
    int z = floor_double(world->player->z);

    for(int i = 0; i < 32000; i++) {
        world->random_number = world->random_number * 3 + HASH_MAGIC;
        int yy = world->random_number >> 2;
        int xx = (yy & 0xFF) - 128 + x;
        int zz = (yy >> 8 & 0xFF) - 128 + z;
        yy = yy >> 16 & 127;
        if(!world_chunk_exists(world, xx >> 4, zz >> 4)) continue;
        uint8_t block_id = world_get_block(world, xx, yy, zz);
        if(block_list[block_id].should_tick) {
            block_list[block_id].update(&block_list[block_id], world, xx, yy, zz, &world->random);
        }
    }
}

void world_visual_update(world_t *world, int x, int y, int z) {
    random_t random = random_create(time(NULL));

    for(int i = 0; i < 1000; i++) {
        int xx = x + random_next_int_range(&random, CHUNK_SIZE_WIDTH) - random_next_int_range(&random, CHUNK_SIZE_WIDTH);
        int yy = y + random_next_int_range(&random, CHUNK_SIZE_WIDTH) - random_next_int_range(&random, CHUNK_SIZE_WIDTH);
        int zz = z + random_next_int_range(&random, CHUNK_SIZE_WIDTH) - random_next_int_range(&random, CHUNK_SIZE_WIDTH);
        uint8_t block_id = world_get_block(world, xx, yy, zz);
        if(block_id > 0) {
            block_list[block_id].visual_update(&block_list[block_id], world, xx, yy, zz, &random);
        }
    }
}

entity_t **world_get_entities_excluding(world_t *world, entity_t *entity, AABB_t box) {
    int x0 = floor_double((box.x0 - 2) / CHUNK_SIZE_WIDTH);
    int x1 = floor_double((box.x1 + 2) / CHUNK_SIZE_WIDTH);
    int z0 = floor_double((box.z0 - 2) / CHUNK_SIZE_WIDTH);
    int z1 = floor_double((box.z1 + 2) / CHUNK_SIZE_WIDTH);
    entity_t **entity_list = array_list_create(sizeof(entity_t *));

    for(int x = x0; x <= x1; x++) {
        for(int z = z0; z <= z1; z++) {
            if(world_chunk_exists(world, x, z)) {
                chunk_t *chunk = world_get_chunk(world, x, z);
                chunk_get_entities(chunk, entity, box, &entity_list);
            }
        }
    }

    return entity_list;
}

void world_add_loaded_entities(world_t *world, entity_t **entity_list) {
    for(int i = 0; i < array_list_length(entity_list); i++) {
        entity_t *entity = *(entity_t **)array_list_get(entity_list, i);
        world->loaded_entity_list = array_list_push(world->loaded_entity_list, &entity);
    }

    for(int i = 0; i < array_list_length(world->renderer_world_list); i++) {
        renderer_world_t *world_renderer = *(renderer_world_t **)array_list_get(world->renderer_world_list, i);
        for(int j = 0; j < array_list_length(entity_list); j++) {
            entity_t *entity = *(entity_t **)array_list_get(entity_list, j);
            renderer_world_obtain_entity_skin(world_renderer, entity);
        }
    }
}

void world_unload_entities(world_t *world, entity_t **entity_list) {
    for(int i = 0; i < array_list_length(entity_list); i++) {
        entity_t *entity = *(entity_t **)array_list_get(entity_list, i);
        int index = array_list_index_of(world->loaded_entity_list, &entity);
        world->loaded_entity_list = array_list_remove(world->loaded_entity_list, index);
    }

    for(int i = 0; i < array_list_length(world->renderer_world_list); i++) {
        renderer_world_t *world_renderer = *(renderer_world_t **)array_list_get(world->renderer_world_list, i);
        for(int j = 0; j < array_list_length(entity_list); j++) {
            entity_t *entity = *(entity_t **)array_list_get(entity_list, j);
            renderer_world_release_entity_skin(world_renderer, entity);
        }
    }
}

void world_set_spawn_position(world_t *world, int x, int y, int z) {
    world->spawn_x = x;
    world->spawn_y = y;
    world->spawn_z = z;
}

void world_destroy(world_t *world) {
    chunk_provider_load_save_chunks(&world->chunk_provider, 1);

    for(int i = 0; i < CHUNK_PROVIDER_ARRAY_SIZE; i++) {
        chunk_t *chunk = world->chunk_provider.chunks[i];
        if(chunk != NULL) {
            chunk_unload_entities(chunk);
            chunk_destroy(chunk);
            free(chunk);
            world->chunk_provider.chunks[i] = NULL;
        }
    }

    free(world->chunk_provider_gen.noise_array);
    world->chunk_provider_gen.noise_array = NULL;
    free(world->chunk_provider_gen.noise_array_1);
    world->chunk_provider_gen.noise_array_1 = NULL;
    free(world->chunk_provider_gen.noise_array_2);
    world->chunk_provider_gen.noise_array_2 = NULL;
    free(world->chunk_provider_gen.noise_array_3);
    world->chunk_provider_gen.noise_array_3 = NULL;

    if(world->chunk_provider_gen.noise_1.destroy != NULL) {
        world->chunk_provider_gen.noise_1.destroy(&world->chunk_provider_gen.noise_1);
    }
    if(world->chunk_provider_gen.noise_2.destroy != NULL) {
        world->chunk_provider_gen.noise_2.destroy(&world->chunk_provider_gen.noise_2);
    }
    if(world->chunk_provider_gen.noise_3.destroy != NULL) {
        world->chunk_provider_gen.noise_3.destroy(&world->chunk_provider_gen.noise_3);
    }
    if(world->chunk_provider_gen.noise_4.destroy != NULL) {
        world->chunk_provider_gen.noise_4.destroy(&world->chunk_provider_gen.noise_4);
    }
    if(world->chunk_provider_gen.noise_5.destroy != NULL) {
        world->chunk_provider_gen.noise_5.destroy(&world->chunk_provider_gen.noise_5);
    }
    if(world->chunk_provider_gen.tree_noise.destroy != NULL) {
        world->chunk_provider_gen.tree_noise.destroy(&world->chunk_provider_gen.tree_noise);
    }

    array_list_free(world->lighting_update_list);
    array_list_free(world->loaded_entity_list);
    array_list_free(world->next_tick_data_list);
    array_list_free(world->loaded_tile_entity_list);
    array_list_free(world->renderer_world_list);
}