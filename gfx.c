#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 1. 純整數 256 階正弦表 (比例 1024) — 零浮點數高速 3D 運算
 * ============================================================ */
static const int16_t SIN256[256] = {
        0,    25,    50,    75,   100,   125,   150,   175,   200,   224,   249,   273,   297,   321,   345,   369,
      392,   415,   438,   460,   483,   505,   526,   548,   569,   590,   610,   630,   650,   669,   688,   706,
      724,   742,   759,   775,   792,   807,   822,   837,   851,   865,   878,   891,   903,   915,   926,   936,
      946,   955,   964,   972,   980,   987,   993,   999,  1004,  1009,  1013,  1016,  1019,  1021,  1023,  1024,
     1024,  1024,  1023,  1021,  1019,  1016,  1013,  1009,  1004,   999,   993,   987,   980,   972,   964,   955,
      946,   936,   926,   915,   903,   891,   878,   865,   851,   837,   822,   807,   792,   775,   759,   742,
      724,   706,   688,   669,   650,   630,   610,   590,   569,   548,   526,   505,   483,   460,   438,   415,
      392,   369,   345,   321,   297,   273,   249,   224,   200,   175,   150,   125,   100,    75,    50,    25,
        0,   -25,   -50,   -75,  -100,  -125,  -150,  -175,  -200,  -224,  -249,  -273,  -297,  -321,  -345,  -369,
     -392,  -415,  -438,  -460,  -483,  -505,  -526,  -548,  -569,  -590,  -610,  -630,  -650,  -669,  -688,  -706,
     -724,  -742,  -759,  -775,  -792,  -807,  -822,  -837,  -851,  -865,  -878,  -891,  -903,  -915,  -926,  -936,
     -946,  -955,  -964,  -972,  -980,  -987,  -993,  -999, -1004, -1009, -1013, -1016, -1019, -1021, -1023, -1024,
    -1024, -1024, -1023, -1021, -1019, -1016, -1013, -1009, -1004,  -999,  -993,  -987,  -980,  -972,  -964,  -955,
     -946,  -936,  -926,  -915,  -903,  -891,  -878,  -865,  -851,  -837,  -822,  -807,  -792,  -775,  -759,  -742,
     -724,  -706,  -688,  -669,  -650,  -630,  -610,  -590,  -569,  -548,  -526,  -505,  -483,  -460,  -438,  -415,
     -392,  -369,  -345,  -321,  -297,  -273,  -249,  -224,  -200,  -175,  -150,  -125,  -100,   -75,   -50,   -25,
};

static inline int isin(int a) { return SIN256[a & 255]; }
static inline int icos(int a) { return SIN256[(a + 64) & 255]; }

static uint32_t shade_color(uint32_t c, int pct) {
    int r = (((int)((c >> 16) & 0xFF)) * pct) / 100;
    int g = (((int)((c >> 8) & 0xFF)) * pct) / 100;
    int b = (((int)(c & 0xFF)) * pct) / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void gfx_grid_to_iso(int gx16, int gy16, int *wx, int *wy) {
    *wx = ((gx16 - gy16) * ISO_HALF_W) / 16;
    *wy = ((gx16 + gy16) * ISO_HALF_H) / 16;
}

/* ============================================================
 * 2. 零除法 16.16 定點數 DDA 三角形高速光柵化 (專為 ARM9 優化)
 * ============================================================ */
void gfx_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t col) {
    int t;
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

    int total_h = y2 - y0;
    if (total_h <= 0) return;

    int sh = hal_get_height();
    if (y2 < 0 || y0 >= sh) return;

    int32_t d_long = ((int32_t)(x2 - x0) << 16) / total_h;

    /* 上半部 (y0 .. y1) */
    int h1 = y1 - y0;
    if (h1 > 0 && y1 >= 0 && y0 < sh) {
        int32_t d_short = ((int32_t)(x1 - x0) << 16) / h1;
        int ys = y0 < 0 ? 0 : y0;
        int ye = y1 >= sh ? (sh - 1) : y1;
        int skip = ys - y0;
        int32_t xa = ((int32_t)x0 << 16) + d_long * skip;
        int32_t xb = ((int32_t)x0 << 16) + d_short * skip;

        for (int y = ys; y <= ye; y++) {
            int ix0 = (int)(xa >> 16);
            int ix1 = (int)(xb >> 16);
            if (ix0 > ix1) { t = ix0; ix0 = ix1; ix1 = t; }
            hal_fill_rect(ix0, y, ix1 - ix0 + 1, 1, col);
            xa += d_long;
            xb += d_short;
        }
    }

    /* 下半部 (y1 .. y2) */
    int h2 = y2 - y1;
    if (h2 > 0 && y2 >= 0 && y1 < sh) {
        int32_t d_short = ((int32_t)(x2 - x1) << 16) / h2;
        int ys = y1 < 0 ? 0 : y1;
        int ye = y2 >= sh ? (sh - 1) : y2;
        int skip_a = ys - y0;
        int skip_b = ys - y1;
        int32_t xa = ((int32_t)x0 << 16) + d_long * skip_a;
        int32_t xb = ((int32_t)x1 << 16) + d_short * skip_b;

        for (int y = ys; y <= ye; y++) {
            int ix0 = (int)(xa >> 16);
            int ix1 = (int)(xb >> 16);
            if (ix0 > ix1) { t = ix0; ix0 = ix1; ix1 = t; }
            hal_fill_rect(ix0, y, ix1 - ix0 + 1, 1, col);
            xa += d_long;
            xb += d_short;
        }
    }
}

void gfx_fill_quad(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t col) {
    gfx_fill_tri(x0, y0, x1, y1, x2, y2, col);
    gfx_fill_tri(x0, y0, x2, y2, x3, y3, col);
}

void gfx_draw_iso_box(int cx, int cy, int hw, int hh, int h_up,
                      uint32_t col_top, uint32_t col_left, uint32_t col_right, uint32_t col_edge) {
    int ty = cy - h_up;
    gfx_fill_quad(cx, ty - hh, cx + hw, ty, cx, ty + hh, cx - hw, ty, col_top);
    gfx_fill_quad(cx - hw, ty, cx, ty + hh, cx, cy + hh, cx - hw, cy, col_left);
    gfx_fill_quad(cx, ty + hh, cx + hw, ty, cx + hw, cy, cx, cy + hh, col_right);

    if (col_edge != 0xFFFFFFFFu) {
        hal_draw_line(cx, ty - hh, cx + hw, ty, col_edge);
        hal_draw_line(cx + hw, ty, cx, ty + hh, col_edge);
        hal_draw_line(cx, ty + hh, cx - hw, ty, col_edge);
        hal_draw_line(cx - hw, ty, cx, ty - hh, col_edge);
        hal_draw_line(cx - hw, ty, cx - hw, cy, col_edge);
        hal_draw_line(cx, ty + hh, cx, cy + hh, col_edge);
        hal_draw_line(cx + hw, ty, cx + hw, cy, col_edge);
        hal_draw_line(cx - hw, cy, cx, cy + hh, col_edge);
        hal_draw_line(cx, cy + hh, cx + hw, cy, col_edge);
    }
}

void gfx_draw_iso_pyramid(int cx, int cy, int hw, int hh, int h_up,
                          uint32_t col_left, uint32_t col_right, uint32_t col_edge) {
    int apex_y = cy - h_up;
    gfx_fill_tri(cx, apex_y, cx - hw, cy, cx, cy + hh, col_left);
    gfx_fill_tri(cx, apex_y, cx, cy + hh, cx + hw, cy, col_right);
    if (col_edge != 0xFFFFFFFFu) {
        hal_draw_line(cx, apex_y, cx - hw, cy, col_edge);
        hal_draw_line(cx, apex_y, cx, cy + hh, col_edge);
        hal_draw_line(cx, apex_y, cx + hw, cy, col_edge);
        hal_draw_line(cx - hw, cy, cx, cy + hh, col_edge);
        hal_draw_line(cx, cy + hh, cx + hw, cy, col_edge);
    }
}

/* ============================================================
 * 3. 內建 5x7 無閃爍像素點陣字型
 * ============================================================ */
static const uint8_t FONT5X7[][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x08,0x08,0x3E,0x08,0x08},
    {0x08,0x08,0x08,0x08,0x08}, {0x23,0x13,0x08,0x64,0x62},
    {0x00,0x36,0x36,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
    {0x00,0x60,0x60,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00},
    {0x02,0x01,0x51,0x09,0x06}, {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}
};

static int font_index(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
    if (ch >= 'a' && ch <= 'z') return 10 + (ch - 'a');
    switch (ch) {
        case '$': return 36; case '+': return 37; case '-': return 38; case '%': return 39;
        case ':': return 40; case '/': return 41; case '.': return 42; case '!': return 43;
        case '?': return 44; case '(': return 45; case ')': return 46; case '*': return 47;
        default: return -1;
    }
}

void gfx_draw_ascii(int x, int y, const char *str, int scale, uint32_t color, int shadow) {
    if (!str || scale < 1) return;
    int cx = x;
    for (int i = 0; str[i]; i++) {
        int idx = font_index(str[i]);
        if (idx >= 0) {
            for (int col = 0; col < 5; col++) {
                uint8_t bits = FONT5X7[idx][col];
                for (int row = 0; row < 7; row++) {
                    if (bits & (1u << row)) {
                        if (shadow) hal_fill_rect(cx + col * scale + 1, y + row * scale + 1, scale, scale, 0x0C0C0C);
                        hal_fill_rect(cx + col * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
        }
        cx += 6 * scale;
    }
}

/* ============================================================
 * 4. 輕量粒子、飛彈、衝擊波與飄浮數字 VFX (無任何畫面震動!)
 * ============================================================ */
#define MAX_PARTICLES 40
#define MAX_POPUPS     8

typedef struct {
    int active;
    int x, y;
    int vx, vy;
    int life, max_life;
    int size, gravity;
    uint32_t color;
} Particle;

typedef struct {
    int active;
    int wx, wy, life;
    char text[24];
    uint32_t color;
} PopupText;

typedef struct {
    int active;
    int x0, y0, x1, y1;
    int elapsed, duration;
} MissileFX;

typedef struct {
    int active;
    int wx, wy;
    int life, max_life;
    uint32_t color;
} RingFX;

static Particle  g_parts[MAX_PARTICLES];
static PopupText g_pops[MAX_POPUPS];
static MissileFX g_missile;
static RingFX    g_ring;
static uint32_t  g_vfx_rng = 0x91827364u;

static int vfx_rand(int mod) {
    g_vfx_rng = g_vfx_rng * 1664525u + 1013904223u;
    return (int)((g_vfx_rng >> 16) % (uint32_t)mod);
}

void gfx_vfx_reset(void) {
    memset(g_parts, 0, sizeof(g_parts));
    memset(g_pops, 0, sizeof(g_pops));
    memset(&g_missile, 0, sizeof(g_missile));
    memset(&g_ring, 0, sizeof(g_ring));
}

static void add_particle(int wx, int wy, int vx, int vy, int life, int size, int grav, uint32_t color) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!g_parts[i].active) {
            g_parts[i].active = 1;
            g_parts[i].x = wx * 16;
            g_parts[i].y = wy * 16;
            g_parts[i].vx = vx;
            g_parts[i].vy = vy;
            g_parts[i].life = life;
            g_parts[i].max_life = life;
            g_parts[i].size = size;
            g_parts[i].gravity = grav;
            g_parts[i].color = color;
            return;
        }
    }
}

void gfx_spawn_dust(int wx, int wy) {
    for (int i = 0; i < 3; i++) {
        add_particle(wx + vfx_rand(10) - 5, wy, vfx_rand(28) - 14, -(vfx_rand(14) + 4), 240, 3, 1, 0xEFEBE9);
    }
}

void gfx_spawn_sparkles(int wx, int wy, uint32_t color, int count) {
    if (count > 14) count = 14;
    const uint32_t cols[4] = { color, 0xFFF176, 0xFFFFFF, 0xFFB300 };
    for (int i = 0; i < count; i++) {
        add_particle(wx, wy, vfx_rand(88) - 44, -(vfx_rand(68) + 20), 420 + vfx_rand(200), 3 + vfx_rand(2), 4, cols[i & 3]);
    }
}

void gfx_spawn_explosion(int wx, int wy) {
    const uint32_t cols[4] = { 0xFF3D00, 0xFF9100, 0xFFEA00, 0xFFFFFF };
    for (int i = 0; i < 20; i++) {
        add_particle(wx, wy, vfx_rand(130) - 65, vfx_rand(120) - 80, 440 + vfx_rand(240), 4 + vfx_rand(4), 3, cols[i & 3]);
    }
    /* 用炫酷擴散衝擊波光環取代傷眼的螢幕震動！ */
    g_ring.active = 1;
    g_ring.wx = wx;
    g_ring.wy = wy;
    g_ring.life = 420;
    g_ring.max_life = 420;
    g_ring.color = 0xFF9100;
}

void gfx_spawn_ice(int wx, int wy) {
    const uint32_t cols[3] = { 0x80D8FF, 0xE1F5FE, 0x40C4FF };
    for (int i = 0; i < 14; i++) {
        add_particle(wx, wy, vfx_rand(64) - 32, vfx_rand(48) - 42, 420 + vfx_rand(180), 3 + vfx_rand(2), 1, cols[i % 3]);
    }
}

void gfx_spawn_popup(int wx, int wy, const char *ascii_text, uint32_t color) {
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!g_pops[i].active) {
            g_pops[i].active = 1;
            g_pops[i].wx = wx;
            g_pops[i].wy = wy;
            g_pops[i].life = 900;
            snprintf(g_pops[i].text, sizeof(g_pops[i].text), "%s", ascii_text);
            g_pops[i].color = color;
            return;
        }
    }
}

void gfx_launch_missile(int x0, int y0, int x1, int y1) {
    g_missile.active = 1;
    g_missile.x0 = x0; g_missile.y0 = y0;
    g_missile.x1 = x1; g_missile.y1 = y1;
    g_missile.elapsed = 0;
    g_missile.duration = 660;
}

int gfx_missile_active(void) { return g_missile.active; }

void gfx_vfx_tick(int dt_ms) {
    if (g_ring.active) {
        g_ring.life -= dt_ms;
        if (g_ring.life <= 0) g_ring.active = 0;
    }
    if (g_missile.active) {
        g_missile.elapsed += dt_ms;
        int t = g_missile.elapsed, dur = g_missile.duration;
        if (t >= dur) {
            g_missile.active = 0;
            gfx_spawn_explosion(g_missile.x1, g_missile.y1);
            gfx_spawn_popup(g_missile.x1 - 28, g_missile.y1 - 36, "BOOM!", 0xFF5252);
        } else {
            int mx = g_missile.x0 + ((g_missile.x1 - g_missile.x0) * t) / dur;
            int arc = (t * (dur - t)) / (dur * dur / 120 + 1);
            int my = g_missile.y0 + ((g_missile.y1 - g_missile.y0) * t) / dur - arc;
            add_particle(mx, my, vfx_rand(12) - 6, vfx_rand(12) - 6, 180, 4, 0, 0xFFB74D);
        }
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!g_parts[i].active) continue;
        g_parts[i].life -= dt_ms;
        if (g_parts[i].life <= 0) { g_parts[i].active = 0; continue; }
        g_parts[i].x += g_parts[i].vx;
        g_parts[i].y += g_parts[i].vy;
        g_parts[i].vy += g_parts[i].gravity;
    }
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!g_pops[i].active) continue;
        g_pops[i].life -= dt_ms;
        g_pops[i].wy -= 1;
        if (g_pops[i].life <= 0) g_pops[i].active = 0;
    }
}

int gfx_vfx_is_active(void) {
    if (g_missile.active || g_ring.active) return 1;
    for (int i = 0; i < MAX_PARTICLES; i++) if (g_parts[i].active) return 1;
    for (int i = 0; i < MAX_POPUPS; i++) if (g_pops[i].active) return 1;
    return 0;
}

int gfx_vfx_get_bounds(int cam_x, int cam_y, int *bx, int *by, int *bw, int *bh) {
    int x0 = 99999, y0 = 99999, x1 = -99999, y1 = -99999;
    int any = 0;

    if (g_ring.active) {
        int sx = g_ring.wx - cam_x, sy = g_ring.wy - cam_y;
        int r = 12 + ((g_ring.max_life - g_ring.life) * 44) / g_ring.max_life;
        if (sx - r < x0) x0 = sx - r;
        if (sy - r < y0) y0 = sy - r;
        if (sx + r > x1) x1 = sx + r;
        if (sy + r > y1) y1 = sy + r;
        any = 1;
    }
    if (g_missile.active) {
        int t = g_missile.elapsed, dur = g_missile.duration;
        int mx = g_missile.x0 + ((g_missile.x1 - g_missile.x0) * t) / dur - cam_x;
        int arc = (t * (dur - t)) / (dur * dur / 120 + 1);
        int my = g_missile.y0 + ((g_missile.y1 - g_missile.y0) * t) / dur - arc - cam_y;
        int tx = g_missile.x1 - cam_x, ty = g_missile.y1 - cam_y;
        int lx0 = (mx < tx ? mx : tx) - 28, ly0 = (my < ty ? my : ty) - 28;
        int lx1 = (mx > tx ? mx : tx) + 28, ly1 = (my > ty ? my : ty) + 28;
        if (lx0 < x0) x0 = lx0;
        if (ly0 < y0) y0 = ly0;
        if (lx1 > x1) x1 = lx1;
        if (ly1 > y1) y1 = ly1;
        any = 1;
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!g_parts[i].active) continue;
        int sx = (g_parts[i].x / 16) - cam_x;
        int sy = (g_parts[i].y / 16) - cam_y;
        if (sx - 8 < x0) x0 = sx - 8;
        if (sy - 8 < y0) y0 = sy - 8;
        if (sx + 8 > x1) x1 = sx + 8;
        if (sy + 8 > y1) y1 = sy + 8;
        any = 1;
    }
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!g_pops[i].active) continue;
        int sx = g_pops[i].wx - cam_x;
        int sy = g_pops[i].wy - cam_y;
        if (sx - 4 < x0) x0 = sx - 4;
        if (sy - 4 < y0) y0 = sy - 4;
        if (sx + 100 > x1) x1 = sx + 100;
        if (sy + 22 > y1) y1 = sy + 22;
        any = 1;
    }
    if (!any) return 0;
    *bx = x0 - 8; *by = y0 - 8;
    *bw = (x1 - x0) + 16; *bh = (y1 - y0) + 16;
    return 1;
}

void gfx_vfx_render(int cam_x, int cam_y) {
    if (g_ring.active) {
        int sx = g_ring.wx - cam_x, sy = g_ring.wy - cam_y;
        int r = 10 + ((g_ring.max_life - g_ring.life) * 40) / g_ring.max_life;
        hal_draw_circle(sx, sy, r, g_ring.color);
        if (r > 4) hal_draw_circle(sx, sy, r - 3, 0xFFEA00);
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!g_parts[i].active) continue;
        int sx = (g_parts[i].x / 16) - cam_x;
        int sy = (g_parts[i].y / 16) - cam_y;
        int sz = g_parts[i].size;
        hal_fill_rect(sx - sz / 2, sy - sz / 2, sz, sz, g_parts[i].color);
    }
    if (g_missile.active) {
        int t = g_missile.elapsed, dur = g_missile.duration;
        int mx = g_missile.x0 + ((g_missile.x1 - g_missile.x0) * t) / dur - cam_x;
        int arc = (t * (dur - t)) / (dur * dur / 120 + 1);
        int my = g_missile.y0 + ((g_missile.y1 - g_missile.y0) * t) / dur - arc - cam_y;
        int tx = g_missile.x1 - cam_x, ty = g_missile.y1 - cam_y;
        hal_draw_circle(tx, ty, 18, 0xFF1744);
        hal_draw_line(tx - 22, ty, tx + 22, ty, 0xFF1744);
        hal_draw_line(tx, ty - 22, tx, ty + 22, 0xFF1744);
        hal_fill_circle(mx, my, 7, 0xECEFF1);
        hal_fill_circle(mx, my, 4, 0xD50000);
    }
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!g_pops[i].active) continue;
        gfx_draw_ascii(g_pops[i].wx - cam_x, g_pops[i].wy - cam_y, g_pops[i].text, 2, g_pops[i].color, 1);
    }
}

/* ============================================================
 * 5. 精緻 UI 面板與高效 3D 海島等角背景
 * ============================================================ */
void gfx_panel(int x, int y, int w, int h, uint32_t bg, uint32_t border, uint32_t header_bg, int header_h) {
    hal_fill_rect(x + 4, y + 4, w, h, 0x070D12);
    hal_fill_rect(x, y, w, h, bg);
    if (header_h > 0) {
        hal_fill_rect(x + 2, y + 2, w - 4, header_h, header_bg);
        hal_fill_rect(x + 2, y + 2 + header_h, w - 4, 2, border);
    }
    hal_draw_rect(x, y, w, h, border);
    hal_draw_rect(x + 1, y + 1, w - 2, h - 2, border);
}

static void draw_iso_tree(int sx, int sy) {
    hal_fill_rect(sx - 2, sy - 6, 4, 9, 0x6D4C41);
    hal_fill_circle(sx, sy - 11, 8, 0x2E7D32);
    hal_fill_circle(sx - 2, sy - 13, 4, 0x4CAF50);
}

void gfx_draw_iso_world_bg(const MapDef *m, int cam_x, int cam_y) {
    int sw = hal_get_width();
    int sh = hal_get_height();

    /* 1. 單次清屏為海洋藍 */
    hal_clear(m->bg_water);

    /* 2. 靜態清爽海浪線 (不隨時間亂跳) */
    uint32_t wcol = shade_color(m->bg_water, 128);
    int wy0 = -(cam_y % 56);
    int wx0 = -(cam_x % 88);
    for (int wy = wy0; wy < sh; wy += 56) {
        for (int wx = wx0; wx < sw; wx += 88) {
            hal_fill_rect(wx, wy, 18, 2, wcol);
        }
    }

    /* 3. 3D 浮島底座 (簡化重疊，單次綠島 + 兩側岩壁，大幅減少像素填充量) */
    int top_x, top_y, right_x, right_y, bot_x, bot_y, left_x, left_y;
    int gw16 = (m->grid_w - 1) * 16;
    int gh16 = (m->grid_h - 1) * 16;
    gfx_grid_to_iso(8, 8, &top_x, &top_y);
    gfx_grid_to_iso(gw16 - 8, 8, &right_x, &right_y);
    gfx_grid_to_iso(gw16 - 8, gh16 - 8, &bot_x, &bot_y);
    gfx_grid_to_iso(8, gh16 - 8, &left_x, &left_y);

    top_x -= cam_x;   top_y -= cam_y;
    right_x -= cam_x; right_y -= cam_y;
    bot_x -= cam_x;   bot_y -= cam_y;
    left_x -= cam_x;  left_y -= cam_y;

    int cliff_h = 28;
    gfx_fill_quad(left_x, left_y, bot_x, bot_y, bot_x, bot_y + cliff_h, left_x, left_y + cliff_h, 0x8D6E63);
    gfx_fill_quad(bot_x, bot_y, right_x, right_y, right_x, right_y + cliff_h, bot_x, bot_y + cliff_h, 0x5D4037);
    gfx_fill_quad(top_x, top_y, right_x, right_y, bot_x, bot_y, left_x, left_y, m->bg_land);

    /* 沙灘海岸高光外框線 */
    hal_draw_line(top_x, top_y, right_x, right_y, 0xE6D5A7);
    hal_draw_line(right_x, right_y, bot_x, bot_y, 0xE6D5A7);
    hal_draw_line(bot_x, bot_y, left_x, left_y, 0xE6D5A7);
    hal_draw_line(left_x, left_y, top_x, top_y, 0xE6D5A7);

    /* 4. 島內風景湖與點綴松樹 */
    int lx, ly;
    gfx_grid_to_iso((m->grid_w * 16) / 2, (m->grid_h * 16) / 2, &lx, &ly);
    lx -= cam_x; ly -= cam_y;
    if (lx > -120 && lx < sw + 120 && ly > -80 && ly < sh + 80) {
        gfx_fill_quad(lx, ly - 38, lx + 76, ly, lx, ly + 38, lx - 76, ly, 0x29B6F6);
        hal_draw_line(lx - 20, ly - 4, lx + 12, ly - 4, 0xE1F5FE);
    }

    for (int gy = 3; gy < m->grid_h - 3; gy += 2) {
        for (int gx = 3; gx < m->grid_w - 3; gx += 2) {
            if (gx >= 6 && gx <= 10 && gy >= 4 && gy <= 7) continue;
            if (((gx * 5 + gy * 7) % 3) != 0) continue;
            int tx, ty;
            gfx_grid_to_iso(gx * 16, gy * 16, &tx, &ty);
            tx -= cam_x; ty -= cam_y;
            if (tx > -20 && tx < sw + 20 && ty > -20 && ty < sh + 20) {
                draw_iso_tree(tx, ty);
            }
        }
    }
}

/* ============================================================
 * 6. 3D 擠出立體建築、地標與等角地塊
 * ============================================================ */
static void draw_3d_house(int sx, int sy, int level, uint32_t col) {
    uint32_t c_top = shade_color(col, 118);
    uint32_t c_l   = shade_color(col, 92);
    uint32_t c_r   = shade_color(col, 68);

    if (level == 0) {
        hal_fill_rect(sx - 2, sy - 24, 2, 22, 0xECEFF1);
        gfx_fill_tri(sx, sy - 24, sx + 14, sy - 19, sx, sy - 14, col);
        return;
    }
    if (level == 1) {
        gfx_draw_iso_box(sx, sy, 17, 9, 14, 0xFFF8E1, 0xF5E6C4, 0xD7C4A3, 0x5D4037);
        gfx_draw_iso_pyramid(sx, sy - 14, 20, 10, 12, c_l, c_r, 0x3E2723);
        hal_fill_rect(sx - 9, sy - 8, 5, 5, 0x4FC3F7);
        hal_fill_rect(sx + 5, sy - 8, 5, 7, 0x6D4C41);
        return;
    }
    if (level == 2) {
        gfx_draw_iso_box(sx, sy, 19, 10, 23, 0xFFE0B2, 0xFFCC80, 0xE6A95C, 0x4E342E);
        gfx_draw_iso_pyramid(sx, sy - 23, 22, 11, 13, c_top, c_r, 0x3E2723);
        for (int r = 0; r < 2; r++) {
            hal_fill_rect(sx - 12, sy - 17 + r * 9, 5, 4, 0x29B6F6);
            hal_fill_rect(sx + 6,  sy - 17 + r * 9, 5, 4, 0xFFF59D);
        }
        return;
    }
    if (level == 3) {
        gfx_draw_iso_box(sx, sy, 20, 10, 36, c_top, 0xCFD8DC, 0x90A4AE, 0x37474F);
        for (int r = 0; r < 3; r++) {
            hal_fill_rect(sx - 13, sy - 28 + r * 10, 5, 5, 0xFFD54F);
            hal_fill_rect(sx + 6,  sy - 28 + r * 10, 5, 5, 0x80DEEA);
        }
        return;
    }
    if (level == 4) {
        gfx_draw_iso_box(sx, sy, 21, 11, 50, 0xE1F5FE, 0x4FC3F7, 0x0288D1, 0x01579B);
        gfx_draw_iso_box(sx, sy - 50, 13, 6, 7, c_top, c_l, c_r, 0x01579B);
        for (int r = 0; r < 4; r++) {
            hal_fill_rect(sx - 14, sy - 42 + r * 11, 10, 4, 0xE1F5FE);
            hal_fill_rect(sx + 4,  sy - 42 + r * 11, 10, 4, 0xB3E5FC);
        }
        return;
    }
    /* Lv.5 3D 黃金摩天塔 */
    gfx_draw_iso_box(sx, sy,      22, 11, 25, 0xFFE082, 0xFFB300, 0xE65100, 0x5D4037);
    gfx_draw_iso_box(sx, sy - 25, 16, 8,  21, 0xFFF176, 0xFFC107, 0xF57C00, 0x5D4037);
    gfx_draw_iso_box(sx, sy - 46, 10, 5,  15, 0xFFFFFF, 0xFFD54F, 0xFFA000, 0x5D4037);
    hal_fill_rect(sx - 1, sy - 74, 3, 14, 0xFFF59D);
}

static void draw_3d_comm(int sx, int sy, CommType comm, uint32_t col) {
    switch (comm) {
        case COMM_MALL:
            gfx_draw_iso_box(sx, sy, 24, 12, 23, 0xFFE0B2, 0xFF9800, 0xE65100, 0x4E342E);
            gfx_draw_iso_box(sx, sy - 23, 13, 6, 9, shade_color(col, 120), col, shade_color(col, 70), 0xFFFFFF);
            gfx_draw_ascii(sx - 3, sy - 13, "$", 1, 0xFFFFFF, 1);
            break;
        case COMM_HOTEL:
            gfx_draw_iso_box(sx, sy, 21, 11, 42, 0xF8BBD0, 0xEC407A, 0xAD1457, 0x880E4F);
            gfx_draw_ascii(sx - 4, sy - 36, "H", 2, 0xFFFFFF, 1);
            break;
        case COMM_LAB:
            gfx_draw_iso_box(sx, sy, 21, 11, 19, 0xCFD8DC, 0x90A4AE, 0x607D8B, 0x263238);
            hal_fill_circle(sx, sy - 23, 11, 0x80DEEA);
            hal_fill_rect(sx - 1, sy - 42, 2, 9, 0x00E5FF);
            break;
        case COMM_PARK:
            draw_iso_tree(sx - 11, sy - 2);
            draw_iso_tree(sx + 11, sy - 4);
            hal_fill_circle(sx, sy + 2, 7, 0x29B6F6);
            break;
        case COMM_GAS:
            gfx_draw_iso_box(sx, sy - 14, 22, 11, 6, 0xFF5252, 0xD32F2F, 0xB71C1C, 0xFFFFFF);
            hal_fill_rect(sx - 12, sy - 12, 4, 16, 0xECEFF1);
            hal_fill_rect(sx + 8,  sy - 12, 4, 16, 0xECEFF1);
            break;
        default:
            draw_3d_house(sx, sy, 0, col);
            break;
    }
}

static void draw_3d_landmark(int sx, int sy, TileType type) {
    switch (type) {
        case TILE_START:
        case TILE_BANK:
            gfx_draw_iso_box(sx, sy, 21, 11, 17, 0xFFF8E1, 0xFFE082, 0xFFB300, 0x6D4C41);
            gfx_draw_iso_pyramid(sx, sy - 17, 23, 11, 11, 0xFFD54F, 0xF57F17, 0x5D4037);
            gfx_draw_ascii(sx - 3, sy - 10, "$", 2, 0xB71C1C, 0);
            break;
        case TILE_HOSPITAL:
            gfx_draw_iso_box(sx, sy, 21, 11, 22, 0xFFFFFF, 0xECEFF1, 0xCFD8DC, 0x607D8B);
            hal_fill_rect(sx - 3, sy - 28, 6, 14, 0xE53935);
            hal_fill_rect(sx - 8, sy - 24, 16, 6, 0xE53935);
            break;
        case TILE_PRISON:
            gfx_draw_iso_box(sx, sy, 20, 10, 22, 0x78909C, 0x546E7A, 0x37474F, 0x263238);
            for (int b = -10; b <= -2; b += 4) hal_fill_rect(sx + b, sy - 16, 2, 12, 0xECEFF1);
            break;
        case TILE_MAGIC:
            gfx_draw_iso_box(sx, sy, 14, 7, 20, 0xCE93D8, 0xAB47BC, 0x6A1B9A, 0x311B92);
            gfx_draw_iso_pyramid(sx, sy - 20, 18, 9, 20, 0x7E57C2, 0x4527A0, 0xFFD54F);
            break;
        case TILE_SHOP:
            gfx_draw_iso_box(sx, sy, 19, 10, 17, 0xA5D6A7, 0x66BB6A, 0x2E7D32, 0x1B5E20);
            gfx_draw_iso_pyramid(sx, sy - 17, 21, 10, 9, 0x43A047, 0x1B5E20, 0xFFFFFF);
            break;
        case TILE_MINIGAME:
        case TILE_LOTTERY:
            gfx_draw_iso_box(sx, sy, 19, 10, 17, 0xFFE082, 0xFF7043, 0xD84315, 0xFFD54F);
            gfx_draw_ascii(sx - 4, sy - 22, "*", 2, 0xFFEA00, 1);
            break;
        case TILE_NEWS:
            gfx_draw_iso_box(sx, sy, 15, 8, 11, 0xFFFFFF, 0xBBDEFB, 0x64B5F6, 0x1565C0);
            gfx_draw_ascii(sx - 3, sy - 13, "?", 2, 0x1565C0, 0);
            break;
        case TILE_COUPON:
            gfx_draw_iso_box(sx, sy, 14, 7, 9, 0xFFF176, 0xFFD54F, 0xFFA000, 0xE65100);
            break;
        default: break;
    }
}

void gfx_draw_god_sprite(int sx, int sy, GodType god) {
    if (god == GOD_NONE) return;
    uint32_t aura = (god <= GOD_LAND) ? 0xFFF176 : 0x9575CD;
    uint32_t robe = 0xFFD54F;
    if (god == GOD_WEALTH) robe = 0xE53935;
    else if (god == GOD_FORTUNE) robe = 0xFF7043;
    else if (god == GOD_LAND) robe = 0x43A047;
    else if (god == GOD_POOR) robe = 0x546E7A;
    else if (god == GOD_BADLUCK) robe = 0x311B92;

    hal_draw_circle(sx, sy, 10, aura);
    hal_fill_circle(sx, sy - 4, 5, 0xFFCCBC);
    hal_fill_rect(sx - 5, sy + 1, 10, 8, robe);
    if (god == GOD_WEALTH) hal_fill_rect(sx - 7, sy - 10, 14, 3, 0xFFD54F);
    else if (god == GOD_BADLUCK) hal_fill_circle(sx, sy - 10, 4, 0x424242);
}

static const uint32_t GRP_COLS[7] = {
    0x607D8B, 0xE53935, 0x1E88E5, 0x43A047, 0xFB8C00, 0x8E24AA, 0x00897B
};

void gfx_draw_iso_tile(int sx, int sy, const TileDef *td,
                       int owner, uint32_t owner_color, int level, CommType comm,
                       int has_roadblock, int has_mine, GodType map_god,
                       int toll, int is_cursor, int is_dest, int draw_label) {
    int sw = hal_get_width();
    int sh = hal_get_height();
    if (sx < -68 || sx > sw + 68 || sy < -78 || sy > sh + 48) return;

    uint32_t top_c = 0xF5EFE0;
    if (td->type == TILE_START || td->type == TILE_BANK) top_c = 0xFFF3CD;
    else if (td->type == TILE_COMMERCIAL) top_c = 0xFCE4EC;
    else if (td->type == TILE_NEWS) top_c = 0xE3F2FD;
    else if (td->type == TILE_SHOP) top_c = 0xE8F5E9;
    else if (td->type == TILE_MAGIC) top_c = 0xF3E5F5;
    else if (td->type == TILE_MINIGAME || td->type == TILE_LOTTERY) top_c = 0xFFF8E1;
    else if (td->type == TILE_HOSPITAL || td->type == TILE_PRISON) top_c = 0xECEFF1;

    if (is_dest) top_c = 0xFFF176;

    uint32_t side_base = (owner >= 0) ? owner_color : GRP_COLS[td->group % 7];
    uint32_t left_c  = shade_color(side_base, 95);
    uint32_t right_c = shade_color(side_base, 65);
    uint32_t edge_c  = is_cursor ? 0xFFEA00 : (is_dest ? 0xFF6D00 : 0x37474F);

    gfx_draw_iso_box(sx, sy + ISO_SLAB_H, ISO_HALF_W - 2, ISO_HALF_H - 1, ISO_SLAB_H,
                     top_c, left_c, right_c, edge_c);

    if (td->type == TILE_LAND || td->type == TILE_COMMERCIAL) {
        if (owner >= 0) {
            if (td->type == TILE_COMMERCIAL) draw_3d_comm(sx, sy - 6, comm, owner_color);
            else draw_3d_house(sx, sy - 6, level, owner_color);
        }
    } else {
        draw_3d_landmark(sx, sy - 6, td->type);
    }

    if (has_roadblock) {
        gfx_draw_iso_box(sx - 12, sy + 6, 8, 4, 8, 0xFF6D00, 0xFFFFFF, 0xE65100, 0x212121);
    }
    if (has_mine) {
        hal_fill_circle(sx + 14, sy + 4, 5, 0x263238);
        hal_fill_circle(sx + 14, sy + 2, 2, 0xFF1744);
    }
    if (map_god != GOD_NONE) {
        gfx_draw_god_sprite(sx + 18, sy - 16, map_god);
    }

    if (draw_label && sx > 28 && sx < sw - 54 && sy > 44 && sy < sh - 66) {
        hal_set_text_bg(top_c);
        hal_draw_text(sx - 26, sy + 2, td->name, 0x1A1A1A);
        if (td->type == TILE_LAND || td->type == TILE_COMMERCIAL) {
            char pbuf[16];
            int val = (owner < 0) ? td->base_price : toll;
            snprintf(pbuf, sizeof(pbuf), "$%d", val);
            gfx_draw_ascii(sx - 18, sy + ISO_HALF_H - 3, pbuf, 1,
                           (owner < 0) ? 0xFFFFFF : 0xFFEA00, 1);
        }
    }
}

/* ============================================================
 * 7. 3D 立體動畫人物 + 分離陰影 + 頭頂名牌與剩餘步數氣泡
 * ============================================================ */
void gfx_draw_character_3d(int sx, int ground_sy, int jump_h, int char_id, int player_idx,
                           uint32_t color, int walk_phase, int vehicle_type, GodType god,
                           int facing_left, int is_active, int steps_left) {
    int sh_w = 13 - (jump_h / 5);
    if (sh_w < 6) sh_w = 6;
    hal_fill_rect(sx - sh_w, ground_sy + 4, sh_w * 2, 5, 0x152028);

    if (is_active) {
        hal_draw_circle(sx, ground_sy + 4, 15, 0xFFEA00);
    }

    int sy = ground_sy - jump_h;
    int body_y = sy;
    if (vehicle_type == 1) {
        gfx_draw_iso_box(sx, sy + 4, 15, 7, 7, 0xFF9100, 0xE65100, 0xBF360C, 0x212121);
        hal_fill_circle(sx - 9, sy + 7, 4, 0x212121);
        hal_fill_circle(sx + 9, sy + 7, 4, 0x212121);
        body_y -= 6;
    } else if (vehicle_type == 2) {
        gfx_draw_iso_box(sx, sy + 4, 19, 9, 8, 0xFF1744, 0xD50000, 0x8E0000, 0xFFFFFF);
        gfx_draw_iso_box(sx, sy - 4, 9, 5, 5, 0x80D8FF, 0x4FC3F7, 0x0288D1, 0x212121);
        hal_fill_circle(sx - 11, sy + 8, 4, 0x212121);
        hal_fill_circle(sx + 11, sy + 8, 4, 0x212121);
        body_y -= 7;
    }

    if (vehicle_type == 0) {
        int l_up = (walk_phase & 1) ? 3 : 0;
        int r_up = (walk_phase & 1) ? 0 : 3;
        hal_fill_rect(sx - 6, body_y - 2 - l_up, 4, 8, 0x263238);
        hal_fill_rect(sx + 2, body_y - 2 - r_up, 4, 8, 0x37474F);
    }

    gfx_draw_iso_box(sx, body_y - 2, 9, 5, 11,
                     shade_color(color, 120), color, shade_color(color, 72), 0xFFFFFF);

    int hy = body_y - 19;
    hal_fill_circle(sx, hy, 8, 0xFFCCBC);
    hal_fill_circle(sx - 2, hy - 2, 3, 0xFFE0D0);
    int edx = facing_left ? -2 : 2;
    hal_fill_rect(sx - 3 + edx, hy - 1, 2, 2, 0x1A1A1A);
    hal_fill_rect(sx + 2 + edx, hy - 1, 2, 2, 0x1A1A1A);

    switch (char_id % 6) {
        case 0:
            hal_fill_circle(sx - 8, hy - 4, 4, 0x3E2723);
            hal_fill_circle(sx + 8, hy - 4, 4, 0x3E2723);
            hal_fill_rect(sx - 6, hy - 8, 12, 3, 0xFF1744);
            break;
        case 1:
            gfx_draw_iso_pyramid(sx, hy - 6, 13, 6, 7, 0xFFD54F, 0xF57F17, 0x5D4037);
            hal_fill_rect(sx - 4, hy + 3, 8, 2, 0x5D4037);
            break;
        case 2:
            hal_fill_rect(sx - 11, hy - 7, 22, 3, 0x8E24AA);
            hal_fill_rect(sx - 5, hy - 11, 10, 4, 0xCE93D8);
            break;
        case 3:
            hal_fill_circle(sx, hy - 6, 6, 0xFFFFFF);
            hal_fill_rect(sx - 1, hy - 8, 3, 3, 0xE53935);
            break;
        case 4:
            hal_fill_rect(sx - 6, hy - 9, 12, 4, 0xF48FB1);
            hal_fill_circle(sx + edx, hy + 4, 2, 0xFFEB3B);
            break;
        default:
            hal_fill_rect(sx - 8, hy - 6, 16, 4, 0x263238);
            hal_fill_rect(sx - 8, hy - 5, 16, 2, 0x43A047);
            break;
    }

    if (god != GOD_NONE) {
        gfx_draw_god_sprite(sx + 18, hy - 8, god);
    }

    int tag_y = hy - 24;
    char ptag[16];
    snprintf(ptag, sizeof(ptag), "P%d", player_idx + 1);
    hal_fill_rect(sx - 11, tag_y, 22, 11, color);
    hal_draw_rect(sx - 11, tag_y, 22, 11, 0xFFFFFF);
    gfx_draw_ascii(sx - 6, tag_y + 2, ptag, 1, 0xFFFFFF, 0);

    if (is_active) {
        int ay = tag_y - 4;
        gfx_fill_tri(sx - 6, ay - 7, sx + 6, ay - 7, sx, ay, 0xFFEA00);
        if (steps_left > 0) {
            char sb[16];
            snprintf(sb, sizeof(sb), "%d", steps_left);
            hal_fill_circle(sx, ay - 18, 10, 0xD50000);
            hal_draw_circle(sx, ay - 18, 10, 0xFFEA00);
            gfx_draw_ascii(sx - 4, ay - 24, sb, 2, 0xFFEA00, 1);
        }
    }
}

/* ============================================================
 * 8. 真 3D 軟體三軸旋轉立體骰子
 * ============================================================ */
static const int8_t CUBE_V[8][3] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
};

static const uint8_t CUBE_F[6][4] = {
    {4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6},
    {0, 4, 7, 3}, {0, 1, 5, 4}, {7, 6, 2, 3}
};
static const uint8_t FACE_VAL[6] = { 1, 6, 2, 5, 3, 4 };

static const int16_t REST_ANGLES[7][3] = {
    { 18,  16, 0},
    { 18,  16, 0},
    { 18, -48, 0},
    {-46,  16, 0},
    { 82,  16, 0},
    { 18,  80, 0},
    { 18, 144, 0}
};

static void rotate_pt_3d(int x, int y, int z, int ax, int ay, int az, int *ox, int *oy, int *oz) {
    int sx = isin(ax), cx = icos(ax);
    int y1 = (y * cx - z * sx) >> 10;
    int z1 = (y * sx + z * cx) >> 10;
    int x1 = x;
    int sy = isin(ay), cy = icos(ay);
    int x2 = (x1 * cy + z1 * sy) >> 10;
    int z2 = (-x1 * sy + z1 * cy) >> 10;
    int y2 = y1;
    int sz = isin(az), cz = icos(az);
    *ox = (x2 * cz - y2 * sz) >> 10;
    *oy = (x2 * sz + y2 * cz) >> 10;
    *oz = z2;
}

static void draw_face_pips_3d(int cx, int cy, int fidx, int S, int ax, int ay, int az) {
    int val = FACE_VAL[fidx];
    static const int8_t F_BASIS[6][9] = {
        { 0, 0, 1,   1, 0, 0,   0, 1, 0 },
        { 0, 0,-1,  -1, 0, 0,   0, 1, 0 },
        { 1, 0, 0,   0, 0,-1,   0, 1, 0 },
        {-1, 0, 0,   0, 0, 1,   0, 1, 0 },
        { 0,-1, 0,   1, 0, 0,   0, 0, 1 },
        { 0, 1, 0,   1, 0, 0,   0, 0,-1 }
    };
    static const int8_t PIP_UV[7][6][2] = {
        {{0,0}},
        {{0,0}},
        {{-50,-50},{50,50}},
        {{-50,-50},{0,0},{50,50}},
        {{-50,-50},{50,-50},{-50,50},{50,50}},
        {{-50,-50},{50,-50},{0,0},{-50,50},{50,50}},
        {{-50,-52},{50,-52},{-50,0},{50,0},{-50,52},{50,52}}
    };

    const int8_t *b = F_BASIS[fidx];
    uint32_t pip_col = (val == 1 || val == 4) ? 0xD50000 : 0x1A237E;
    int pr = (val == 1) ? 6 : 4;

    for (int p = 0; p < val; p++) {
        int u = (PIP_UV[val][p][0] * S) / 100;
        int v = (PIP_UV[val][p][1] * S) / 100;
        int lx = b[0] * S + b[3] * u + b[6] * v;
        int ly = b[1] * S + b[4] * u + b[7] * v;
        int lz = b[2] * S + b[5] * u + b[8] * v;

        int rx, ry, rz;
        rotate_pt_3d(lx, ly, lz, ax, ay, az, &rx, &ry, &rz);
        int denom = 260 - rz;
        if (denom < 40) denom = 40;
        int px = cx + (rx * 260) / denom;
        int py = cy + (ry * 260) / denom;
        hal_fill_circle(px, py, pr, pip_col);
    }
}

void gfx_draw_3d_dice(int cx, int cy, int value, int rolling, int bounce_h, int tick_ms, int seed_off) {
    if (value < 1 || value > 6) value = 1;

    int sh_r = 24 - (bounce_h / 4);
    if (sh_r < 10) sh_r = 10;
    hal_fill_rect(cx - sh_r, cy + 34, sh_r * 2, 9, 0x0D161E);

    int draw_cy = cy - bounce_h;
    int ax, ay, az;
    if (rolling) {
        ax = ((tick_ms * 11) / 28 + seed_off * 37) & 255;
        ay = ((tick_ms * 15) / 28 + seed_off * 71) & 255;
        az = ((tick_ms * 7)  / 28 + seed_off * 19) & 255;
    } else {
        ax = REST_ANGLES[value][0] & 255;
        ay = REST_ANGLES[value][1] & 255;
        az = REST_ANGLES[value][2] & 255;
    }

    int S = 24;
    int px[8], py[8], pz[8];
    for (int i = 0; i < 8; i++) {
        int rx, ry, rz;
        rotate_pt_3d(CUBE_V[i][0] * S, CUBE_V[i][1] * S, CUBE_V[i][2] * S, ax, ay, az, &rx, &ry, &rz);
        int denom = 260 - rz;
        if (denom < 40) denom = 40;
        px[i] = cx + (rx * 260) / denom;
        py[i] = draw_cy + (ry * 260) / denom;
        pz[i] = rz;
    }

    for (int f = 0; f < 6; f++) {
        int i0 = CUBE_F[f][0], i1 = CUBE_F[f][1], i2 = CUBE_F[f][2], i3 = CUBE_F[f][3];
        int cross = (px[i1] - px[i0]) * (py[i2] - py[i0]) - (py[i1] - py[i0]) * (px[i2] - px[i0]);
        if (cross <= 0) continue;

        int avg_z = (pz[i0] + pz[i1] + pz[i2] + pz[i3]) / 4;
        int avg_y = (py[i0] + py[i1] + py[i2] + py[i3]) / 4 - draw_cy;
        int light = 225 + (avg_z * 2) - avg_y;
        if (light > 255) light = 255;
        if (light < 150) light = 150;
        uint32_t fcol = ((uint32_t)light << 16) | ((uint32_t)light << 8) | (uint32_t)(light - 8);

        gfx_fill_quad(px[i0], py[i0], px[i1], py[i1], px[i2], py[i2], px[i3], py[i3], fcol);

        uint32_t ecol = rolling ? 0xFFB300 : 0x455A64;
        hal_draw_line(px[i0], py[i0], px[i1], py[i1], ecol);
        hal_draw_line(px[i1], py[i1], px[i2], py[i2], ecol);
        hal_draw_line(px[i2], py[i2], px[i3], py[i3], ecol);
        hal_draw_line(px[i3], py[i3], px[i0], py[i0], ecol);

        draw_face_pips_3d(cx, draw_cy, f, S, ax, ay, az);
    }
}
