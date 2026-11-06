#include <gui/screen_title.h>

#include <gui/screen_options.h>
#include <gui/screen_select_world.h>
#include <infcube.h>
#include <renderer/tesselator.h>
#include <renderer/renderer_block.h>
#include <world/block/blocks.h>
#include <util/stats.h>
#include <util/time.h>

#include <GL/gl.h>
#include <GL/glu.h>

char *splashes[] = { "Pre-beta!", "As seen on TV!", "Awesome!", "100% pure!", "May contain nuts!", "Better than Prey!", "More polygons!", "Sexy!", "Limited edition!", "Flashing letters!", "Made by Notch!", "Coming soon!", "Best in class!", "When it\'s finished!", "Absolutely dragon free!", "Excitement!", "More than 5000 sold!", "One of a kind!", "700+ hits on YouTube!", "Indev!", "Spiders everywhere!", "Check it out!", "Holy cow, man!", "It\'s a game!", "Made in Sweden!", "Uses LWJGL!", "Reticulating splines!", "InfCube!", "Yaaay!", "Alpha version!", "Singleplayer!", "Keyboard compatible!", "Undocumented!", "Ingots!", "Exploding creepers!", "That\'s not a moon!", "l33t!", "Create!", "Survive!", "Dungeon!", "Exclusive!", "The bee\'s knees!", "Down with O.P.P.!", "Closed source!", "Classy!", "Wow!", "Not on steam!", "9.95 euro!", "Half price!", "Oh man!", "Check it out!", "Awesome community!", "Pixels!", "Teetsuuuuoooo!", "Kaaneeeedaaaa!", "Now with difficulty!", "Enhanced!", "90% bug free!", "Pretty!", "12 herbs and spices!", "Fat free!", "Absolutely no memes!", "Free dental!", "Ask your doctor!", "Minors welcome!", "Cloud computing!", "Legal in Finland!", "Hard to label!", "Technically good!", "Bringing home the bacon!", "Indie!", "GOTY!", "Ceci n\'est pas une title screen!", "Euclidian!", "Now in 3D!", "Inspirational!", "Herregud!", "Complex cellular automata!", "Yes, sir!", "Played by cowboys!", "OpenGL 1.1!", "Thousands of colors!", "Try it!", "Age of Wonders is better!", "Try the mushroom stew!", "Sensational!", "Hot tamale, hot hot tamale!", "Play him off, keyboard cat!", "Guaranteed!", "Macroscopic!", "Bring it on!", "Random splash!", "Call your mother!", "Monster infighting!", "Loved by millions!", "Ultimate edition!", "Freaky!", "You\'ve got a brand new key!", "Water proof!", "Uninflammable!", "Whoa, dude!", "All inclusive!", "Tell your friends!", "NP is not in P!", "Notch <3 Ez!", "Music by C418!" };

#define LOGO_COLS 34
#define LOGO_ROWS 5

// Pixel font for the InfCube logo, each * is one block
static const char *logo_pattern[LOGO_ROWS] = {
    "*** *   * **** .*** *  * ***. ****",
    ".*. **  * *... *... *  * *..* *...",
    ".*. * * * ***. *... *  * ***. ***.",
    ".*. *  ** *... *... *  * *..* *...",
    "*** *   * *... .*** .**. ***. ****",
};

typedef struct {
    double height;
    double prev_height;
    double velocity;
} logo_block_t;

static logo_block_t logo_blocks[LOGO_COLS][LOGO_ROWS];
static uint8_t logo_ready = 0;

static uint8_t logo_has_block(int col, int row) {
    char c = logo_pattern[row][col];
    return c == '*';
}

extern void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);

screen_t screen_title_create() {
    screen_t screen = screen_create();

    screen.type = SCREEN_TYPE_TITLE;
    screen.id = rand() % (sizeof(splashes) / sizeof(char *));
    logo_ready = 0;
    screen.grabs_mouse = 0;

    screen.tick = screen_title_tick;
    screen.on_open = screen_title_on_open;
    screen.on_button_clicked = screen_title_on_button_clicked;
    screen.render = screen_title_render;
    screen.on_key_pressed = screen_title_on_key_pressed;

    return screen;
}

static void logo_init() {
    for(int x = 0; x < LOGO_COLS; x++) {
        for(int y = 0; y < LOGO_ROWS; y++) {
            double start = (10 + y) + ((double)rand() / RAND_MAX) * 32.0 + x;
            logo_blocks[x][y] = (logo_block_t){ start, start, 0.0 };
        }
    }
    logo_ready = 1;
}

// Blocks fall in from far away and bounce into place
static void logo_tick() {
    for(int x = 0; x < LOGO_COLS; x++) {
        for(int y = 0; y < LOGO_ROWS; y++) {
            logo_block_t *block = &logo_blocks[x][y];
            block->prev_height = block->height;
            if(block->height > 0.0) block->velocity -= 0.6;
            block->height += block->velocity;
            block->velocity *= 0.9;
            if(block->height < 0.0) {
                block->height = 0.0;
                block->velocity = 0.0;
            }
        }
    }
}

static void logo_render(screen_t *screen, float partial_tick) {
    infcube_t *infcube = screen->infcube;
    if(!logo_ready) logo_init();

    int scale = 1;
    while(infcube->width / (scale + 1) >= 320 && infcube->height / (scale + 1) >= 240) scale++;
    int viewport_height = (int)(120.0 * scale * infcube->frame_height / infcube->height);

    glPushAttrib(GL_ENABLE_BIT | GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluPerspective(70.0, (double)infcube->frame_width / viewport_height, 0.05, 100.0);
    glViewport(0, infcube->frame_height - viewport_height, infcube->frame_width, viewport_height);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    renderer_block_t renderer = renderer_block_create(NULL);
    block_t *stone = &blocks.stone;

    // Pass 0 is the shadow, 1 the textured blocks and 2 a additive shine
    for(int pass = 0; pass < 3; pass++) {
        glPushMatrix();
        glTranslatef(0.4, 0.6, -13.0);
        if(pass == 0) {
            glClear(GL_DEPTH_BUFFER_BIT);
            glTranslatef(0.0, -0.4, 0.0);
            glScalef(0.98, 1.0, 1.0);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else if(pass == 1) {
            glDisable(GL_BLEND);
            glClear(GL_DEPTH_BUFFER_BIT);
        } else {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_COLOR, GL_ONE);
        }

        glScalef(1.0, -1.0, 1.0);
        glRotatef(15.0, 1.0, 0.0, 0.0);
        glScalef(0.89, 1.0, 0.4);
        glTranslatef(-LOGO_COLS * 0.5, -LOGO_ROWS * 0.5, 0.0);

        if(pass == 0) {
            glDisable(GL_TEXTURE_2D);
        } else {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textures_load(&infcube->textures, "terrain.png"));
        }

        for(int y = 0; y < LOGO_ROWS; y++) {
            for(int x = 0; x < LOGO_COLS; x++) {
                if(!logo_has_block(x, y)) continue;

                logo_block_t *block = &logo_blocks[x][y];
                float offset = block->prev_height + (block->height - block->prev_height) * partial_tick;
                float size = 1.0;
                float brightness = 1.0;
                float alpha = 1.0;
                if(pass == 0) {
                    size = offset * 0.04 + 1.0;
                    brightness = 0.0;
                    alpha = 0.7;
                    offset = 0.0;
                }

                glPushMatrix();
                glTranslatef(x, y, offset);
                glScalef(size, size, size);
                renderer_block_render_logo_cube(&renderer, stone, brightness, alpha);
                glPopMatrix();
            }
        }
        glPopMatrix();
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

void screen_title_tick(screen_t *screen) {
    screen->counter_f += 0.01;
    if(!logo_ready) logo_init();
    logo_tick();
}

void screen_title_on_open(screen_t *screen) {
    screen->buttons = array_list_clear(screen->buttons);
    button_t button_singleplayer = button_create(1, screen->width / 2 - 100, screen->height / 4 + 48, "Single player");
    button_t button_multiplayer = button_create(2, screen->width / 2 - 100, screen->height / 4 + 72, "Multi player");
    button_t button_tutorial = button_create(3, screen->width / 2 - 100, screen->height / 4 + 96, "Play tutorial level");
    button_t button_options = button_create(0, screen->width / 2 - 100, screen->height / 4 + 120 + 12, "Options...");
    button_multiplayer.active = 0;
    button_tutorial.active = 0;
    screen->buttons = array_list_push(screen->buttons, &button_singleplayer);
    screen->buttons = array_list_push(screen->buttons, &button_multiplayer);
    screen->buttons = array_list_push(screen->buttons, &button_tutorial);
    screen->buttons = array_list_push(screen->buttons, &button_options);
}

void screen_title_on_button_clicked(screen_t *screen, button_t *button) {
    if(button->id == 0) {
        screen_t *options = malloc(sizeof(screen_t));
        *options = screen_options_create(screen, &screen->infcube->settings);
        infcube_set_current_screen(screen->infcube, (screen_t *)options);
    }
    if(button->id == 1) {
        screen_t *select_world = malloc(sizeof(screen_t));
        *select_world = screen_select_world_create(screen);
        infcube_set_current_screen(screen->infcube, (screen_t *)select_world);
    }
}

void screen_title_render(screen_t *screen, int mouse_x, int mouse_y, float partial_tick) {
    screen_render_background(screen);
    logo_render(screen, partial_tick);
    glColor4f(1.0, 1.0, 1.0, 1.0);
    tesselator_color_opaque_int(0xffffffff);
    glPushMatrix();
    glTranslatef((screen->width / 2) + 130, 75, 0);
    glRotatef(-20.0, 0.0, 0.0, 1.0);
    float splash_scale = 1.8 - fabs(tsin((time_millis() % 1000) / 1000.0 * M_PI * 2) * 0.1);
    splash_scale *= 100.0 / (font_get_width(screen->font, splashes[screen->id]) + 32);
    glScalef(splash_scale, splash_scale, splash_scale);
    gui_draw_centered_string(screen->font, splashes[screen->id], 0, -8, 0xFFFF00);
    glPopMatrix();

    uint64_t max_mem = stats_get_max_memory();
    uint64_t total_mem = stats_get_total_memory();
    uint64_t free_mem = stats_get_free_memory();
    uint64_t avail_mem = max_mem - free_mem;
    char string[128];
    snprintf(string, sizeof(string),
             "Free memory: %lld%% of %lldMB",
             avail_mem * 100LL / max_mem,
             max_mem / 1024LL / 1024LL);
    gui_draw_string(screen->font, string, screen->width - font_get_width(screen->font, string) - 2, 2, 0x808080);
    memset(string, 0, sizeof(string));
    snprintf(string, sizeof(string),
             "Allocated memory: %lld%% (%lld MB)",
             total_mem * 100LL / max_mem,
             total_mem / 1024LL / 1024LL);
    gui_draw_string(screen->font, string, screen->width - font_get_width(screen->font, string) - 2, 12, 0x808080);

    screen_render(screen, mouse_x, mouse_y, partial_tick);
}

void screen_title_on_key_pressed(screen_t *screen, char event_char, int event_key) {
    return;
}