/*
 * 無敵 CD-952 (Besta RTOS / Project Muteki) 遊戲平台實作
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

static lcd_t *g_lcd = NULL;
static int g_running = 1;
static uint32_t g_text_bg = 0x102027;

static size_t utf8_to_utf16(const char *src, UTF16 *dst, size_t max_dst) {
    size_t out = 0;
    size_t i = 0;
    size_t src_len = strlen(src);
    while (i < src_len && out + 1 < max_dst) {
        unsigned char c = (unsigned char)src[i];
        uint32_t cp = 0;
        if ((c & 0x80) == 0) {
            cp = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < src_len) {
            cp = ((c & 0x1F) << 6) | (src[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < src_len) {
            cp = ((c & 0x0F) << 12) | ((src[i + 1] & 0x3F) << 6) | (src[i + 2] & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < src_len) {
            cp = 0x3F;
            i += 4;
        } else {
            cp = 0x3F;
            i += 1;
        }
        dst[out++] = (UTF16)cp;
    }
    dst[out] = 0;
    return out;
}

static inline uint16_t rgb888_to_rgb565(uint32_t rgb) {
    uint16_t r = (uint16_t)((rgb >> 16) & 0xFF);
    uint16_t g = (uint16_t)((rgb >> 8) & 0xFF);
    uint16_t b = (uint16_t)(rgb & 0xFF);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void put_pixel_fb(int x, int y, uint32_t rgb) {
    if (!g_lcd || !g_lcd->surface || !g_lcd->surface->buffer) return;
    if (x < 0 || y < 0 || x >= (int)g_lcd->width || y >= (int)g_lcd->height) return;

    int stride = g_lcd->width;
    if (g_lcd->surface->depth == LCD_SURFACE_PIXFMT_XRGB) {
        uint32_t *fb32 = (uint32_t *)g_lcd->surface->buffer;
        fb32[y * stride + x] = rgb;
    } else if (g_lcd->surface->depth == LCD_SURFACE_PIXFMT_RGB565) {
        uint16_t *fb16 = (uint16_t *)g_lcd->surface->buffer;
        fb16[y * stride + x] = rgb888_to_rgb565(rgb);
    }
}

int hal_get_width(void)  { return g_lcd ? (int)g_lcd->width  : 800; }
int hal_get_height(void) { return g_lcd ? (int)g_lcd->height : 480; }

void hal_set_text_bg(uint32_t rgb) {
    g_text_bg = rgb;
}

void hal_clear(uint32_t rgb) {
    g_text_bg = rgb;
    rgbSetBkColor(rgb);
    ClearScreen(false);
    hal_fill_rect(0, 0, hal_get_width(), hal_get_height(), rgb);
}

void hal_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            put_pixel_fb(px, py, rgb);
        }
    }
}

void hal_draw_rect(int x, int y, int w, int h, uint32_t rgb) {
    for (int px = x; px < x + w; px++) {
        put_pixel_fb(px, y, rgb);
        put_pixel_fb(px, y + h - 1, rgb);
    }
    for (int py = y; py < y + h; py++) {
        put_pixel_fb(x, py, rgb);
        put_pixel_fb(x + w - 1, py, rgb);
    }
}

void hal_draw_text(int x, int y, const char *utf8_text, uint32_t rgb) {
    if (!g_lcd || !utf8_text || !utf8_text[0]) return;
    UTF16 u16buf[256];
    utf8_to_utf16(utf8_text, u16buf, 256);

    rgbSetColor(rgb);
    rgbSetBkColor(g_text_bg);
    SetFontType(MONOSPACE_CJK);
    WriteAlignString(
        x, y,
        u16buf,
        g_lcd->width - x,
        STR_ALIGN_LEFT,
        PRINT_NONE
    );
}

void hal_sleep_ms(int ms) {
    OSSleep(ms);
}

uint32_t hal_get_seed(void) {
    return (uint32_t)time(NULL) ^ 0x9E3779B9u;
}

const char *hal_get_save_path(void) {
    return "C:\\richman4.sav";
}

void hal_request_quit(void) {
    g_running = 0;
}

int main(void) {
    g_lcd = GetActiveLCD();
    if (!g_lcd || !g_lcd->surface) {
        return 1;
    }

    game_init();
    game_render();

    ui_event_t uievent = {0};
    while (g_running) {
        if (TestKeyEvent(&uievent) && GetEvent(&uievent)) {
            if (uievent.event_type == UI_EVENT_TYPE_KEY) {
                GameKey k = GKEY_NONE;
                switch (uievent.key_code0) {
                    case 1:   k = GKEY_BACK; break;
                    case 2:   k = GKEY_LEFT; break;
                    case 3:   k = GKEY_UP; break;
                    case 4:   k = GKEY_RIGHT; break;
                    case 5:   k = GKEY_DOWN; break;
                    case 6:   k = GKEY_PGUP; break;
                    case 7:   k = GKEY_PGDN; break;
                    case 8:   k = GKEY_BACK; break;
                    case 10:
                    case 13:
                    case 32:  k = GKEY_OK; break;
                    case 49:  k = GKEY_1; break;
                    case 50:  k = GKEY_2; break;
                    case 51:  k = GKEY_3; break;
                    case 52:  k = GKEY_4; break;
                    case 53:  k = GKEY_5; break;
                    case 54:  k = GKEY_6; break;
                    case 67:
                    case 99:  k = GKEY_CARD; break;
                    case 70:
                    case 102: k = GKEY_SAVE; break;
                    case 72:
                    case 104: k = GKEY_HELP; break;
                    case 83:
                    case 115: k = GKEY_STOCK; break;
                    default:  break;
                }
                if (k != GKEY_NONE) {
                    game_on_key(k);
                    if (g_running) game_render();
                }
            }
            ClearEvent(&uievent);
        } else {
            OSSleep(20);
        }
    }

    game_shutdown();
    return 0;
}
