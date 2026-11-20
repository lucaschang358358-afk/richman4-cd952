/*
 * 無敵 CD-952 (Besta RTOS / Project Muteki) 3D 雙緩衝遊戲平台實作 v3
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if __has_include(<muteki/common.h>)
#include <muteki/common.h>
#endif
#if __has_include(<muteki/threading.h>)
#include <muteki/threading.h>
#endif
#if __has_include(<muteki/utf16.h>)
#include <muteki/utf16.h>
#endif

#if __has_include(<muteki/draw/font.h>)
#include <muteki/draw/font.h>
#elif __has_include(<muteki/ui/font.h>)
#include <muteki/ui/font.h>
#elif __has_include(<muteki/font.h>)
#include <muteki/font.h>
#endif

#if __has_include(<muteki/draw/lcd.h>)
#include <muteki/draw/lcd.h>
#elif __has_include(<muteki/ui/lcd.h>)
#include <muteki/ui/lcd.h>
#elif __has_include(<muteki/lcd.h>)
#include <muteki/lcd.h>
#endif

#if __has_include(<muteki/draw/surface.h>)
#include <muteki/draw/surface.h>
#elif __has_include(<muteki/ui/surface.h>)
#include <muteki/ui/surface.h>
#elif __has_include(<muteki/surface.h>)
#include <muteki/surface.h>
#endif

#if __has_include(<muteki/ui/canvas.h>)
#include <muteki/ui/canvas.h>
#endif
#if __has_include(<muteki/ui/common.h>)
#include <muteki/ui/common.h>
#endif
#if __has_include(<muteki/ui.h>)
#include <muteki/ui.h>
#endif

#if __has_include(<muteki/ui/framework/event.h>)
#include <muteki/ui/framework/event.h>
#elif __has_include(<muteki/ui/event.h>)
#include <muteki/ui/event.h>
#elif __has_include(<muteki/event.h>)
#include <muteki/event.h>
#endif

#include "hal.h"
#include "game.h"

#define MAX_TEXT_QUEUE 180

typedef struct {
    int x;
    int y;
    uint32_t fg;
    uint32_t bg;
    char text[140];
} TextCmd;

static lcd_t *g_lcd = NULL;
static int g_w = 800;
static int g_h = 480;
static int g_is_xrgb = 1;
static void *g_backbuf = NULL;
static size_t g_fb_bytes = 0;

static TextCmd g_text_q[MAX_TEXT_QUEUE];
static int g_text_q_len = 0;
static uint32_t g_cur_text_bg = 0x0B1929;
static int g_running = 1;

static inline uint16_t rgb888_to_rgb565(uint32_t rgb) {
    uint16_t r = (uint16_t)((rgb >> 16) & 0xFF);
    uint16_t g = (uint16_t)((rgb >> 8) & 0xFF);
    uint16_t b = (uint16_t)(rgb & 0xFF);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static size_t utf8_to_utf16(const char *src, UTF16 *dst, size_t max_dst) {
    size_t out = 0, i = 0, src_len = strlen(src);
    while (i < src_len && out + 1 < max_dst) {
        unsigned char c = (unsigned char)src[i];
        uint32_t cp = 0;
        if ((c & 0x80) == 0) { cp = c; i += 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < src_len) {
            cp = ((c & 0x1F) << 6) | (src[i + 1] & 0x3F); i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < src_len) {
            cp = ((c & 0x0F) << 12) | ((src[i + 1] & 0x3F) << 6) | (src[i + 2] & 0x3F); i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < src_len) {
            cp = 0x3F; i += 4;
        } else { cp = 0x3F; i += 1; }
        dst[out++] = (UTF16)cp;
    }
    dst[out] = 0;
    return out;
}

int hal_get_width(void)  { return g_w; }
int hal_get_height(void) { return g_h; }

void hal_put_pixel(int x, int y, uint32_t rgb) {
    if (!g_backbuf || x < 0 || y < 0 || x >= g_w || y >= g_h) return;
    if (g_is_xrgb) ((uint32_t *)g_backbuf)[y * g_w + x] = rgb;
    else ((uint16_t *)g_backbuf)[y * g_w + x] = rgb888_to_rgb565(rgb);
}

void hal_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    if (!g_backbuf || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > g_w) ? g_w : (x + w);
    int y1 = (y + h > g_h) ? g_h : (y + h);
    if (x0 >= x1 || y0 >= y1) return;

    if (g_is_xrgb) {
        uint32_t *fb = (uint32_t *)g_backbuf;
        for (int py = y0; py < y1; py++) {
            uint32_t *row = fb + py * g_w + x0;
            for (int n = x1 - x0; n > 0; n--) *row++ = rgb;
        }
    } else {
        uint16_t c16 = rgb888_to_rgb565(rgb);
        uint16_t *fb = (uint16_t *)g_backbuf;
        for (int py = y0; py < y1; py++) {
            uint16_t *row = fb + py * g_w + x0;
            for (int n = x1 - x0; n > 0; n--) *row++ = c16;
        }
    }
}

void hal_clear(uint32_t rgb) {
    g_text_q_len = 0;
    g_cur_text_bg = rgb;
    hal_fill_rect(0, 0, g_w, g_h, rgb);
}

void hal_draw_rect(int x, int y, int w, int h, uint32_t rgb) {
    if (w <= 0 || h <= 0) return;
    hal_fill_rect(x, y, w, 1, rgb);
    hal_fill_rect(x, y + h - 1, w, 1, rgb);
    hal_fill_rect(x, y, 1, h, rgb);
    hal_fill_rect(x + w - 1, y, 1, h, rgb);
}

void hal_fill_circle(int cx, int cy, int r, uint32_t rgb) {
    if (r <= 0) return;
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= g_h) continue;
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r2) dx++;
        hal_fill_rect(cx - dx, py, dx * 2 + 1, 1, rgb);
    }
}

void hal_draw_circle(int cx, int cy, int r, uint32_t rgb) {
    if (r <= 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        hal_put_pixel(cx + x, cy + y, rgb);
        hal_put_pixel(cx + y, cy + x, rgb);
        hal_put_pixel(cx - y, cy + x, rgb);
        hal_put_pixel(cx - x, cy + y, rgb);
        hal_put_pixel(cx - x, cy - y, rgb);
        hal_put_pixel(cx - y, cy - x, rgb);
        hal_put_pixel(cx + y, cy - x, rgb);
        hal_put_pixel(cx + x, cy - y, rgb);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x + 1); }
    }
}

void hal_draw_line(int x0, int y0, int x1, int y1, uint32_t rgb) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        hal_put_pixel(x0, y0, rgb);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void hal_set_text_bg(uint32_t rgb) { g_cur_text_bg = rgb; }

void hal_draw_text(int x, int y, const char *utf8_text, uint32_t rgb) {
    if (!utf8_text || !utf8_text[0] || g_text_q_len >= MAX_TEXT_QUEUE) return;
    if (y < -18 || y >= g_h || x >= g_w) return;
    TextCmd *tc = &g_text_q[g_text_q_len++];
    tc->x = x; tc->y = y;
    tc->fg = rgb; tc->bg = g_cur_text_bg;
    snprintf(tc->text, sizeof(tc->text), "%s", utf8_text);
}

void hal_present(void) {
    if (!g_lcd || !g_lcd->surface || !g_lcd->surface->buffer || !g_backbuf) return;
    memcpy(g_lcd->surface->buffer, g_backbuf, g_fb_bytes);

    SetFontType(MONOSPACE_CJK);
    UTF16 u16[160];
    for (int i = 0; i < g_text_q_len; i++) {
        TextCmd *tc = &g_text_q[i];
        int draw_x = tc->x < 0 ? 0 : tc->x;
        int max_w = g_w - draw_x;
        if (max_w <= 8) continue;
        utf8_to_utf16(tc->text, u16, 160);
        rgbSetColor(tc->fg);
        rgbSetBkColor(tc->bg);
        WriteAlignString(draw_x, tc->y, u16, max_w, STR_ALIGN_LEFT, PRINT_NONE);
    }
    g_text_q_len = 0;
}

void hal_sleep_ms(int ms) { OSSleep(ms); }
uint32_t hal_get_seed(void) { return (uint32_t)time(NULL) ^ 0x9E3779B9u; }
const char *hal_get_save_path(void) { return "C:\\richman4_3d.sav"; }
void hal_request_quit(void) { g_running = 0; }

static GameKey map_besta_key(short code) {
    switch (code) {
        case 1:   return GKEY_BACK;
        case 2:   return GKEY_LEFT;
        case 3:   return GKEY_UP;
        case 4:   return GKEY_RIGHT;
        case 5:   return GKEY_DOWN;
        case 6:   return GKEY_PGUP;
        case 7:   return GKEY_PGDN;
        case 8:   return GKEY_BACK;
        case 10:
        case 13:  return GKEY_OK;
        case 32:  return GKEY_SPACE;
        case 49:  return GKEY_1;
        case 50:  return GKEY_2;
        case 51:  return GKEY_3;
        case 52:  return GKEY_4;
        case 53:  return GKEY_5;
        case 54:  return GKEY_6;
        case 67:
        case 99:  return GKEY_CARD;
        case 70:
        case 102: return GKEY_SAVE;
        case 72:
        case 104: return GKEY_HELP;
        case 77:
        case 109: return GKEY_MAP;
        case 83:
        case 115: return GKEY_STOCK;
        default:  return GKEY_NONE;
    }
}

int main(void) {
    g_lcd = GetActiveLCD();
    if (!g_lcd || !g_lcd->surface || !g_lcd->surface->buffer) return 1;

    g_w = (int)g_lcd->width;
    g_h = (int)g_lcd->height;
    g_is_xrgb = (g_lcd->surface->depth == LCD_SURFACE_PIXFMT_XRGB);
    g_fb_bytes = (size_t)g_w * (size_t)g_h * (g_is_xrgb ? 4u : 2u);
    g_backbuf = malloc(g_fb_bytes);
    if (!g_backbuf) return 1;

    game_init();
    game_render();

    ui_event_t uievent = {0};
    while (g_running) {
        int drained = 0;
        while (drained < 4 && TestKeyEvent(&uievent) && GetEvent(&uievent)) {
            drained++;
            if (uievent.event_type == UI_EVENT_TYPE_KEY) {
                GameKey k = map_besta_key(uievent.key_code0);
                if (k != GKEY_NONE) game_on_key(k);
            }
            ClearEvent(&uievent);
        }

        if (!g_running) break;

        game_tick(45);
        if (game_needs_redraw()) game_render();
        OSSleep(35);
    }

    game_shutdown();
    free(g_backbuf);
    return 0;
}
