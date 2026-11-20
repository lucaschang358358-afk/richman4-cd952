#ifndef MAPS_H
#define MAPS_H

#include <stdint.h>

#define MAX_MAP_TILES 64
#define NUM_MAPS      3

typedef enum {
    TILE_START = 0,     /* 起點 / 銀行 */
    TILE_LAND,          /* 一般小地產 (0~5級) */
    TILE_COMMERCIAL,    /* 大型商業用地 (商場/飯店/研究所/公園/加油站) */
    TILE_COUPON,        /* 點券格 */
    TILE_SHOP,          /* 卡片商店 */
    TILE_NEWS,          /* 新聞 / 命運 */
    TILE_HOSPITAL,      /* 醫院 */
    TILE_PRISON,        /* 監獄 */
    TILE_MAGIC,         /* 魔法屋 */
    TILE_MINIGAME,      /* 遊樂場 (接金幣 / 猜大小) */
    TILE_LOTTERY,       /* 樂透彩券行 */
    TILE_BANK           /* 分行銀行 */
} TileType;

typedef struct {
    int gx;
    int gy;
    const char *name;
    TileType type;
    int group;          /* 1~6 同色地段，0=特殊格 */
    int base_price;
} TileDef;

typedef struct {
    const char *name;
    const char *subtitle;
    int tile_count;
    int grid_w;
    int grid_h;
    int hospital_tile;
    int prison_tile;
    uint32_t bg_water;
    uint32_t bg_land;
    uint32_t bg_accent;
    const TileDef *tiles;
} MapDef;

const MapDef *map_get(int map_id);

#endif
