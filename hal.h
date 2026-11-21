#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>

/* 使用 GKEY_ 前綴，完全避免與 Muteki SDK 內建 KEY_* 撞名 */
typedef enum {
    GKEY_NONE = 0,
    GKEY_UP,
    GKEY_DOWN,
    GKEY_LEFT,
    GKEY_RIGHT,
    GKEY_PGUP,
    GKEY_PGDN,
    GKEY_OK,       /* Enter */
    GKEY_SPACE,    /* Space (確認 / 加速AI) */
    GKEY_BACK,     /* ESC (暫停選單 / 返回) */
    GKEY_CARD,     /* C: 卡片袋 */
    GKEY_STOCK,    /* S: 股市 */
    GKEY_MAP,      /* M: 自由查看3D全地圖 */
    GKEY_SAVE,     /* F: 存檔 */
    GKEY_HELP,     /* H: 說明 */
    GKEY_1,
    GKEY_2,
    GKEY_3,
    GKEY_4,
    GKEY_5,
    GKEY_6
} GameKey;

int  hal_get_width(void);
int  hal_get_height(void);

/* 像素雙緩衝繪圖 */
void hal_clear(uint32_t rgb);
void hal_put_pixel(int x, int y, uint32_t rgb);
void hal_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void hal_draw_rect(int x, int y, int w, int h, uint32_t rgb);
void hal_fill_circle(int cx, int cy, int r, uint32_t rgb);
void hal_draw_circle(int cx, int cy, int r, uint32_t rgb);
void hal_draw_line(int x0, int y0, int x1, int y1, uint32_t rgb);

/* 局部更新與靜態背景快取 (大幅降低 ARM9 繪圖量並徹底消除文字閃爍) */
void hal_begin_partial(void);
int  hal_save_bg_cache(void);
void hal_restore_bg_rect(int x, int y, int w, int h);
void hal_present(void);
void hal_present_rect(int x, int y, int w, int h);

/* 中文文字佇列 */
void hal_set_text_bg(uint32_t rgb);
void hal_draw_text(int x, int y, const char *utf8_text, uint32_t rgb);
void hal_clear_text_in_rect(int rx, int ry, int rw, int rh);
void hal_dim_rect(int rx, int ry, int rw, int rh);

void hal_sleep_ms(int ms);
uint32_t hal_get_seed(void);
const char *hal_get_save_path(void);
void hal_request_quit(void);

#endif
