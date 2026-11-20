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

/* 顏色明暗調整輔助 (percent: 50=變暗一半, 120=變亮20%) */
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
 * 2. 高速純整數 3D 三角形 / 四邊形 / 立體方塊 / 尖塔光柵化
 * ============================================================ */
void gfx_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t col) {
    /* 依 y 排序 (y0 <= y1 <= y2) */
    int t;
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

    int total_h = y2 - y0;
    if (total_h == 0) return;

    int sh = hal_get_height();
    int y_start = y0 < 0 ? 0 : y0;
    int y_end = y2 >= sh ? (sh - 1) : y2;

    for (int y = y_start; y <= y_end; y++) {
        int second_half = (y > y1) || (y1 == y0);
        int seg_h = second_half ? (y2 - y1) : (y1 - y0);
        if (seg_h == 0) continue;

        int xa = x0 + ((x2 - x0) * (y - y0)) / total_h;
        int xb = second_half
            ? (x1 + ((x2 - x1) * (y - y1)) / seg_h)
            : (x0 + ((x1 - x0) * (y - y0)) / seg_h);

        if (xa > xb) { t = xa; xa = xb; xb = t; }
        hal_fill_rect(xa, y, xb - xa + 1, 1, col);
    }
}

void gfx_fill_quad(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t col) {
    gfx_fill_tri(x0, y0, x1, y1, x2, y2, col);
    gfx_fill_tri(x0, y0, x2, y2, x3, y3, col);
}

void gfx_draw_iso_box(int cx, int cy, int hw, int hh, int h_up,
                      uint32_t col_top, uint32_t col_left, uint32_t col_right, uint32_t col_edge) {
    int ty = cy - h_up;
    /* 頂面菱形 (最亮) */
    gfx_fill_quad(cx, ty - hh, cx + hw, ty, cx, ty + hh, cx - hw, ty, col_top);
    /* 左側面 (中等受光) */
    gfx_fill_quad(cx - hw, ty, cx, ty + hh, cx, cy + hh, cx - hw, cy, col_left);
    /* 右側面 (陰影面) */
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
    /* 左前坡面 */
    gfx_fill_tri(cx, apex_y, cx - hw, cy, cx, cy + hh, col_left);
    /* 右前坡面 */
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
 * 4. 粒子、飛彈、畫面震動、飄浮數字 VFX 系統
 * ============================================================ */
#define MAX_PARTICLES 72
#define MAX_POPUPS    12

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

static Particle  g_parts[MAX_PARTICLES];
static PopupText g_pops[MAX_POPUPS];
static MissileFX g_missile;
static int g_shake_ms = 0;
static int g_shake_mag = 0;
static uint32_t g_vfx_rng = 0x91827364u;

static int vfx_rand(int mod) {
    g_vfx_rng = g_vfx_rng * 1664525u + 1013904223u;
    return (int)((g_vfx_rng >> 16) % (uint32_t)mod);
}

void gfx_vfx_reset(void) {
    memset(g_parts, 0, sizeof(g_parts));
    memset(g_pops, 0, sizeof(g_pops));
    memset(&g_missile, 0, sizeof(g_missile));
    g_shake_ms = 0;
    g_shake_mag = 0;
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
    for (int i = 0; i < 5; i++) {
        add_particle(wx + vfx_rand(12) - 6, wy, vfx_rand(36) - 18, -(vfx_rand(18) + 6), 300 + vfx_rand(140), 3, 1, 0xEFEBE9);
    }
}

void gfx_spawn_sparkles(int wx, int wy, uint32_t color, int count) {
    const uint32_t cols[4] = { color, 0xFFF176, 0xFFFFFF, 0xFFB300 };
    for (int i = 0; i < count; i++) {
        add_particle(wx, wy, vfx_rand(104) - 52, -(vfx_rand(80) + 24), 520 + vfx_rand(280), 3 + vfx_rand(3), 4, cols[i & 3]);
    }
}

void gfx_spawn_explosion(int wx, int wy) {
    const uint32_t cols[5] = { 0xFF3D00, 0xFF9100, 0xFFEA00, 0xFFFFFF, 0x5D4037 };
    for (int i = 0; i < 32; i++) {
        add_particle(wx, wy, vfx_rand(150) - 75, vfx_rand(140) - 90, 500 + vfx_rand(360), 4 + vfx_rand(5), 3, cols[i % 5]);
    }
    gfx_add_shake(460, 7);
}

void gfx_spawn_ice(int wx, int wy) {
    const uint32_t cols[3] = { 0x80D8FF, 0xE1F5FE, 0x40C4FF };
    for (int i = 0; i < 20; i++) {
        add_particle(wx, wy, vfx_rand(76) - 38, vfx_rand(56) - 48, 520 + vfx_rand(240), 3 + vfx_rand(3), 1, cols[i % 3]);
    }
}

void gfx_spawn_popup(int wx, int wy, const char *ascii_text, uint32_t color) {
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!g_pops[i].active) {
            g_pops[i].active = 1;
            g_pops[i].wx = wx;
            g_pops[i].wy = wy;
            g_pops[i].life = 1350;
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
    g_missile.duration = 780;
}

int gfx_missile_active(void) { return g_missile.active; }
void gfx_add_shake(int ms, int mag) { g_shake_ms = ms; g_shake_mag = mag; }
int gfx_get_shake_x(void) { return (g_shake_ms > 0 && g_shake_mag > 0) ? (vfx_rand(g_shake_mag * 2 + 1) - g_shake_mag) : 0; }
int gfx_get_shake_y(void) { return (g_shake_ms > 0 && g_shake_mag > 0) ? (vfx_rand(g_shake_mag * 2 + 1) - g_shake_mag) : 0; }

void gfx_vfx_tick(int dt_ms) {
    if (g_shake_ms > 0) { g_shake_ms -= dt_ms; if (g_shake_ms < 0) g_shake_ms = 0; }
    if (g_missile.active) {
        g_missile.elapsed += dt_ms;
        int t = g_missile.elapsed, dur = g_missile.duration;
        if (t >= dur) {
            g_missile.active = 0;
            gfx_spawn_explosion(g_missile.x1, g_missile.y1);
            gfx_spawn_popup(g_missile.x1 - 28, g_missile.y1 - 36, "BOOM!", 0xFF5252);
        } else {
            int mx = g_missile.x0 + ((g_missile.x1 - g_missile.x0) * t) / dur;
            int arc = (t * (dur - t)) / (dur * dur / 140 + 1);
            int my = g_missile.y0 + ((g_missile.y1 - g_missile.y0) * t) / dur - arc;
            add_particle(mx, my, vfx_rand(14) - 7, vfx_rand(14) - 7, 240, 4, 0, 0xFFB74D);
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
    if (g_shake_ms > 0 || g_missile.active) return 1;
    for (int i = 0; i < MAX_PARTICLES; i++) if (g_parts[i].active) return 1;
    for (int i = 0; i < MAX_POPUPS; i++) if (g_pops[i].active) return 1;
    return 0;
}

void gfx_vfx_render(int cam_x, int cam_y) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!g_parts[i].active) continue;
        int sx = (g_parts[i].x / 16) - cam_x;
        int sy = (g_parts[i].y / 16) - cam_y;
        int sz = g_parts[i].size;
        if (g_parts[i].life < g_parts[i].max_life / 3 && sz > 2) sz -= 1;
        hal_fill_rect(sx - sz / 2, sy - sz / 2, sz, sz, g_parts[i].color);
    }
    if (g_missile.active) {
        int t = g_missile.elapsed, dur = g_missile.duration;
        int mx = g_missile.x0 + ((g_missile.x1 - g_missile.x0) * t) / dur - cam_x;
        int arc = (t * (dur - t)) / (dur * dur / 140 + 1);
        int my = g_missile.y0 + ((g_missile.y1 - g_missile.y0) * t) / dur - arc - cam_y;
        int tx = g_missile.x1 - cam_x, ty = g_missile.y1 - cam_y;
        hal_draw_circle(tx, ty, 20, 0xFF1744);
        hal_draw_line(tx - 24, ty, tx + 24, ty, 0xFF1744);
        hal_draw_line(tx, ty - 24, tx, ty + 24, 0xFF1744);
        hal_fill_circle(mx, my, 8, 0xECEFF1);
        hal_fill_circle(mx, my, 4, 0xD50000);
    }
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!g_pops[i].active) continue;
        gfx_draw_ascii(g_pops[i].wx - cam_x, g_pops[i].wy - cam_y, g_pops[i].text, 2, g_pops[i].color, 1);
    }
}

/* ============================================================
 * 5. 精緻 UI 面板與 3D 海島等角大背景
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
    hal_fill_rect(sx - 6, sy + 2, 12, 5, 0x264A2B); /* 陰影 */
    hal_fill_rect(sx - 2, sy - 6, 5, 10, 0x6D4C41); /* 樹幹 */
    hal_fill_circle(sx, sy - 12, 9, 0x2E7D32);
    hal_fill_circle(sx - 3, sy - 15, 5, 0x4CAF50);
}

void gfx_draw_iso_world_bg(const MapDef *m, int cam_x, int cam_y, int tick_ms) {
    int sw = hal_get_width();
    int sh = hal_get_height();

    /* 1. 漸層海洋底色 */
    hal_clear(m->bg_water);
    hal_fill_rect(0, 0, sw, sh / 3, shade_color(m->bg_water, 82));

    /* 2. 動態海面波光 */
    int wave = (tick_ms / 160) % 28;
    uint32_t wcol = shade_color(m->bg_water, 130);
    for (int wy = - (cam_y % 44); wy < sh; wy += 44) {
        for (int wx = - (cam_x % 76) + wave; wx < sw; wx += 76) {
            hal_fill_rect(wx, wy, 20, 2, wcol);
            hal_fill_rect(wx + 28, wy + 18, 12, 2, wcol);
        }
    }

    /* 3. 超大 3D 等角立體浮島底座 (含3D海岸懸崖邊厚度！) */
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

    /* 3D 島嶼左岩壁與右陰影岩壁 */
    int cliff_h = 34;
    gfx_fill_quad(left_x, left_y, bot_x, bot_y, bot_x, bot_y + cliff_h, left_x, left_y + cliff_h, 0x8D6E63);
    gfx_fill_quad(bot_x, bot_y, right_x, right_y, right_x, right_y + cliff_h, bot_x, bot_y + cliff_h, 0x5D4037);

    /* 沙灘外框與綠地島頂 */
    gfx_fill_quad(top_x, top_y - 10, right_x + 14, right_y, bot_x, bot_y + 10, left_x - 14, left_y, 0xE6D5A7);
    gfx_fill_quad(top_x, top_y, right_x, right_y, bot_x, bot_y, left_x, left_y, m->bg_land);

    /* 4. 島嶼內部 3D 風景湖與 3D 樹木裝飾 */
    int lx, ly;
    gfx_grid_to_iso((m->grid_w * 16) / 2, (m->grid_h * 16) / 2, &lx, &ly);
    lx -= cam_x; ly -= cam_y;
    if (lx > -160 && lx < sw + 160 && ly > -120 && ly < sh + 120) {
        gfx_fill_quad(lx, ly - 44, lx + 88, ly, lx, ly + 44, lx - 88, ly, 0x29B6F6);
        hal_draw_line(lx - 24 + (wave % 14), ly - 6, lx + 10 + (wave % 14), ly - 6, 0xE1F5FE);
        hal_draw_line(lx - 10 + (wave % 10), ly + 10, lx + 26 + (wave % 10), ly + 10, 0xB3E5FC);
    }

    for (int gy = 3; gy < m->grid_h - 3; gy += 2) {
        for (int gx = 3; gx < m->grid_w - 3; gx += 2) {
            if (gx >= 6 && gx <= 10 && gy >= 4 && gy <= 7) continue;
            if (((gx * 5 + gy * 7) % 3) != 0) continue;
            int tx, ty;
            gfx_grid_to_iso(gx * 16, gy * 16, &tx, &ty);
            tx -= cam_x; ty -= cam_y;
            if (tx > -24 && tx < sw + 24 && ty > -24 && ty < sh + 24) {
                draw_iso_tree(tx, ty);
            }
        }
    }
}

/* ============================================================
 * 6. 真 3D 擠出立體建築、地標與等角地塊繪製
 * ============================================================ */
static void draw_3d_house(int sx, int sy, int level, uint32_t col) {
    uint32_t c_top = shade_color(col, 118);
    uint32_t c_l   = shade_color(col, 92);
    uint32_t c_r   = shade_color(col, 68);

    if (level == 0) {
        /* Lv.0 3D 領地圍樁 + 飄揚小旗 */
        hal_fill_rect(sx - 2, sy - 24, 2, 22, 0xECEFF1);
        gfx_fill_tri(sx, sy - 24, sx + 14, sy - 19, sx, sy - 14, col);
        hal_fill_rect(sx - 12, sy - 4, 3, 6, 0x8D6E63);
        hal_fill_rect(sx + 10, sy - 4, 3, 6, 0x8D6E63);
        return;
    }
    if (level == 1) {
        /* Lv.1 3D 可愛平房 (白牆 + 擁有者色金字塔屋頂) */
        gfx_draw_iso_box(sx, sy, 18, 9, 14, 0xFFF8E1, 0xF5E6C4, 0xD7C4A3, 0x5D4037);
        gfx_draw_iso_pyramid(sx, sy - 14, 21, 11, 13, c_l, c_r, 0x3E2723);
        hal_fill_rect(sx - 10, sy - 8, 5, 5, 0x4FC3F7);
        hal_fill_rect(sx + 5, sy - 8, 5, 7, 0x6D4C41);
        return;
    }
    if (level == 2) {
        /* Lv.2 3D 雙層洋房 */
        gfx_draw_iso_box(sx, sy, 20, 10, 24, 0xFFE0B2, 0xFFCC80, 0xE6A95C, 0x4E342E);
        gfx_draw_iso_pyramid(sx, sy - 24, 23, 12, 14, c_top, c_r, 0x3E2723);
        for (int r = 0; r < 2; r++) {
            hal_fill_rect(sx - 13, sy - 18 + r * 9, 5, 4, 0x29B6F6);
            hal_fill_rect(sx + 7, sy - 18 + r * 9, 5, 4, 0xFFF59D);
        }
        return;
    }
    if (level == 3) {
        /* Lv.3 3D 公寓大樓 */
        gfx_draw_iso_box(sx, sy, 21, 11, 38, c_top, 0xCFD8DC, 0x90A4AE, 0x37474F);
        gfx_draw_iso_box(sx, sy - 38, 12, 6, 6, c_top, c_l, c_r, 0x263238);
        for (int r = 0; r < 3; r++) {
            hal_fill_rect(sx - 14, sy - 30 + r * 10, 5, 5, 0xFFD54F);
            hal_fill_rect(sx - 6,  sy - 26 + r * 10, 4, 5, 0xFFD54F);
            hal_fill_rect(sx + 4,  sy - 26 + r * 10, 4, 5, 0x80DEEA);
            hal_fill_rect(sx + 11, sy - 30 + r * 10, 5, 5, 0x80DEEA);
        }
        return;
    }
    if (level == 4) {
        /* Lv.4 3D 玻璃帷幕摩天商辦 */
        gfx_draw_iso_box(sx, sy, 22, 11, 52, 0xE1F5FE, 0x4FC3F7, 0x0288D1, 0x01579B);
        gfx_draw_iso_box(sx, sy - 52, 14, 7, 8, c_top, c_l, c_r, 0x01579B);
        for (int r = 0; r < 4; r++) {
            hal_fill_rect(sx - 15, sy - 44 + r * 11, 11, 4, 0xE1F5FE);
            hal_fill_rect(sx + 4,  sy - 44 + r * 11, 11, 4, 0xB3E5FC);
        }
        return;
    }
    /* Lv.5 3D 黃金地標摩天塔 (台北 101 階梯塔造型!) */
    gfx_draw_iso_box(sx, sy,      23, 12, 26, 0xFFE082, 0xFFB300, 0xE65100, 0x5D4037);
    gfx_draw_iso_box(sx, sy - 26, 17, 9,  22, 0xFFF176, 0xFFC107, 0xF57C00, 0x5D4037);
    gfx_draw_iso_box(sx, sy - 48, 11, 6,  16, 0xFFFFFF, 0xFFD54F, 0xFFA000, 0x5D4037);
    hal_fill_rect(sx - 1, sy - 78, 3, 16, 0xFFF59D);
    hal_fill_circle(sx, sy - 78, 3, 0xFFFFFF);
}

static void draw_3d_comm(int sx, int sy, CommType comm, uint32_t col) {
    switch (comm) {
        case COMM_MALL:
            gfx_draw_iso_box(sx, sy, 26, 13, 24, 0xFFE0B2, 0xFF9800, 0xE65100, 0x4E342E);
            gfx_draw_iso_box(sx, sy - 24, 14, 7, 10, shade_color(col, 120), col, shade_color(col, 70), 0xFFFFFF);
            gfx_draw_ascii(sx - 3, sy - 14, "$", 1, 0xFFFFFF, 1);
            break;
        case COMM_HOTEL:
            gfx_draw_iso_box(sx, sy, 22, 11, 44, 0xF8BBD0, 0xEC407A, 0xAD1457, 0x880E4F);
            gfx_draw_ascii(sx - 4, sy - 38, "H", 2, 0xFFFFFF, 1);
            break;
        case COMM_LAB:
            gfx_draw_iso_box(sx, sy, 22, 11, 20, 0xCFD8DC, 0x90A4AE, 0x607D8B, 0x263238);
            hal_fill_circle(sx, sy - 24, 12, 0x80DEEA);
            hal_draw_circle(sx, sy - 24, 12, 0xFFFFFF);
            hal_fill_rect(sx - 1, sy - 44, 2, 10, 0x00E5FF);
            break;
        case COMM_PARK:
            draw_iso_tree(sx - 12, sy - 2);
            draw_iso_tree(sx + 12, sy - 4);
            hal_fill_circle(sx, sy + 2, 8, 0x29B6F6);
            hal_fill_circle(sx, sy + 2, 3, 0xE1F5FE);
            break;
        case COMM_GAS:
            gfx_draw_iso_box(sx, sy - 14, 24, 12, 6, 0xFF5252, 0xD32F2F, 0xB71C1C, 0xFFFFFF);
            hal_fill_rect(sx - 14, sy - 12, 4, 16, 0xECEFF1);
            hal_fill_rect(sx + 10, sy - 12, 4, 16, 0xECEFF1);
            gfx_draw_iso_box(sx, sy, 8, 4, 10, 0xFFD54F, 0xFFB300, 0xF57F17, 0x3E2723);
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
            gfx_draw_iso_box(sx, sy, 22, 11, 18, 0xFFF8E1, 0xFFE082, 0xFFB300, 0x6D4C41);
            gfx_draw_iso_pyramid(sx, sy - 18, 24, 12, 12, 0xFFD54F, 0xF57F17, 0x5D4037);
            gfx_draw_ascii(sx - 3, sy - 10, "$", 2, 0xB71C1C, 0);
            break;
        case TILE_HOSPITAL:
            gfx_draw_iso_box(sx, sy, 22, 11, 24, 0xFFFFFF, 0xECEFF1, 0xCFD8DC, 0x607D8B);
            hal_fill_rect(sx - 3, sy - 30, 6, 14, 0xE53935);
            hal_fill_rect(sx - 8, sy - 26, 16, 6, 0xE53935);
            break;
        case TILE_PRISON:
            gfx_draw_iso_box(sx, sy, 21, 11, 24, 0x78909C, 0x546E7A, 0x37474F, 0x263238);
            for (int b = -12; b <= -2; b += 4) hal_fill_rect(sx + b, sy - 18, 2, 14, 0xECEFF1);
            break;
        case TILE_MAGIC:
            gfx_draw_iso_box(sx, sy, 15, 8, 22, 0xCE93D8, 0xAB47BC, 0x6A1B9A, 0x311B92);
            gfx_draw_iso_pyramid(sx, sy - 22, 19, 10, 22, 0x7E57C2, 0x4527A0, 0xFFD54F);
            hal_fill_circle(sx, sy - 44, 3, 0xFFEA00);
            break;
        case TILE_SHOP:
            gfx_draw_iso_box(sx, sy, 20, 10, 18, 0xA5D6A7, 0x66BB6A, 0x2E7D32, 0x1B5E20);
            gfx_draw_iso_pyramid(sx, sy - 18, 22, 11, 10, 0x43A047, 0x1B5E20, 0xFFFFFF);
            break;
        case TILE_MINIGAME:
        case TILE_LOTTERY:
            gfx_draw_iso_box(sx, sy, 20, 10, 18, 0xFFE082, 0xFF7043, 0xD84315, 0xFFD54F);
            gfx_draw_ascii(sx - 4, sy - 24, "*", 2, 0xFFEA00, 1);
            break;
        case TILE_NEWS:
            gfx_draw_iso_box(sx, sy, 16, 8, 12, 0xFFFFFF, 0xBBDEFB, 0x64B5F6, 0x1565C0);
            gfx_draw_ascii(sx - 3, sy - 14, "?", 2, 0x1565C0, 0);
            break;
        case TILE_COUPON:
            gfx_draw_iso_box(sx, sy, 14, 7, 10, 0xFFF176, 0xFFD54F, 0xFFA000, 0xE65100);
            break;
        default:
            break;
    }
}

void gfx_draw_god_sprite(int sx, int sy, GodType god, int tick_ms) {
    if (god == GOD_NONE) return;
    int bob = ((tick_ms / 160) & 1) ? -3 : 2;
    int y = sy + bob;
    uint32_t aura = (god <= GOD_LAND) ? 0xFFF176 : 0x9575CD;
    uint32_t robe = 0xFFD54F;
    if (god == GOD_WEALTH) robe = 0xE53935;
    else if (god == GOD_FORTUNE) robe = 0xFF7043;
    else if (god == GOD_LAND) robe = 0x43A047;
    else if (god == GOD_POOR) robe = 0x546E7A;
    else if (god == GOD_BADLUCK) robe = 0x311B92;

    hal_draw_circle(sx, y, 11, aura);
    hal_fill_circle(sx, y - 4, 5, 0xFFCCBC);
    hal_fill_rect(sx - 5, y + 1, 10, 8, robe);
    if (god == GOD_WEALTH) hal_fill_rect(sx - 7, y - 10, 14, 3, 0xFFD54F);
    else if (god == GOD_BADLUCK) hal_fill_circle(sx, y - 11, 4, 0x424242);
}

static const uint32_t GRP_COLS[7] = {
    0x607D8B, 0xE53935, 0x1E88E5, 0x43A047, 0xFB8C00, 0x8E24AA, 0x00897B
};

void gfx_draw_iso_tile(int sx, int sy, const TileDef *td,
                       int owner, uint32_t owner_color, int level, CommType comm,
                       int has_roadblock, int has_mine, GodType map_god,
                       int toll, int is_cursor, int is_dest, int tick_ms) {
    int sw = hal_get_width();
    int sh = hal_get_height();
    if (sx < -110 || sx > sw + 110 || sy < -110 || sy > sh + 120) return;

    /* 1. 決定 3D 地塊表面顏色 */
    uint32_t top_c = 0xF5EFE0;
    if (td->type == TILE_START || td->type == TILE_BANK) top_c = 0xFFF3CD;
    else if (td->type == TILE_COMMERCIAL) top_c = 0xFCE4EC;
    else if (td->type == TILE_NEWS) top_c = 0xE3F2FD;
    else if (td->type == TILE_SHOP) top_c = 0xE8F5E9;
    else if (td->type == TILE_MAGIC) top_c = 0xF3E5F5;
    else if (td->type == TILE_MINIGAME || td->type == TILE_LOTTERY) top_c = 0xFFF8E1;
    else if (td->type == TILE_HOSPITAL || td->type == TILE_PRISON) top_c = 0xECEFF1;

    /* 若正在走向這格，閃爍金黃高亮！ */
    if (is_dest) {
        top_c = ((tick_ms / 120) & 1) ? 0xFFF176 : 0xFFE082;
    }

    uint32_t side_base = (owner >= 0) ? owner_color : GRP_COLS[td->group % 7];
    uint32_t left_c  = shade_color(side_base, 95);
    uint32_t right_c = shade_color(side_base, 65);
    uint32_t edge_c  = is_cursor ? 0xFFEA00 : (is_dest ? 0xFF6D00 : 0x37474F);

    /* 2. 繪製 3D 凸起等角石板方塊 */
    gfx_draw_iso_box(sx, sy + ISO_SLAB_H, ISO_HALF_W - 2, ISO_HALF_H - 1, ISO_SLAB_H,
                     top_c, left_c, right_c, edge_c);

    if (is_cursor || is_dest) {
        /* 加粗發光邊框 */
        hal_draw_line(sx, sy - ISO_HALF_H, sx + ISO_HALF_W - 1, sy, 0xFFEA00);
        hal_draw_line(sx + ISO_HALF_W - 1, sy, sx, sy + ISO_HALF_H, 0xFFEA00);
        hal_draw_line(sx, sy + ISO_HALF_H, sx - ISO_HALF_W + 1, sy, 0xFFEA00);
        hal_draw_line(sx - ISO_HALF_W + 1, sy, sx, sy - ISO_HALF_H, 0xFFEA00);
    }

    /* 3. 繪製地塊上的 3D 擠出建築物或地標 (偏移到後側留出人物站立空間) */
    if (td->type == TILE_LAND || td->type == TILE_COMMERCIAL) {
        if (owner >= 0) {
            if (td->type == TILE_COMMERCIAL) draw_3d_comm(sx, sy - 6, comm, owner_color);
            else draw_3d_house(sx, sy - 6, level, owner_color);
        }
    } else {
        draw_3d_landmark(sx, sy - 6, td->type);
    }

    /* 4. 3D 路障 / 地雷 / 地圖神明 */
    if (has_roadblock) {
        gfx_draw_iso_box(sx - 12, sy + 6, 9, 5, 9, 0xFF6D00, 0xFFFFFF, 0xE65100, 0x212121);
    }
    if (has_mine) {
        uint32_t led = ((tick_ms / 220) & 1) ? 0xFF1744 : 0x4A148C;
        hal_fill_circle(sx + 14, sy + 4, 6, 0x263238);
        hal_fill_circle(sx + 14, sy + 2, 3, led);
    }
    if (map_god != GOD_NONE) {
        gfx_draw_god_sprite(sx + 18, sy - 16, map_god, tick_ms);
    }

    /* 5. 清楚的地名與價格標籤 (只在視野內繪製，保持高速不卡頓) */
    if (sx > 28 && sx < sw - 54 && sy > 44 && sy < sh - 70) {
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
 * 7. 3D 立體動畫人物 + 分離陰影 + 頭頂玩家名牌與跳躍步數氣泡
 * ============================================================ */
void gfx_draw_character_3d(int sx, int ground_sy, int jump_h, int char_id, int player_idx,
                           uint32_t color, int walk_phase, int vehicle_type, GodType god,
                           int facing_left, int is_active, int steps_left, int tick_ms) {
    /* 1. 地面陰影留在地面 (人物跳越高、陰影縮小，3D 浮空感超強！) */
    int sh_w = 13 - (jump_h / 5);
    if (sh_w < 6) sh_w = 6;
    hal_fill_rect(sx - sh_w, ground_sy + 4, sh_w * 2, 5, 0x152028);

    if (is_active) {
        uint32_t ring = ((tick_ms / 180) & 1) ? 0xFFEA00 : 0xFFFFFF;
        hal_draw_circle(sx, ground_sy + 4, 15, ring);
    }

    int sy = ground_sy - jump_h;

    /* 2. 3D 立體載具 (機車 / 紅色跑車) */
    int body_y = sy;
    if (vehicle_type == 1) {
        gfx_draw_iso_box(sx, sy + 4, 16, 8, 7, 0xFF9100, 0xE65100, 0xBF360C, 0x212121);
        hal_fill_circle(sx - 10, sy + 7, 4, 0x212121);
        hal_fill_circle(sx + 10, sy + 7, 4, 0x212121);
        body_y -= 6;
    } else if (vehicle_type == 2) {
        gfx_draw_iso_box(sx, sy + 4, 20, 10, 8, 0xFF1744, 0xD50000, 0x8E0000, 0xFFFFFF);
        gfx_draw_iso_box(sx, sy - 4, 10, 5, 5, 0x80D8FF, 0x4FC3F7, 0x0288D1, 0x212121);
        hal_fill_circle(sx - 12, sy + 8, 4, 0x212121);
        hal_fill_circle(sx + 12, sy + 8, 4, 0x212121);
        body_y -= 7;
    }

    /* 3. 3D 擺動雙腳 */
    if (vehicle_type == 0) {
        int l_up = (walk_phase & 1) ? 3 : 0;
        int r_up = (walk_phase & 1) ? 0 : 3;
        hal_fill_rect(sx - 6, body_y - 2 - l_up, 4, 8, 0x263238);
        hal_fill_rect(sx + 2, body_y - 2 - r_up, 4, 8, 0x37474F);
    }

    /* 4. 3D 立體身軀方塊 (左亮右暗受光!) */
    gfx_draw_iso_box(sx, body_y - 2, 9, 5, 11,
                     shade_color(color, 120), color, shade_color(color, 72), 0xFFFFFF);

    /* 5. 立體圓頭 + 高光 + 方向眼睛 */
    int hy = body_y - 19;
    hal_fill_circle(sx, hy, 8, 0xFFCCBC);
    hal_fill_circle(sx - 2, hy - 2, 3, 0xFFE0D0); /* 3D 高光 */
    int edx = facing_left ? -2 : 2;
    hal_fill_rect(sx - 3 + edx, hy - 1, 2, 2, 0x1A1A1A);
    hal_fill_rect(sx + 2 + edx, hy - 1, 2, 2, 0x1A1A1A);

    /* 6. 角色招牌頭飾 */
    switch (char_id % 6) {
        case 0: /* 孫小美 */
            hal_fill_circle(sx - 8, hy - 4, 4, 0x3E2723);
            hal_fill_circle(sx + 8, hy - 4, 4, 0x3E2723);
            hal_fill_rect(sx - 6, hy - 8, 12, 3, 0xFF1744);
            break;
        case 1: /* 阿土伯 */
            gfx_draw_iso_pyramid(sx, hy - 6, 13, 6, 7, 0xFFD54F, 0xF57F17, 0x5D4037);
            hal_fill_rect(sx - 4, hy + 3, 8, 2, 0x5D4037);
            break;
        case 2: /* 錢夫人 */
            hal_fill_rect(sx - 11, hy - 7, 22, 3, 0x8E24AA);
            hal_fill_rect(sx - 5, hy - 11, 10, 4, 0xCE93D8);
            break;
        case 3: /* 沙隆巴斯 */
            hal_fill_circle(sx, hy - 6, 6, 0xFFFFFF);
            hal_fill_rect(sx - 1, hy - 8, 3, 3, 0xE53935);
            break;
        case 4: /* 金貝貝 */
            hal_fill_rect(sx - 6, hy - 9, 12, 4, 0xF48FB1);
            hal_fill_circle(sx + edx, hy + 4, 2, 0xFFEB3B);
            break;
        default: /* 忍太郎 */
            hal_fill_rect(sx - 8, hy - 6, 16, 4, 0x263238);
            hal_fill_rect(sx - 8, hy - 5, 16, 2, 0x43A047);
            break;
    }

    /* 7. 附身神明 */
    if (god != GOD_NONE) {
        gfx_draw_god_sprite(sx + 18, hy - 8, god, tick_ms);
    }

    /* 8. 頭頂顯眼名牌 (P1..P4) + 行動中動態黃色箭頭與剩餘步數倒數！ */
    int tag_y = hy - 25;
    char ptag[16];
    snprintf(ptag, sizeof(ptag), "P%d", player_idx + 1);
    hal_fill_rect(sx - 11, tag_y, 22, 11, color);
    hal_draw_rect(sx - 11, tag_y, 22, 11, 0xFFFFFF);
    gfx_draw_ascii(sx - 6, tag_y + 2, ptag, 1, 0xFFFFFF, 0);

    if (is_active) {
        int arrow_bob = ((tick_ms / 140) & 1) ? -4 : 0;
        int ay = tag_y - 6 + arrow_bob;
        gfx_fill_tri(sx - 7, ay - 8, sx + 7, ay - 8, sx, ay, 0xFFEA00);
        if (steps_left > 0) {
            char sb[16];
            snprintf(sb, sizeof(sb), "%d", steps_left);
            hal_fill_circle(sx, ay - 19, 11, 0xD50000);
            hal_draw_circle(sx, ay - 19, 11, 0xFFEA00);
            gfx_draw_ascii(sx - 4, ay - 25, sb, 2, 0xFFEA00, 1);
        }
    }
}

/* ============================================================
 * 8. 真 3D 軟體旋轉多面體立體骰子 (8頂點 6面 3軸旋轉 + 3D點數投影)
 * ============================================================ */
static const int8_t CUBE_V[8][3] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
};

/* 每個面的 4 個頂點索引 (順時針從外部看，外法線朝外) 與點數(1..6) */
static const uint8_t CUBE_F[6][4] = {
    {4, 5, 6, 7}, /* Face 0 (+Z): 1點 */
    {1, 0, 3, 2}, /* Face 1 (-Z): 6點 */
    {5, 1, 2, 6}, /* Face 2 (+X): 2點 */
    {0, 4, 7, 3}, /* Face 3 (-X): 5點 */
    {0, 1, 5, 4}, /* Face 4 (-Y 上面): 3點 */
    {7, 6, 2, 3}  /* Face 5 (+Y 下面): 4點 */
};
static const uint8_t FACE_VAL[6] = { 1, 6, 2, 5, 3, 4 };

/* 讓指定點數 (1..6) 朝向鏡頭正斜前方的靜止 3D 角度 (ax, ay, az in 0..255) */
static const int16_t REST_ANGLES[7][3] = {
    { 18,  16, 0}, /* unused */
    { 18,  16, 0}, /* 1 (+Z) */
    { 18, -48, 0}, /* 2 (+X) */
    {-46,  16, 0}, /* 3 (-Y) */
    { 82,  16, 0}, /* 4 (+Y) */
    { 18,  80, 0}, /* 5 (-X) */
    { 18, 144, 0}  /* 6 (-Z) */
};

static void rotate_pt_3d(int x, int y, int z, int ax, int ay, int az, int *ox, int *oy, int *oz) {
    /* 1. 繞 X 軸 */
    int sx = isin(ax), cx = icos(ax);
    int y1 = (y * cx - z * sx) >> 10;
    int z1 = (y * sx + z * cx) >> 10;
    int x1 = x;
    /* 2. 繞 Y 軸 */
    int sy = isin(ay), cy = icos(ay);
    int x2 = (x1 * cy + z1 * sy) >> 10;
    int z2 = (-x1 * sy + z1 * cy) >> 10;
    int y2 = y1;
    /* 3. 繞 Z 軸 */
    int sz = isin(az), cz = icos(az);
    *ox = (x2 * cz - y2 * sz) >> 10;
    *oy = (x2 * sz + y2 * cz) >> 10;
    *oz = z2;
}

static void draw_face_pips_3d(int cx, int cy, int fidx, int S, int ax, int ay, int az) {
    int val = FACE_VAL[fidx];
    /* 每個面的本地座標軸 (center, u_axis, v_axis) 乘上 S */
    static const int8_t F_BASIS[6][9] = {
        /* center(3), u(3), v(3) */
        { 0, 0, 1,   1, 0, 0,   0, 1, 0 }, /* +Z */
        { 0, 0,-1,  -1, 0, 0,   0, 1, 0 }, /* -Z */
        { 1, 0, 0,   0, 0,-1,   0, 1, 0 }, /* +X */
        {-1, 0, 0,   0, 0, 1,   0, 1, 0 }, /* -X */
        { 0,-1, 0,   1, 0, 0,   0, 0, 1 }, /* -Y */
        { 0, 1, 0,   1, 0, 0,   0, 0,-1 }  /* +Y */
    };
    /* 點數在本地 (u, v) 平面位置 (-52..+52%) */
    static const int8_t PIP_UV[7][6][2] = {
        {{0,0}},
        {{0,0}},                                                     /* 1 */
        {{-50,-50},{50,50}},                                         /* 2 */
        {{-50,-50},{0,0},{50,50}},                                   /* 3 */
        {{-50,-50},{50,-50},{-50,50},{50,50}},                       /* 4 */
        {{-50,-50},{50,-50},{0,0},{-50,50},{50,50}},                 /* 5 */
        {{-50,-52},{50,-52},{-50,0},{50,0},{-50,52},{50,52}}         /* 6 */
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

    /* 1. 地面立體投影 (隨彈跳高度縮放) */
    int sh_r = 26 - (bounce_h / 4);
    if (sh_r < 10) sh_r = 10;
    hal_fill_rect(cx - sh_r, cy + 36, sh_r * 2, 10, 0x0D161E);

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

    int S = 26;
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

    /* 2. 繪製 6 個面 (背面剔除 + 真3D法線打光 + 3D點數投影) */
    for (int f = 0; f < 6; f++) {
        int i0 = CUBE_F[f][0], i1 = CUBE_F[f][1], i2 = CUBE_F[f][2], i3 = CUBE_F[f][3];
        /* 2D 外積檢查是否面向鏡頭 */
        int cross = (px[i1] - px[i0]) * (py[i2] - py[i0]) - (py[i1] - py[i0]) * (px[i2] - px[i0]);
        if (cross <= 0) continue;

        /* 根據面的平均 Z 深度與 Y 高度計算立體明暗 */
        int avg_z = (pz[i0] + pz[i1] + pz[i2] + pz[i3]) / 4;
        int avg_y = (py[i0] + py[i1] + py[i2] + py[i3]) / 4 - draw_cy;
        int light = 225 + (avg_z * 2) - avg_y;
        if (light > 255) light = 255;
        if (light < 150) light = 150;
        uint32_t fcol = ((uint32_t)light << 16) | ((uint32_t)light << 8) | (uint32_t)(light - 8);

        gfx_fill_quad(px[i0], py[i0], px[i1], py[i1], px[i2], py[i2], px[i3], py[i3], fcol);

        /* 骰子稜邊 */
        uint32_t ecol = rolling ? 0xFFB300 : 0x455A64;
        hal_draw_line(px[i0], py[i0], px[i1], py[i1], ecol);
        hal_draw_line(px[i1], py[i1], px[i2], py[i2], ecol);
        hal_draw_line(px[i2], py[i2], px[i3], py[i3], ecol);
        hal_draw_line(px[i3], py[i3], px[i0], py[i0], ecol);

        /* 投影該面上的 3D 骰子點數！ */
        draw_face_pips_3d(cx, draw_cy, f, S, ax, ay, az);
    }
}
