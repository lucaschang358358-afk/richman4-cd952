#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "hal.h"
#include "maps.h"

/* 2.5D 斜45度等角3D地塊尺寸 (菱形寬 96px、高 48px、立體厚度 14px) */
#define ISO_HALF_W 48
#define ISO_HALF_H 24
#define ISO_SLAB_H 14

typedef enum {
    COMM_NONE = 0,
    COMM_MALL,
    COMM_HOTEL,
    COMM_LAB,
    COMM_PARK,
    COMM_GAS
} CommType;

typedef enum {
    GOD_NONE = 0,
    GOD_WEALTH,     /* 財神 */
    GOD_FORTUNE,    /* 福神 */
    GOD_LAND,       /* 土地公 */
    GOD_POOR,       /* 窮神 */
    GOD_BADLUCK     /* 衰神 */
} GodType;

/* 網格座標 (fixed-point * 16) 轉 2.5D 等角世界座標 */
void gfx_grid_to_iso(int gx16, int gy16, int *wx, int *wy);

/* 基礎 3D 多邊形與立體方塊原語 (純整數高速光柵化) */
void gfx_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t col);
void gfx_fill_quad(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t col);
void gfx_draw_iso_box(int cx, int cy, int hw, int hh, int h_up,
                      uint32_t col_top, uint32_t col_left, uint32_t col_right, uint32_t col_edge);
void gfx_draw_iso_pyramid(int cx, int cy, int hw, int hh, int h_up,
                          uint32_t col_left, uint32_t col_right, uint32_t col_edge);

/* 粒子、飛彈、震動、飄浮數字 VFX 系統 */
void gfx_vfx_reset(void);
void gfx_vfx_tick(int dt_ms);
void gfx_vfx_render(int cam_x, int cam_y);
int  gfx_vfx_is_active(void);

void gfx_spawn_dust(int wx, int wy);
void gfx_spawn_sparkles(int wx, int wy, uint32_t color, int count);
void gfx_spawn_explosion(int wx, int wy);
void gfx_spawn_ice(int wx, int wy);
void gfx_spawn_popup(int wx, int wy, const char *ascii_text, uint32_t color);
void gfx_launch_missile(int x0, int y0, int x1, int y1);
void gfx_add_shake(int ms, int mag);
int  gfx_get_shake_x(void);
int  gfx_get_shake_y(void);
int  gfx_missile_active(void);

/* 內建 5x7 無閃爍像素英數繪製 */
void gfx_draw_ascii(int x, int y, const char *str, int scale, uint32_t color, int shadow);

/* 精緻 UI 面板 */
void gfx_panel(int x, int y, int w, int h, uint32_t bg, uint32_t border, uint32_t header_bg, int header_h);

/* 3D 海島世界背景 */
void gfx_draw_iso_world_bg(const MapDef *m, int cam_x, int cam_y, int tick_ms);

/* 3D 立體地塊與建築繪製 */
void gfx_draw_iso_tile(int sx, int sy, const TileDef *td,
                       int owner, uint32_t owner_color, int level, CommType comm,
                       int has_roadblock, int has_mine, GodType map_god,
                       int toll, int is_cursor, int is_dest, int tick_ms);

/* 3D 立體角色精靈 (含地面分離陰影、跳躍高度、步數倒數、頭頂玩家名牌與動態指標) */
void gfx_draw_character_3d(int sx, int ground_sy, int jump_h, int char_id, int player_idx,
                           uint32_t color, int walk_phase, int vehicle_type, GodType god,
                           int facing_left, int is_active, int steps_left, int tick_ms);

void gfx_draw_god_sprite(int sx, int sy, GodType god, int tick_ms);

/* 真 3D 軟體旋轉多面體立體骰子 (3軸旋轉 + 透視投影 + 光照陰影 + 3D立體點數) */
void gfx_draw_3d_dice(int cx, int cy, int value, int rolling, int bounce_h, int tick_ms, int seed_off);

#endif
