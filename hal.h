#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>

/* 使用 GKEY_ 前綴，避免與 Muteki SDK 內建 KEY_* 撞名 */
typedef enum {
    GKEY_NONE = 0,
    GKEY_UP,
    GKEY_DOWN,
    GKEY_LEFT,
    GKEY_RIGHT,
    GKEY_PGUP,
    GKEY_PGDN,
    GKEY_OK,       /* Enter / Space */
    GKEY_BACK,     /* ESC / Backspace */
    GKEY_CARD,     /* C: 卡片袋 */
    GKEY_STOCK,    /* S: 股市 */
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

void hal_clear(uint32_t rgb);
void hal_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void hal_draw_rect(int x, int y, int w, int h, uint32_t rgb);
void hal_set_text_bg(uint32_t rgb);
void hal_draw_text(int x, int y, const char *utf8_text, uint32_t rgb);

void hal_sleep_ms(int ms);
uint32_t hal_get_seed(void);
const char *hal_get_save_path(void);
void hal_request_quit(void);

#endif
