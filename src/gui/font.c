#include <gui/font.h>
#include <renderer/textures.h>
#include <renderer/tesselator.h>

#include <util/string.h>
#include <assets/fonts/default.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>

// Texels rasterized per game unit
#define FONT_SCALE 4
// Target metrics of the game text grid in game units, taken from the old 8x8 bitmap font
#define FONT_CAP_UNITS 7
#define FONT_BASELINE_UNITS 7

typedef struct {
    uint8_t *pixels;
    int size;     // atlas width and height in texels
    int cell_w;   // glyph cell size in texels
    int cell_h;
    int pad_left; // texels between cell edge and glyph origin
    int pad_top;  // texels between cell top and the line top
} font_atlas_t;

static int font_next_pow2(int v) {
    int p = 1;
    while(p < v) p <<= 1;
    return p;
}

// Rasterizes the embedded TTF into a 16x16 glyph atlas, with all sizes derived from the font itself
static font_atlas_t font_build_atlas(int *width_map) {
    font_atlas_t atlas = { 0 };
    FT_Library lib;
    FT_Face face;
    if(FT_Init_FreeType(&lib)) return atlas;
    if(FT_New_Memory_Face(lib, asset_default_ttf, asset_default_ttf_size, 0, &face)) {
        FT_Done_FreeType(lib);
        return atlas;
    }

    // Scale the em so the font's cap height matches the game grid
    FT_Load_Char(face, 'H', FT_LOAD_NO_SCALE);
    long cap_units = face->glyph->metrics.height;
    long em_26_6 = (long)FONT_CAP_UNITS * FONT_SCALE * face->units_per_EM * 64 / cap_units;
    FT_Set_Char_Size(face, 0, em_26_6, 72, 72);

    int baseline = FONT_BASELINE_UNITS * FONT_SCALE;

    // Pass 1: measure the extents of every glyph to size the cells
    int max_top = 0, max_below = 0, max_right = 0, min_left = 0;
    for(int i = 0; i < 256; i++) {
        width_map[i] = 0;
        if(i < 32 || (i >= 127 && i < 160)) continue;
        if(FT_Load_Char(face, i, FT_LOAD_RENDER)) continue;

        FT_GlyphSlot g = face->glyph;
        int advance = (g->advance.x + 32) >> 6;
        if(g->bitmap_top > max_top) max_top = g->bitmap_top;
        if((int)g->bitmap.rows - g->bitmap_top > max_below) max_below = g->bitmap.rows - g->bitmap_top;
        if(g->bitmap_left + (int)g->bitmap.width > max_right) max_right = g->bitmap_left + g->bitmap.width;
        if(g->bitmap_left < min_left) min_left = g->bitmap_left;
        if(advance > max_right) max_right = advance;

        // Advance in game units
        width_map[i] = (advance + FONT_SCALE / 2) / FONT_SCALE;
    }
    width_map[32] = 4;

    atlas.pad_left = -min_left;
    atlas.pad_top = max_top > baseline ? max_top - baseline : 0;
    atlas.cell_w = atlas.pad_left + max_right;
    atlas.cell_h = atlas.pad_top + baseline + max_below;
    int largest = atlas.cell_w > atlas.cell_h ? atlas.cell_w : atlas.cell_h;
    atlas.size = font_next_pow2(largest * 16);
    atlas.pixels = calloc(atlas.size * atlas.size, 4);

    // Pass 2: draw glyph coverage into the atlas as white with alpha
    for(int i = 0; i < 256; i++) {
        if(width_map[i] == 0 || FT_Load_Char(face, i, FT_LOAD_RENDER)) continue;

        FT_GlyphSlot g = face->glyph;
        int ox = (i % 16) * atlas.cell_w + atlas.pad_left;
        int oy = (i / 16) * atlas.cell_h + atlas.pad_top + baseline - g->bitmap_top;
        for(int y = 0; y < (int)g->bitmap.rows; y++) {
            for(int x = 0; x < (int)g->bitmap.width; x++) {
                uint8_t *dst = atlas.pixels + ((oy + y) * atlas.size + ox + g->bitmap_left + x) * 4;
                dst[0] = dst[1] = dst[2] = 255;
                dst[3] = g->bitmap.buffer[y * g->bitmap.pitch + x];
            }
        }
    }

    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return atlas;
}

font_t font_create(game_settings_t *settings) {
    font_t font;
    font = (font_t){ 0 };

    font.settings = settings;

    font_atlas_t atlas = font_build_atlas(font.width_map);
    if(atlas.pixels == NULL) printf("Failed to load embedded font.\n");

    glGenTextures(1, (GLuint *)&font.texture);
    glBindTexture(GL_TEXTURE_2D, font.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas.size, atlas.size, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.pixels);
    free(atlas.pixels);

    font.render_list = glGenLists(288);

    for(int i = 0; i < 256; i++) {
        glNewList(font.render_list + i, GL_COMPILE);
        float u = (i % 16) * atlas.cell_w;
        float v = (i / 16) * atlas.cell_h;
        float x0 = -(float)atlas.pad_left / FONT_SCALE;
        float y0 = -(float)atlas.pad_top / FONT_SCALE;
        float x1 = x0 + (float)atlas.cell_w / FONT_SCALE;
        float y1 = y0 + (float)atlas.cell_h / FONT_SCALE;
        float u0 = u / atlas.size;
        float v0 = v / atlas.size;
        float u1 = (u + atlas.cell_w) / atlas.size;
        float v1 = (v + atlas.cell_h) / atlas.size;
        tesselator_begin_quads();
        tesselator_vertex_uv(x0, y1, 0.0, u0, v1);
        tesselator_vertex_uv(x1, y1, 0.0, u1, v1);
        tesselator_vertex_uv(x1, y0, 0.0, u1, v0);
        tesselator_vertex_uv(x0, y0, 0.0, u0, v0);
        tesselator_end();
        glTranslatef(font.width_map[i], 0.0, 0.0);
        glEndList();
    }

    for(int i = 0; i < 32; i++) {
        int brightness = (i & 8) << 3;
        int red = ((i & 1) & 191) + brightness;
        int green = ((i & 2) >> 1) * 191 + brightness;
        int blue = ((i & 4) >> 2) * 191 + brightness;
        uint8_t is_dark = i >= 16;

        if(font.settings->anaglyph) {
            uint8_t r = (blue * 30 + green * 59 + red * 11) / 100;
            uint8_t g = (blue * 30 + green * 70) / 100;
            uint8_t b = (blue * 30 + red * 70) / 100;
            red = r;
            green = g;
            blue = b;
        }

        if(is_dark) {
            red /= 4;
            green /= 4;
            blue /= 4;
        }

        glColor4f(red / 255.0, green / 255.0, blue / 255.0, 1.0);
    }

    return font;
}

static void private_text_render(font_t *font, char *str, int x, int y, uint32_t color, uint8_t darken) {
    if(str != NULL) {
        if(darken) color = (((color >> 8) & 0xfcfcfc) >> 2) << 8;
        glBindTexture(GL_TEXTURE_2D, font->texture);
        float red = ((color >> 16) & 0xFF) / 255.0;
        float green = ((color >> 8) & 0xFF) / 255.0;
        float blue = (color & 0xFF) / 255.0;
        glColor4f(red, green, blue, 1.0);
        glPushMatrix();
        glTranslatef(x, y, 0.0);
        for(int i = 0; i < strlen(str); i++) {
            if(str[i] == 38 && strlen(str) > i + 1) {
                char *hex = string_create("0123456789abcdef");
                int index = string_index_of(hex, str[i + 1]);
                string_free(hex);
                if(index < 0 || index > 15) index = 15;
                uint32_t color_list = font->render_list + 256 + index + (darken ? 16 : 0);
                glCallList(color_list);

                i += 2;
            }

            uint32_t char_list = font->render_list + (uint8_t)str[i];
            glCallList(char_list);
        }

        glPopMatrix();
    }
}

void font_render(font_t *font, char *string, int x, int y, uint32_t color) {
    private_text_render(font, string, x + 1, y + 1, color, 1);
    private_text_render(font, string, x, y, color, 0);
}

void font_render_no_shadow(font_t *font, char *string, int x, int y, uint32_t color) {
    private_text_render(font, string, x, y, color, 0);
}

int font_get_width(font_t *font, char *string) {
    if(string == (char *)0) {
        return 0;
    }

    int width = 0;

    for(int i = 0; i < strlen(string); i++) {
        if(string[i] == '&')
            i++;
        else
            width += font->width_map[(int)string[i]];
    }

    return width;
}

char *font_strip_color(char *string) {
    char *new_string = string_create(string);
    for(int i = 0; i < strlen(string); i++) {
        if(string[i] == '&')
            i++;
        else
            string_concat(&new_string, (char[]){ string[i], '\0' });
    }

    return new_string;
}