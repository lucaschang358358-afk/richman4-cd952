#include "game.h"
#include "maps.h"
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 大富翁 4 Fun — CD-952 極速流暢 3D 版 v4
 *  - 零除法 DDA 3D 光柵引擎 + 局部髒矩形刷新 (Dirty-Rect) + 靜態場景快取
 *  - 徹底消除畫面震動與文字閃爍：靜止介面不重繪、死區攝影機不微震、3D 骰子與角色局部更新
 *  - 隨時按 [ESC] 呼叫遊戲暫停選單 (繼續 / 存檔 / AI速度 / 資產 / 說明 / 主選單 / 離開)
 * ============================================================ */

#define MAX_PLAYERS    4
#define NUM_CHARS      6
#define MAX_CARDS_HELD 14
#define NUM_STOCKS     6
#define STOCK_HIST_LEN 12
#define FEED_LINES     4
#define SAVE_MAGIC     0x524D3435u /* "RM45" */

typedef enum {
    CARD_NONE = 0,
    CARD_REMOTE_DICE,
    CARD_MOTO,
    CARD_CAR,
    CARD_TURTLE,
    CARD_ROADBLOCK,
    CARD_LANDMINE,
    CARD_STAY,
    CARD_REVERSE,
    CARD_EQUAL_WEALTH,
    CARD_STEAL_CARD,
    CARD_BUY_LAND,
    CARD_MONSTER,
    CARD_SWAP_HOUSE,
    CARD_HIBERNATE,
    CARD_TAX,
    CARD_RED_STOCK,
    CARD_BLACK_STOCK,
    CARD_MISSILE,
    CARD_BANISH_GOD,
    CARD_COUNT
} CardType;

typedef enum {
    ST_TITLE = 0,
    ST_SETUP,
    ST_TURN_MENU,
    ST_PAUSE_MENU,      /* ESC 遊戲暫停選單 */
    ST_FREE_MAP,
    ST_AI_THINK,
    ST_CARD_FX,
    ST_DICE_ANIM,
    ST_WALK_ANIM,
    ST_AI_PAUSE,
    ST_BUY_LAND,
    ST_BUILD_COMM,
    ST_CARD_BAG,
    ST_CARD_TARGET,
    ST_STOCK_MARKET,
    ST_BANK_MENU,
    ST_CARD_SHOP,
    ST_MAGIC_HOUSE,
    ST_LOTTERY,
    ST_MINIGAME_SELECT,
    ST_MG_COINS,
    ST_MG_DICE,
    ST_STATUS_VIEW,
    ST_HELP_VIEW,
    ST_MESSAGE,
    ST_GAME_OVER
} GameState;

typedef enum {
    RM_NONE = 0,
    RM_FULL,            /* 全螢幕重繪 */
    RM_TITLE_DICE,      /* 僅局部更新標題旋轉 3D 骰子 */
    RM_DICE,            /* 僅局部更新場中 3D 骰子 */
    RM_WALK,            /* 僅局部更新移動中的 3D 角色 */
    RM_VFX,             /* 僅局部更新飛彈與爆炸粒子 */
    RM_COINS            /* 僅局部更新接金幣遊戲區 */
} RenderMode;

typedef struct {
    int owner;
    int level;
    CommType comm;
    int has_roadblock;
    int has_mine;
    GodType map_god;
} TileState;

typedef struct {
    char name[24];
    int char_id;
    int is_ai;
    int bankrupt;
    int pos;
    int dir;
    int cash;
    int deposit;
    int loan;
    int coupons;
    int lottery_num;
    int stocks[NUM_STOCKS];
    int cards[MAX_CARDS_HELD];
    int card_count;
    int skip_turns;
    int skip_reason;
    int turtle_turns;
    int vehicle;
    int vehicle_turns;
    int remote_dice;
    int stay_flag;
    GodType god;
    int god_turns;
} Player;

typedef struct {
    char name[24];
    int price;
    int prev_price;
    int red_buff;
    int hist[STOCK_HIST_LEN];
} Stock;

typedef struct {
    uint32_t magic;
    int map_id;
    int win_mode;
    int turn_count;
    int day;
    int month;
    int cur_player;
    int player_count;
    int human_count;
    int win_player;
    TileState tiles[MAX_MAP_TILES];
    Player players[MAX_PLAYERS];
    Stock stocks[NUM_STOCKS];
} GameData;

/* --- 靜態資料表 --- */
static const uint32_t PLAYER_COLORS[MAX_PLAYERS] = {
    0xE53935, /* P1 紅 */
    0x1E88E5, /* P2 藍 */
    0x8E24AA, /* P3 紫 */
    0xFB8C00  /* P4 橘 */
};

static const char *CHAR_NAMES[NUM_CHARS] = {
    "孫小美", "阿土伯", "錢夫人", "沙隆巴斯", "金貝貝", "忍太郎"
};
static const char *CHAR_TRAITS[NUM_CHARS] = {
    "開局現金+$2000", "開局贈購地卡", "開局贈台積100股",
    "開局點券+40", "開局福神附身3回合", "開局贈機車與飛彈"
};

static const char *CARD_NAMES[CARD_COUNT] = {
    "無", "遙控骰子", "機車卡", "汽車卡", "烏龜卡", "路障卡",
    "地雷卡", "停留卡", "轉向卡", "均富卡", "搶奪卡",
    "購地卡", "怪獸卡", "換屋卡", "冬眠卡", "查稅卡",
    "股市紅卡", "股市黑卡", "飛彈卡", "送神卡"
};

static const char *CARD_DESCS[CARD_COUNT] = {
    "",
    "指定下次3D骰子步數 (1~6)",
    "3回合內一次擲2顆3D骰子",
    "3回合內一次擲3顆3D骰子",
    "目標接下來3回合只走1步",
    "在腳下設3D路障，路過必停",
    "在腳下埋地雷，踩中炸飛住院",
    "讓自己原地再停留1次",
    "讓目標行進方向反轉",
    "平分全場所有玩家的現金",
    "隨機搶奪指定對手1張卡片",
    "原價強制收購腳下對手土地",
    "摧毀對手最高級3D大樓為Lv0",
    "將自己最低地與對手最高地交換",
    "冰凍其他所有對手2回合",
    "對對手課徵30%存款稅",
    "指定股票連2天漲停(+10%)",
    "指定股票連2天跌停(-10%)",
    "發射3D飛彈轟炸對手送醫院",
    "驅逐自己身上的衰神/窮神"
};

static const int CARD_COSTS[CARD_COUNT] = {
    0, 30, 35, 50, 25, 25, 35, 20, 20, 60, 35, 50, 45, 55, 55, 40, 35, 35, 55, 20
};

static const char *GOD_NAMES[6] = { "無", "財神", "福神", "土地公", "窮神", "衰神" };
static const char *COMM_NAMES[6] = { "空地", "商業中心", "大飯店", "研究所", "公園", "加油站" };
static const char *WIN_MODE_NAMES[3] = { "經典破產制", "資產$80000勝", "30回合決勝" };

/* --- 執行與渲染快取狀態 --- */
static GameData g;
static GameState g_state = ST_TITLE;
static GameState g_msg_next_state = ST_TURN_MENU;
static GameState g_paused_state = ST_TURN_MENU;
static GameState g_sub_return_state = ST_TURN_MENU;

static int g_tick_ms = 0;
static RenderMode g_render_mode = RM_FULL;
static int g_cache_ready = 0;
static int g_fast_ai = 0;

/* 局部髒矩形追蹤 */
static int g_prev_x = 0, g_prev_y = 0, g_prev_w = 0, g_prev_h = 0;
static int g_banner_dirty = 0;

/* 穩定的死區 3D 等角攝影機 (不微震!) */
static int g_cam_x = 0;
static int g_cam_y = 0;
static int g_free_cursor = 0;

/* 開局設定 */
static int g_setup_row = 0;
static int g_setup_map = 0;
static int g_setup_humans = 1;
static int g_setup_ais = 1;
static int g_setup_p1_char = 0;
static int g_setup_win = 0;

/* 選單與系統 */
static int g_menu_sel = 0;
static int g_pause_sel = 0;
static int g_pending_card_idx = -1;
static int g_pending_card_type = CARD_NONE;
static int g_stock_sel = 0;
static int g_shop_items[5] = { CARD_REMOTE_DICE, CARD_MOTO, CARD_LANDMINE, CARD_MISSILE, CARD_RED_STOCK };
static int g_lottery_pick = 7;

/* 3D 骰子與跳躍行走 */
static int g_dice_count = 1;
static int g_dice_vals[3] = { 1, 1, 1 };
static int g_dice_total = 1;
static int g_dice_timer = 0;
static int g_dice_total_dur = 1150;

static int g_walk_steps_left = 0;
static int g_walk_from = 0;
static int g_walk_to = 0;
static int g_walk_elapsed = 0;
static int g_walk_dur = 280;
static int g_walk_phase = 0;

/* AI 與播報系統 */
static int g_state_timer = 0;
static int g_ai_did_card = 0;
static char g_banner_text[160] = {0};
static int g_banner_active = 0;
static char g_ai_toast[160] = {0};

/* 左下角即時戰報歷史 */
static char g_feed[FEED_LINES][160];
static uint32_t g_feed_col[FEED_LINES];
static int g_feed_count = 0;

static char g_msg_title[64] = {0};
static char g_msg_l1[160] = {0};
static char g_msg_l2[160] = {0};
static char g_msg_l3[160] = {0};

/* 小遊戲 1：接金幣 */
#define MAX_FALL_ITEMS 12
typedef struct { int active, x, y, vy, type; } FallItem;
static FallItem g_mg_items[MAX_FALL_ITEMS];
static int g_mg_px = 400;
static int g_mg_score = 0;
static int g_mg_last_score = -1;
static int g_mg_last_sec = -1;
static int g_mg_time_ms = 0;
static int g_mg_spawn_ms = 0;
static int g_mg_done = 0;

/* 小遊戲 2：猜大小 */
static int g_mg_dice = 0;
static char g_mg_result[96] = {0};

/* --- PRNG --- */
static uint32_t g_rng = 0x13579BDFu;
static int rng_next(int mod) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (int)((g_rng >> 16) % (uint32_t)mod);
}

static const MapDef *cur_map(void) { return map_get(g.map_id); }

static void request_full_redraw(void) {
    g_render_mode = RM_FULL;
    g_cache_ready = 0;
}

static void request_partial(RenderMode rm) {
    if (g_render_mode != RM_FULL) {
        g_render_mode = rm;
    }
}

static void push_feed(uint32_t col, const char *text) {
    if (!text || !text[0]) return;
    for (int i = FEED_LINES - 1; i > 0; i--) {
        snprintf(g_feed[i], sizeof(g_feed[i]), "%s", g_feed[i - 1]);
        g_feed_col[i] = g_feed_col[i - 1];
    }
    snprintf(g_feed[0], sizeof(g_feed[0]), "%s", text);
    g_feed_col[0] = col;
    if (g_feed_count < FEED_LINES) g_feed_count++;
}

static void set_banner(const char *txt) {
    snprintf(g_banner_text, sizeof(g_banner_text), "%s", txt ? txt : "");
    g_banner_active = (g_banner_text[0] != '\0');
    g_banner_dirty = 1;
}

/* 3D 等角座標換算 */
static void tile_iso_center(int ti, int *wx, int *wy) {
    const MapDef *m = cur_map();
    if (ti < 0 || ti >= m->tile_count) ti = 0;
    gfx_grid_to_iso(m->tiles[ti].gx * 16, m->tiles[ti].gy * 16, wx, wy);
}

static void player_iso_pos(int pi, int *wx, int *wy, int *jump_h, int *facing_left, int *out_depth) {
    const MapDef *m = cur_map();
    if (g_state == ST_WALK_ANIM && pi == g.cur_player) {
        const TileDef *t0 = &m->tiles[g_walk_from];
        const TileDef *t1 = &m->tiles[g_walk_to];
        int t = g_walk_elapsed;
        int d = g_walk_dur > 0 ? g_walk_dur : 1;
        if (t > d) t = d;

        int gx16 = t0->gx * 16 + ((t1->gx - t0->gx) * 16 * t) / d;
        int gy16 = t0->gy * 16 + ((t1->gy - t0->gy) * 16 * t) / d;
        gfx_grid_to_iso(gx16, gy16, wx, wy);

        int x0, y0, x1, y1;
        tile_iso_center(g_walk_from, &x0, &y0);
        tile_iso_center(g_walk_to, &x1, &y1);
        *facing_left = (x1 < x0);
        *jump_h = (t * (d - t) * 96) / (d * d);
        *out_depth = (gx16 + gy16 + 8) / 16;
        return;
    }

    int ti = g.players[pi].pos;
    tile_iso_center(ti, wx, wy);
    int ox = ((pi & 1) ? 14 : -14);
    int oy = ((pi & 2) ? 6 : -6);
    *wx += ox;
    *wy += oy;
    *jump_h = 0;
    *facing_left = (g.players[pi].dir < 0);
    *out_depth = m->tiles[ti].gx + m->tiles[ti].gy;
}

/* 一次到位攝影機 (無微震拖影!) */
static void snap_camera_to_world(int wx, int wy) {
    int sw = hal_get_width();
    int sh = hal_get_height();
    g_cam_x = wx - sw / 2;
    g_cam_y = wy - (sh / 2) - 8;
    request_full_redraw();
}

static void snap_camera_to_player(int pi) {
    int wx, wy, jh, fl, dp;
    player_iso_pos(pi, &wx, &wy, &jh, &fl, &dp);
    snap_camera_to_world(wx, wy);
}

/* 檢查指定世界座標是否已超出安全可視區，只有超出時才重新置中攝影機！ */
static int ensure_visible_in_deadzone(int wx, int wy) {
    int sw = hal_get_width();
    int sh = hal_get_height();
    int sx = wx - g_cam_x;
    int sy = wy - g_cam_y;
    if (sx < 130 || sx > sw - 175 || sy < 168 || sy > sh - 125) {
        snap_camera_to_world(wx, wy);
        return 1;
    }
    return 0;
}

/* --- 經濟與規則計算 --- */
static int calc_net_worth(int pi) {
    const Player *p = &g.players[pi];
    if (p->bankrupt) return 0;
    const MapDef *m = cur_map();
    int total = p->cash + p->deposit - p->loan;
    for (int s = 0; s < NUM_STOCKS; s++) total += p->stocks[s] * g.stocks[s].price;
    for (int i = 0; i < m->tile_count; i++) {
        if (g.tiles[i].owner == pi) {
            total += m->tiles[i].base_price + g.tiles[i].level * (m->tiles[i].base_price / 2);
        }
    }
    return total;
}

static int count_group_owned(int pi, int grp) {
    if (grp <= 0) return 0;
    const MapDef *m = cur_map();
    int c = 0;
    for (int i = 0; i < m->tile_count; i++) if (m->tiles[i].group == grp && g.tiles[i].owner == pi) c++;
    return c;
}

static int calc_tile_toll(int ti) {
    const MapDef *m = cur_map();
    const TileDef *td = &m->tiles[ti];
    const TileState *ts = &g.tiles[ti];
    if (ts->owner < 0) return 0;

    if (td->type == TILE_COMMERCIAL) {
        if (ts->comm == COMM_PARK) return 0;
        if (ts->comm == COMM_MALL) return 1500 + ts->level * 650;
        if (ts->comm == COMM_HOTEL) return 1000 + ts->level * 450;
        if (ts->comm == COMM_LAB) return 650 + ts->level * 300;
        if (ts->comm == COMM_GAS) return g_dice_total * 350;
        return 400;
    }

    int base = td->base_price / 4;
    int toll = base + ts->level * (td->base_price / 3);
    int chain = count_group_owned(ts->owner, td->group);
    if (chain >= 2) toll = (toll * (10 + (chain - 1) * 2)) / 10;
    return toll;
}

static int add_card(int pi, CardType c) {
    Player *p = &g.players[pi];
    if (c <= CARD_NONE || c >= CARD_COUNT || p->card_count >= MAX_CARDS_HELD) return 0;
    p->cards[p->card_count++] = (int)c;
    return 1;
}

static void remove_card_at(int pi, int idx) {
    Player *p = &g.players[pi];
    if (idx < 0 || idx >= p->card_count) return;
    for (int i = idx; i + 1 < p->card_count; i++) p->cards[i] = p->cards[i + 1];
    p->card_count--;
}

static void show_msg(const char *title, const char *l1, const char *l2, const char *l3, GameState next_st) {
    snprintf(g_msg_title, sizeof(g_msg_title), "%s", title ? title : "");
    snprintf(g_msg_l1, sizeof(g_msg_l1), "%s", l1 ? l1 : "");
    snprintf(g_msg_l2, sizeof(g_msg_l2), "%s", l2 ? l2 : "");
    snprintf(g_msg_l3, sizeof(g_msg_l3), "%s", l3 ? l3 : "");
    g_msg_next_state = next_st;
    g_state = ST_MESSAGE;
    request_full_redraw();
}

static void pay_money(int pi, int amount, int receiver_pi) {
    Player *p = &g.players[pi];
    if (amount <= 0) return;

    int wx, wy, jh, fl, dp;
    player_iso_pos(pi, &wx, &wy, &jh, &fl, &dp);
    char pop[24];
    snprintf(pop, sizeof(pop), "-$%d", amount);
    gfx_spawn_popup(wx - 24, wy - 44, pop, 0xFF5252);

    if (p->cash >= amount) {
        p->cash -= amount;
    } else {
        int rem = amount - p->cash;
        p->cash = 0;
        if (p->deposit >= rem) {
            p->deposit -= rem;
        } else {
            rem -= p->deposit;
            p->deposit = 0;
            for (int s = 0; s < NUM_STOCKS && rem > 0; s++) {
                while (p->stocks[s] > 0 && rem > 0) {
                    int sell = p->stocks[s] >= 100 ? 100 : p->stocks[s];
                    p->stocks[s] -= sell;
                    int val = sell * g.stocks[s].price;
                    if (val >= rem) { p->cash += (val - rem); rem = 0; }
                    else rem -= val;
                }
            }
            const MapDef *m = cur_map();
            for (int i = 0; i < m->tile_count && rem > 0; i++) {
                if (g.tiles[i].owner == pi) {
                    int val = (m->tiles[i].base_price * 7) / 10 + g.tiles[i].level * (m->tiles[i].base_price / 3);
                    g.tiles[i].owner = -1;
                    g.tiles[i].level = 0;
                    g.tiles[i].comm = COMM_NONE;
                    if (val >= rem) { p->cash += (val - rem); rem = 0; }
                    else rem -= val;
                }
            }
            if (rem > 0) {
                p->bankrupt = 1;
                p->cash = 0;
                p->deposit = 0;
                gfx_spawn_explosion(wx, wy);
            }
        }
    }

    if (receiver_pi >= 0 && receiver_pi < g.player_count && !g.players[receiver_pi].bankrupt) {
        g.players[receiver_pi].cash += amount;
        int rx, ry;
        player_iso_pos(receiver_pi, &rx, &ry, &jh, &fl, &dp);
        snprintf(pop, sizeof(pop), "+$%d", amount);
        gfx_spawn_popup(rx - 24, ry - 44, pop, 0x69F0AE);
        gfx_spawn_sparkles(rx, ry, 0xFFD54F, 10);
    }
}

static int check_winner(void) {
    int alive = 0, last_pi = -1;
    int best_pi = 0, best_nw = -9999999;

    for (int i = 0; i < g.player_count; i++) {
        if (!g.players[i].bankrupt) {
            alive++;
            last_pi = i;
            int nw = calc_net_worth(i);
            if (nw > best_nw) { best_nw = nw; best_pi = i; }
            if (g.win_mode == 1 && nw >= 80000) { g.win_player = i; return 1; }
        }
    }
    if (alive <= 1) { g.win_player = last_pi; return 1; }
    if (g.win_mode == 2 && g.turn_count > 30) { g.win_player = best_pi; return 1; }
    return 0;
}

/* --- 存檔與讀檔 --- */
static int save_game(void) {
    g.magic = SAVE_MAGIC;
    FILE *f = fopen(hal_get_save_path(), "wb");
    if (!f) return 0;
    size_t n = fwrite(&g, sizeof(GameData), 1, f);
    fclose(f);
    return (n == 1);
}

static int has_save_file(void) {
    FILE *f = fopen(hal_get_save_path(), "rb");
    if (!f) return 0;
    uint32_t magic = 0;
    int ok = (fread(&magic, sizeof(magic), 1, f) == 1 && magic == SAVE_MAGIC);
    fclose(f);
    return ok;
}

static int load_game(void) {
    FILE *f = fopen(hal_get_save_path(), "rb");
    if (!f) return 0;
    GameData tmp;
    if (fread(&tmp, sizeof(GameData), 1, f) != 1 || tmp.magic != SAVE_MAGIC) {
        fclose(f);
        return 0;
    }
    fclose(f);
    g = tmp;
    gfx_vfx_reset();
    g_feed_count = 0;
    snap_camera_to_player(g.cur_player);
    g_state = g.players[g.cur_player].is_ai ? ST_AI_THINK : ST_TURN_MENU;
    g_state_timer = 650;
    g_menu_sel = 0;
    char msg[140];
    snprintf(msg, sizeof(msg), "讀取存檔！%s 第%d月%d日，輪到 P%d %s",
             cur_map()->name, g.month, g.day, g.cur_player + 1, g.players[g.cur_player].name);
    push_feed(0xFFD54F, msg);
    set_banner(msg);
    request_full_redraw();
    return 1;
}

static void spawn_map_god(void) {
    const MapDef *m = cur_map();
    int existing = 0;
    for (int i = 0; i < m->tile_count; i++) if (g.tiles[i].map_god != GOD_NONE) existing++;
    if (existing >= 2) return;
    int ti = 1 + rng_next(m->tile_count - 1);
    if (g.tiles[ti].map_god == GOD_NONE) g.tiles[ti].map_god = (GodType)(1 + rng_next(5));
}

static void start_new_game(void) {
    memset(&g, 0, sizeof(g));
    g.magic = SAVE_MAGIC;
    g.map_id = g_setup_map;
    g.win_mode = g_setup_win;
    g.day = 1;
    g.month = 1;
    g.turn_count = 1;
    g.cur_player = 0;
    g.human_count = g_setup_humans;
    g.player_count = g_setup_humans + g_setup_ais;
    if (g.player_count < 2) g.player_count = 2;
    if (g.player_count > MAX_PLAYERS) g.player_count = MAX_PLAYERS;
    g.win_player = -1;

    const MapDef *m = cur_map();
    for (int i = 0; i < MAX_MAP_TILES; i++) {
        g.tiles[i].owner = -1;
        g.tiles[i].level = 0;
        g.tiles[i].comm = COMM_NONE;
        g.tiles[i].has_roadblock = 0;
        g.tiles[i].has_mine = 0;
        g.tiles[i].map_god = GOD_NONE;
    }

    const char *snames[NUM_STOCKS] = { "台積晶圓", "長榮海運", "大宇資訊", "國泰金控", "鴻海科技", "台北地產" };
    const int sprices[NUM_STOCKS] = { 52, 28, 34, 40, 36, 48 };
    for (int s = 0; s < NUM_STOCKS; s++) {
        snprintf(g.stocks[s].name, sizeof(g.stocks[s].name), "%s", snames[s]);
        g.stocks[s].price = sprices[s];
        g.stocks[s].prev_price = sprices[s];
        for (int h = 0; h < STOCK_HIST_LEN; h++) g.stocks[s].hist[h] = sprices[s];
    }

    int used[NUM_CHARS] = {0};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g.players[i];
        if (i >= g.player_count) { p->bankrupt = 1; continue; }
        int cid = (i == 0) ? g_setup_p1_char : 0;
        if (i > 0) {
            for (int c = 0; c < NUM_CHARS; c++) { if (!used[c]) { cid = c; break; } }
        }
        used[cid] = 1;
        p->char_id = cid;
        snprintf(p->name, sizeof(p->name), "%s", CHAR_NAMES[cid]);
        p->is_ai = (i >= g.human_count) ? 1 : 0;
        p->pos = 0;
        p->dir = 1;
        p->cash = 15000;
        p->deposit = 10000;
        p->coupons = 60;

        add_card(i, CARD_REMOTE_DICE);
        add_card(i, CARD_ROADBLOCK);
        add_card(i, CARD_LANDMINE);

        if (cid == 0) p->cash += 2000;
        else if (cid == 1) add_card(i, CARD_BUY_LAND);
        else if (cid == 2) p->stocks[0] += 100;
        else if (cid == 3) p->coupons += 40;
        else if (cid == 4) { p->god = GOD_FORTUNE; p->god_turns = 3; }
        else if (cid == 5) { add_card(i, CARD_MOTO); add_card(i, CARD_MISSILE); }
    }

    spawn_map_god();
    spawn_map_god();

    gfx_vfx_reset();
    g_feed_count = 0;
    snap_camera_to_player(0);
    g_state = ST_TURN_MENU;
    g_menu_sel = 0;

    char msg[140];
    snprintf(msg, sizeof(msg), "【3D %s】%d格大冒險開始！輪到 P1 %s (按ESC暫停)",
             m->name, m->tile_count, g.players[0].name);
    push_feed(0xFFD54F, msg);
    set_banner(msg);
    request_full_redraw();
}

static void update_daily_economy(void) {
    g.day++;
    int is_lottery_day = (g.day == 15);
    int is_new_month = 0;
    if (g.day > 15) { g.day = 1; g.month++; is_new_month = 1; }

    for (int s = 0; s < NUM_STOCKS; s++) {
        Stock *st = &g.stocks[s];
        st->prev_price = st->price;
        int pct;
        if (st->red_buff > 0) { pct = 10; st->red_buff--; }
        else if (st->red_buff < 0) { pct = -10; st->red_buff++; }
        else pct = rng_next(19) - 9;

        int delta = (st->price * pct) / 100;
        if (delta == 0 && pct != 0) delta = (pct > 0) ? 1 : -1;
        st->price += delta;
        if (st->price < 8) st->price = 8;
        if (st->price > 300) st->price = 300;

        for (int h = 0; h < STOCK_HIST_LEN - 1; h++) st->hist[h] = st->hist[h + 1];
        st->hist[STOCK_HIST_LEN - 1] = st->price;
    }

    if (is_lottery_day) {
        int win_num = 1 + rng_next(20);
        int any_win = 0;
        char lb[140];
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!g.players[i].bankrupt && g.players[i].lottery_num > 0) {
                if (g.players[i].lottery_num == win_num) {
                    g.players[i].cash += 15000;
                    any_win = 1;
                    snprintf(lb, sizeof(lb), "【樂透開獎 %d號】恭喜 %s 獨得 $15000！", win_num, g.players[i].name);
                    push_feed(0xFFEA00, lb);
                }
                g.players[i].lottery_num = 0;
            }
        }
        if (!any_win) {
            snprintf(lb, sizeof(lb), "【樂透開獎】本期開出 %d 號，無人中獎。", win_num);
            push_feed(0xB0BEC5, lb);
        }
    }

    if (is_new_month) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            Player *p = &g.players[i];
            if (p->bankrupt) continue;
            p->deposit += p->deposit / 20;
            if (p->loan > 0) pay_money(i, p->loan / 10, -1);
            for (int s = 0; s < NUM_STOCKS; s++) {
                if (p->stocks[s] > 0) p->cash += (p->stocks[s] * g.stocks[s].price) / 25;
            }
        }
        push_feed(0x80CBC4, "【月初結算】銀行利息 5% 與股票股利已發放！");
    }

    const MapDef *m = cur_map();
    for (int t = 0; t < m->tile_count; t++) {
        if (g.tiles[t].owner >= 0 && g.tiles[t].comm == COMM_LAB && (g.day % 4 == 0)) {
            int ow = g.tiles[t].owner;
            if (!g.players[ow].bankrupt) add_card(ow, (CardType)(1 + rng_next(CARD_COUNT - 1)));
        }
    }
    if ((g.day % 4) == 0) spawn_map_god();
}

static void refresh_shop_items(void) {
    for (int i = 0; i < 5; i++) g_shop_items[i] = 1 + rng_next(CARD_COUNT - 1);
}

static void advance_turn(void);

/* --- 啟動 3D 骰子翻滾 --- */
static void begin_dice_roll(int pi) {
    Player *p = &g.players[pi];
    snap_camera_to_player(pi);

    if (p->stay_flag) {
        p->stay_flag = 0;
        g_dice_count = 1;
        g_dice_vals[0] = 0;
        g_dice_total = 0;
        g_walk_steps_left = 0;
        char b[140];
        snprintf(b, sizeof(b), "P%d %s 發動停留卡，留在 [%s]！", pi + 1, p->name, cur_map()->tiles[p->pos].name);
        push_feed(PLAYER_COLORS[pi], b);
        set_banner(b);
        g_state = ST_WALK_ANIM;
        g_walk_elapsed = g_walk_dur;
        request_full_redraw();
        return;
    }

    if (p->remote_dice > 0) {
        g_dice_count = 1;
        g_dice_vals[0] = p->remote_dice;
        p->remote_dice = 0;
    } else if (p->turtle_turns > 0) {
        g_dice_count = 1;
        g_dice_vals[0] = 1;
        p->turtle_turns--;
    } else if (p->vehicle == 2) {
        g_dice_count = 3;
        for (int i = 0; i < 3; i++) g_dice_vals[i] = 1 + rng_next(6);
        p->vehicle_turns--;
        if (p->vehicle_turns <= 0) p->vehicle = 0;
    } else if (p->vehicle == 1) {
        g_dice_count = 2;
        for (int i = 0; i < 2; i++) g_dice_vals[i] = 1 + rng_next(6);
        p->vehicle_turns--;
        if (p->vehicle_turns <= 0) p->vehicle = 0;
    } else {
        g_dice_count = 1;
        g_dice_vals[0] = 1 + rng_next(6);
    }

    g_dice_total = 0;
    for (int i = 0; i < g_dice_count; i++) g_dice_total += g_dice_vals[i];

    g_dice_total_dur = (p->is_ai && g_fast_ai) ? 450 : 1120;
    g_dice_timer = g_dice_total_dur;

    char b[140];
    snprintf(b, sizeof(b), "【P%d %s】正在擲 3D 立體骰子...", pi + 1, p->name);
    set_banner(b);
    g_state = ST_DICE_ANIM;
    request_full_redraw();
}

static void start_next_walk_step(int pi) {
    Player *p = &g.players[pi];
    const MapDef *m = cur_map();
    g_walk_from = p->pos;
    g_walk_to = (p->pos + p->dir + m->tile_count) % m->tile_count;
    g_walk_elapsed = 0;
    g_walk_dur = (p->is_ai && g_fast_ai) ? 120 : 280;
    g_walk_phase++;

    /* 只有目標格靠近螢幕邊緣時才重設攝影機，避免畫面一直晃動！ */
    int tx, ty;
    tile_iso_center(g_walk_to, &tx, &ty);
    ensure_visible_in_deadzone(tx, ty);

    char mb[140];
    snprintf(mb, sizeof(mb), "【P%d %s 移動中】前往「%s」 (還剩 %d 步)",
             pi + 1, p->name, m->tiles[g_walk_to].name, g_walk_steps_left);
    set_banner(mb);

    if (g_render_mode != RM_FULL) {
        request_partial(RM_WALK);
    }
}

static void finish_ai_step(const char *summary) {
    if (check_winner()) { g_state = ST_GAME_OVER; request_full_redraw(); return; }
    snprintf(g_ai_toast, sizeof(g_ai_toast), "%s", summary ? summary : "");
    if (summary && summary[0]) {
        push_feed(PLAYER_COLORS[g.cur_player], summary);
        set_banner(summary);
    }
    g_state = ST_AI_PAUSE;
    g_state_timer = g_fast_ai ? 420 : 1350;
    request_full_redraw();
}

static void handle_landing(int pi) {
    Player *p = &g.players[pi];
    const MapDef *m = cur_map();
    int pos = p->pos;
    const TileDef *td = &m->tiles[pos];
    TileState *ts = &g.tiles[pos];
    int is_ai = p->is_ai;
    int wx, wy;
    tile_iso_center(pos, &wx, &wy);
    char msg[150];

    if (td->type == TILE_START || td->type == TILE_BANK) {
        if (is_ai) {
            if (p->loan > 0 && p->cash >= p->loan + 4000) {
                p->cash -= p->loan; p->loan = 0;
                snprintf(msg, sizeof(msg), "P%d %s 在 %s 還清了全部貸款！", pi + 1, p->name, td->name);
            } else if (p->cash > 14000) {
                p->cash -= 5000; p->deposit += 5000;
                snprintf(msg, sizeof(msg), "P%d %s 在 %s 存入 $5000！", pi + 1, p->name, td->name);
            } else {
                snprintf(msg, sizeof(msg), "P%d %s 抵達 %s 辦理業務。", pi + 1, p->name, td->name);
            }
            finish_ai_step(msg);
        } else {
            snprintf(msg, sizeof(msg), "P%d %s 抵達 %s！", pi + 1, p->name, td->name);
            push_feed(PLAYER_COLORS[pi], msg);
            g_menu_sel = 0; g_state = ST_BANK_MENU; request_full_redraw();
        }
        return;
    }

    if (td->type == TILE_COUPON) {
        int gain = 30 + (pos % 3) * 10;
        p->coupons += gain;
        snprintf(msg, sizeof(msg), "P%d %s 抵達點券格，獲得 +%d 點券！", pi + 1, p->name, gain);
        if (is_ai) finish_ai_step(msg);
        else { push_feed(PLAYER_COLORS[pi], msg); show_msg("獲得點券", msg, "可在卡片商店購買強力卡片與載具！", "", ST_GAME_OVER); }
        return;
    }

    if (td->type == TILE_SHOP) {
        refresh_shop_items();
        if (is_ai) {
            int bought = 0;
            for (int i = 0; i < 5; i++) {
                int c = g_shop_items[i];
                if (p->coupons >= CARD_COSTS[c] && p->card_count < MAX_CARDS_HELD) {
                    p->coupons -= CARD_COSTS[c];
                    add_card(pi, (CardType)c);
                    snprintf(msg, sizeof(msg), "P%d %s 在卡片商店購入 [%s]！", pi + 1, p->name, CARD_NAMES[c]);
                    bought = 1; break;
                }
            }
            if (!bought) snprintf(msg, sizeof(msg), "P%d %s 逛了卡片商店沒買東西。", pi + 1, p->name);
            finish_ai_step(msg);
        } else {
            g_menu_sel = 0; g_state = ST_CARD_SHOP; request_full_redraw();
        }
        return;
    }

    if (td->type == TILE_LOTTERY) {
        if (is_ai) {
            if (p->cash >= 3000 && p->lottery_num == 0) {
                p->cash -= 500; p->lottery_num = 1 + rng_next(20);
                snprintf(msg, sizeof(msg), "P%d %s 買了一張 %d 號樂透彩券！", pi + 1, p->name, p->lottery_num);
            } else {
                snprintf(msg, sizeof(msg), "P%d %s 路過樂透彩券行。", pi + 1, p->name);
            }
            finish_ai_step(msg);
        } else {
            g_lottery_pick = 1 + rng_next(20); g_state = ST_LOTTERY; request_full_redraw();
        }
        return;
    }

    if (td->type == TILE_MINIGAME) {
        if (is_ai) {
            int gain = 15 + rng_next(35);
            p->coupons += gain;
            snprintf(msg, sizeof(msg), "P%d %s 在遊樂場贏得 +%d 點券！", pi + 1, p->name, gain);
            finish_ai_step(msg);
        } else {
            g_menu_sel = 0; g_state = ST_MINIGAME_SELECT; request_full_redraw();
        }
        return;
    }

    if (td->type == TILE_MAGIC) {
        if (is_ai) {
            int richest = pi;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!g.players[i].bankrupt && g.players[i].cash > g.players[richest].cash) richest = i;
            }
            if (richest != pi) {
                pay_money(richest, 2000, pi);
                snprintf(msg, sizeof(msg), "P%d %s 施展魔法，從 %s 吸走 $2000！", pi + 1, p->name, g.players[richest].name);
            } else {
                p->coupons += 40;
                snprintf(msg, sizeof(msg), "P%d %s 在魔法屋變出 40 點券！", pi + 1, p->name);
            }
            finish_ai_step(msg);
        } else {
            g_menu_sel = 0; g_state = ST_MAGIC_HOUSE; request_full_redraw();
        }
        return;
    }

    if (td->type == TILE_HOSPITAL || td->type == TILE_PRISON) {
        snprintf(msg, sizeof(msg), "P%d %s 路過 %s 參觀探望。", pi + 1, p->name, td->name);
        if (is_ai) finish_ai_step(msg);
        else { push_feed(PLAYER_COLORS[pi], msg); show_msg(td->name, msg, "平安無事，繼續前進！", "", ST_GAME_OVER); }
        return;
    }

    if (td->type == TILE_NEWS) {
        int ev = rng_next(8);
        char l1[128] = {0}, l2[128] = {0};
        switch (ev) {
            case 0: p->cash += 2800; snprintf(l1, sizeof(l1), "【新聞】P%d %s 中發票特獎 +$2800！", pi + 1, p->name); break;
            case 1: pay_money(pi, 1800, -1); snprintf(l1, sizeof(l1), "【新聞】徵收豪宅稅，P%d %s 繳稅 $1800！", pi + 1, p->name); break;
            case 2: {
                GodType ng = (GodType)(1 + rng_next(5));
                p->god = ng; p->god_turns = 4;
                snprintf(l1, sizeof(l1), "【神明降臨】%s 附身在 P%d %s 身上(4回合)！", GOD_NAMES[ng], pi + 1, p->name);
                break;
            }
            case 3: {
                int s = rng_next(NUM_STOCKS);
                g.stocks[s].red_buff = 2;
                snprintf(l1, sizeof(l1), "【財經】%s 接獲大單，連續 2 天漲停！", g.stocks[s].name);
                break;
            }
            case 4: {
                CardType nc = (CardType)(1 + rng_next(CARD_COUNT - 1));
                add_card(pi, nc);
                snprintf(l1, sizeof(l1), "【好康】P%d %s 撿到寶箱，獲得 [%s]！", pi + 1, p->name, CARD_NAMES[nc]);
                break;
            }
            case 5:
                p->pos = m->prison_tile; p->skip_turns = 2; p->skip_reason = 2;
                snap_camera_to_player(pi);
                snprintf(l1, sizeof(l1), "【社會】P%d %s 違規遭移送監獄 2 回合！", pi + 1, p->name);
                break;
            case 6: {
                int best = -1;
                for (int i = 0; i < m->tile_count; i++) if (g.tiles[i].owner == pi && g.tiles[i].level < 5) { best = i; break; }
                if (best >= 0) {
                    g.tiles[best].level++;
                    snprintf(l1, sizeof(l1), "【都更】P%d %s 的 [%s] 免費升級至 Lv.%d！", pi + 1, p->name, m->tiles[best].name, g.tiles[best].level);
                } else {
                    p->cash += 1500;
                    snprintf(l1, sizeof(l1), "【補助】P%d %s 獲得居住津貼 +$1500！", pi + 1, p->name);
                }
                break;
            }
            default:
                for (int i = 0; i < MAX_PLAYERS; i++) if (!g.players[i].bankrupt) g.players[i].cash += 1200;
                snprintf(l1, sizeof(l1), "【普發現金】政府發紅包，全場每人 +$1200！");
                break;
        }
        if (is_ai) finish_ai_step(l1);
        else { push_feed(0x4FC3F7, l1); show_msg("大富翁新聞台", l1, l2, "", ST_GAME_OVER); }
        return;
    }

    if (td->type == TILE_LAND || td->type == TILE_COMMERCIAL) {
        if (ts->owner >= 0 && ts->owner != pi && p->god == GOD_LAND) {
            int old_ow = ts->owner;
            ts->owner = pi;
            snprintf(msg, sizeof(msg), "土地公顯靈！P%d %s 免費強佔 %s 的 [%s]！", pi + 1, p->name, g.players[old_ow].name, td->name);
            if (is_ai) finish_ai_step(msg);
            else { push_feed(0x69F0AE, msg); show_msg("土地公附身", msg, "", "", ST_GAME_OVER); }
            return;
        }

        if (ts->owner < 0) {
            int cost = (p->god == GOD_FORTUNE) ? 0 : td->base_price;
            if (is_ai) {
                if (p->cash >= cost + 1800 || cost == 0) {
                    p->cash -= cost; ts->owner = pi; ts->level = 0;
                    if (td->type == TILE_COMMERCIAL) {
                        ts->comm = (CommType)(1 + rng_next(5));
                        snprintf(msg, sizeof(msg), "P%d %s 買下 [%s] 並蓋了 3D %s！", pi + 1, p->name, td->name, COMM_NAMES[ts->comm]);
                    } else {
                        snprintf(msg, sizeof(msg), "P%d %s 花費 $%d 買下 [%s]！", pi + 1, p->name, cost, td->name);
                    }
                } else {
                    snprintf(msg, sizeof(msg), "P%d %s 走到 [%s]，暫時放棄購買。", pi + 1, p->name, td->name);
                }
                finish_ai_step(msg);
            } else {
                if (p->cash >= cost) { g_menu_sel = 0; g_state = ST_BUY_LAND; request_full_redraw(); }
                else {
                    snprintf(msg, sizeof(msg), "[%s] 售價 $%d，現金不足。", td->name, cost);
                    push_feed(0xB0BEC5, msg);
                    show_msg("現金不足", msg, "可到股市賣股或到銀行貸款。", "", ST_GAME_OVER);
                }
            }
            return;
        }

        if (ts->owner == pi) {
            if (ts->level < 5) {
                int up_cost = (p->god == GOD_FORTUNE) ? 0 : (td->base_price / 2);
                if (is_ai) {
                    if (p->cash >= up_cost + 1200 || up_cost == 0) {
                        p->cash -= up_cost; ts->level++;
                        snprintf(msg, sizeof(msg), "P%d %s 將 [%s] 升級成 Lv.%d 3D大樓！", pi + 1, p->name, td->name, ts->level);
                    } else {
                        snprintf(msg, sizeof(msg), "P%d %s 回到自己的 [%s]。", pi + 1, p->name, td->name);
                    }
                    finish_ai_step(msg);
                } else {
                    if (p->cash >= up_cost) { g_menu_sel = 0; g_state = ST_BUY_LAND; request_full_redraw(); }
                    else {
                        snprintf(msg, sizeof(msg), "回到自己的 [%s] (現金不足升級)。", td->name);
                        push_feed(PLAYER_COLORS[pi], msg);
                        advance_turn();
                    }
                }
            } else {
                snprintf(msg, sizeof(msg), "P%d %s 巡視最高級 Lv.5 地標 [%s]！", pi + 1, p->name, td->name);
                if (is_ai) finish_ai_step(msg);
                else { push_feed(PLAYER_COLORS[pi], msg); advance_turn(); }
            }
            return;
        }

        int ow = ts->owner;
        if (td->type == TILE_COMMERCIAL && ts->comm == COMM_PARK) {
            p->coupons += 20;
            snprintf(msg, sizeof(msg), "P%d %s 逛了 %s 的公園，免費拿 20 點券！", pi + 1, p->name, g.players[ow].name);
            if (is_ai) finish_ai_step(msg);
            else { push_feed(0x69F0AE, msg); show_msg("漫步公園", msg, "", "", ST_GAME_OVER); }
            return;
        }

        if (p->god == GOD_WEALTH) {
            snprintf(msg, sizeof(msg), "財神顯靈！P%d %s 免付 %s 過路費！", pi + 1, p->name, g.players[ow].name);
            if (is_ai) finish_ai_step(msg);
            else { push_feed(0xFFEA00, msg); show_msg("財神保佑", msg, "", "", ST_GAME_OVER); }
            return;
        }

        int toll = calc_tile_toll(pos);
        if (p->god == GOD_POOR) toll *= 2;
        pay_money(pi, toll, ow);

        char l2[128] = {0};
        snprintf(msg, sizeof(msg), "P%d %s 踩到 %s 的 [%s]，付過路費 $%d！",
                 pi + 1, p->name, g.players[ow].name, td->name, toll);

        if (td->type == TILE_COMMERCIAL && ts->comm == COMM_HOTEL && !p->bankrupt) {
            p->skip_turns = 1; p->skip_reason = 4;
            snprintf(l2, sizeof(l2), "入住大飯店，下回合強制休息 1 天！");
        } else if (td->type == TILE_COMMERCIAL && ts->comm == COMM_GAS) {
            snprintf(l2, sizeof(l2), "(加油站依骰子 %d 點 x $350 計費！)", g_dice_total);
        } else if (p->bankrupt) {
            snprintf(l2, sizeof(l2), "%s 資金耗盡，宣告破產出局！！", p->name);
        } else if (p->god == GOD_POOR) {
            snprintf(l2, sizeof(l2), "(窮神附身導致過路費加倍！)");
        }

        if (check_winner()) { push_feed(0xFF5252, msg); g_state = ST_GAME_OVER; request_full_redraw(); return; }
        if (is_ai) finish_ai_step(msg);
        else { push_feed(0xFF5252, msg); show_msg("支付過路費", msg, l2, "", ST_GAME_OVER); }
        return;
    }

    (void)wx; (void)wy;
    if (is_ai) finish_ai_step("回合結束。");
    else advance_turn();
}

/* --- 卡片施放 --- */
static void execute_card(int pi, int card_idx, int target) {
    Player *p = &g.players[pi];
    const MapDef *m = cur_map();
    CardType ct = (CardType)p->cards[card_idx];
    remove_card_at(pi, card_idx);

    int px, py, jh, fl, dp;
    player_iso_pos(pi, &px, &py, &jh, &fl, &dp);

    char l1[140] = {0}, l2[128] = {0};
    int launch_vfx_wait = 0;

    switch (ct) {
        case CARD_REMOTE_DICE:
            p->remote_dice = (target >= 1 && target <= 6) ? target : 1;
            snprintf(l1, sizeof(l1), "P%d %s 使用遙控骰子，鎖定 %d 步！", pi + 1, p->name, p->remote_dice);
            break;
        case CARD_MOTO:
            p->vehicle = 1; p->vehicle_turns = 3;
            snprintf(l1, sizeof(l1), "P%d %s 騎上 3D 重機！3回合擲2顆骰子！", pi + 1, p->name);
            break;
        case CARD_CAR:
            p->vehicle = 2; p->vehicle_turns = 3;
            snprintf(l1, sizeof(l1), "P%d %s 開上 3D 跑車！3回合擲3顆骰子！", pi + 1, p->name);
            break;
        case CARD_TURTLE:
            if (target >= 0 && target < MAX_PLAYERS && !g.players[target].bankrupt) {
                g.players[target].turtle_turns = 3;
                snprintf(l1, sizeof(l1), "P%d %s 對 %s 使用烏龜卡(3回合走1步)！", pi + 1, p->name, g.players[target].name);
            }
            break;
        case CARD_ROADBLOCK:
            g.tiles[p->pos].has_roadblock = 1;
            snprintf(l1, sizeof(l1), "P%d %s 在 [%s] 放了 3D 路障！", pi + 1, p->name, m->tiles[p->pos].name);
            break;
        case CARD_LANDMINE:
            g.tiles[p->pos].has_mine = 1;
            snprintf(l1, sizeof(l1), "P%d %s 在 [%s] 埋下定時地雷！", pi + 1, p->name, m->tiles[p->pos].name);
            break;
        case CARD_STAY:
            p->stay_flag = 1;
            snprintf(l1, sizeof(l1), "P%d %s 使用停留卡，本回合停在原地！", pi + 1, p->name);
            break;
        case CARD_REVERSE:
            if (target >= 0 && target < MAX_PLAYERS && !g.players[target].bankrupt) {
                g.players[target].dir = -g.players[target].dir;
                snprintf(l1, sizeof(l1), "P%d %s 對 %s 用轉向卡，行進方向反轉！", pi + 1, p->name, g.players[target].name);
            }
            break;
        case CARD_EQUAL_WEALTH: {
            int sum = 0, cnt = 0;
            for (int i = 0; i < MAX_PLAYERS; i++) if (!g.players[i].bankrupt) { sum += g.players[i].cash; cnt++; }
            int each = (cnt > 0) ? (sum / cnt) : 0;
            for (int i = 0; i < MAX_PLAYERS; i++) if (!g.players[i].bankrupt) g.players[i].cash = each;
            snprintf(l1, sizeof(l1), "P%d %s 發動【均富卡】！每人現金變 $%d！", pi + 1, p->name, each);
            break;
        }
        case CARD_STEAL_CARD:
            if (target >= 0 && target < MAX_PLAYERS && g.players[target].card_count > 0) {
                int ridx = rng_next(g.players[target].card_count);
                CardType stolen = (CardType)g.players[target].cards[ridx];
                remove_card_at(target, ridx);
                add_card(pi, stolen);
                snprintf(l1, sizeof(l1), "P%d %s 搶走 %s 的 [%s]！", pi + 1, p->name, g.players[target].name, CARD_NAMES[stolen]);
            } else snprintf(l1, sizeof(l1), "對手沒卡片，搶奪卡落空！");
            break;
        case CARD_BUY_LAND: {
            TileState *ts = &g.tiles[p->pos];
            const TileDef *td = &m->tiles[p->pos];
            if ((td->type == TILE_LAND || td->type == TILE_COMMERCIAL) && ts->owner >= 0 && ts->owner != pi) {
                int cost = td->base_price;
                if (p->cash >= cost) {
                    int old_ow = ts->owner;
                    p->cash -= cost; g.players[old_ow].cash += cost; ts->owner = pi;
                    snprintf(l1, sizeof(l1), "P%d %s 強購 %s 的 [%s]！", pi + 1, p->name, g.players[old_ow].name, td->name);
                } else snprintf(l1, sizeof(l1), "現金不足 $%d，購地卡無效！", cost);
            } else snprintf(l1, sizeof(l1), "腳下不是對手土地，購地卡落空！");
            break;
        }
        case CARD_MONSTER: {
            int best = -1;
            for (int i = 0; i < m->tile_count; i++) {
                if (g.tiles[i].owner == target && g.tiles[i].level > 0) {
                    if (best < 0 || g.tiles[i].level > g.tiles[best].level) best = i;
                }
            }
            if (best >= 0) {
                g.tiles[best].level = 0;
                int bx, by; tile_iso_center(best, &bx, &by);
                snap_camera_to_world(bx, by);
                gfx_spawn_explosion(bx, by);
                launch_vfx_wait = 1;
                snprintf(l1, sizeof(l1), "大怪獸！%s 的 [%s] 被夷為平地！", g.players[target].name, m->tiles[best].name);
            } else snprintf(l1, sizeof(l1), "%s 沒有可摧毀的建築！", g.players[target].name);
            break;
        }
        case CARD_SWAP_HOUSE: {
            int my_t = -1, en_t = -1;
            for (int i = 0; i < m->tile_count; i++) {
                if (g.tiles[i].owner == pi) { if (my_t < 0 || g.tiles[i].level < g.tiles[my_t].level) my_t = i; }
                else if (g.tiles[i].owner == target) { if (en_t < 0 || g.tiles[i].level > g.tiles[en_t].level) en_t = i; }
            }
            if (my_t >= 0 && en_t >= 0) {
                g.tiles[my_t].owner = target; g.tiles[en_t].owner = pi;
                snprintf(l1, sizeof(l1), "換屋！用 [%s] 換到 %s 的 [%s]！", m->tiles[my_t].name, g.players[target].name, m->tiles[en_t].name);
            } else snprintf(l1, sizeof(l1), "雙方需各有地產才能換屋！");
            break;
        }
        case CARD_HIBERNATE:
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (i != pi && !g.players[i].bankrupt) {
                    g.players[i].skip_turns = 2; g.players[i].skip_reason = 3;
                }
            }
            gfx_spawn_ice(px, py);
            snprintf(l1, sizeof(l1), "P%d %s 發動冬眠卡！所有對手冰凍 2 回合！", pi + 1, p->name);
            break;
        case CARD_TAX:
            if (target >= 0 && target < MAX_PLAYERS && !g.players[target].bankrupt) {
                int tax = (g.players[target].deposit * 3) / 10;
                g.players[target].deposit -= tax;
                snprintf(l1, sizeof(l1), "P%d %s 對 %s 查稅，沒收 30%% 存款 $%d！", pi + 1, p->name, g.players[target].name, tax);
            }
            break;
        case CARD_RED_STOCK:
            if (target >= 0 && target < NUM_STOCKS) {
                g.stocks[target].red_buff = 2;
                snprintf(l1, sizeof(l1), "P%d %s 對 [%s] 用紅卡！連漲 2 天！", pi + 1, p->name, g.stocks[target].name);
            }
            break;
        case CARD_BLACK_STOCK:
            if (target >= 0 && target < NUM_STOCKS) {
                g.stocks[target].red_buff = -2;
                snprintf(l1, sizeof(l1), "P%d %s 對 [%s] 用黑卡！連跌 2 天！", pi + 1, p->name, g.stocks[target].name);
            }
            break;
        case CARD_MISSILE:
            if (target >= 0 && target < MAX_PLAYERS && !g.players[target].bankrupt) {
                int tx, ty; player_iso_pos(target, &tx, &ty, &jh, &fl, &dp);
                snap_camera_to_world((px + tx) / 2, (py + ty) / 2);
                gfx_launch_missile(px, py - 18, tx, ty - 12);
                g.players[target].pos = m->hospital_tile;
                g.players[target].skip_turns = 3;
                g.players[target].skip_reason = 1;
                launch_vfx_wait = 1;
                snprintf(l1, sizeof(l1), "轟隆！P%d %s 發射飛彈炸飛 %s 送醫 3 回合！", pi + 1, p->name, g.players[target].name);
            }
            break;
        case CARD_BANISH_GOD:
            p->god = GOD_NONE; p->god_turns = 0;
            snprintf(l1, sizeof(l1), "P%d %s 使用送神卡送走神明！", pi + 1, p->name);
            break;
        default: break;
    }

    push_feed(PLAYER_COLORS[pi], l1);
    set_banner(l1);

    if (launch_vfx_wait) {
        g_state = ST_CARD_FX;
        g_state_timer = 950;
        g_prev_w = 0;
        request_full_redraw();
        return;
    }
    if (p->is_ai) {
        g_state = ST_CARD_FX;
        g_state_timer = g_fast_ai ? 320 : 800;
        request_full_redraw();
    } else {
        show_msg("卡片發動", l1, l2, "", ST_TURN_MENU);
    }
}

static void advance_turn(void) {
    if (check_winner()) { g_state = ST_GAME_OVER; request_full_redraw(); return; }

    for (int tries = 0; tries < MAX_PLAYERS * 2; tries++) {
        int prev = g.cur_player;
        g.cur_player = (g.cur_player + 1) % g.player_count;
        if (g.cur_player <= prev) {
            g.turn_count++;
            update_daily_economy();
        }

        Player *p = &g.players[g.cur_player];
        if (p->bankrupt) continue;

        if (p->god == GOD_BADLUCK) {
            int drop = 500 + rng_next(500);
            pay_money(g.cur_player, drop, -1);
            if (check_winner()) { g_state = ST_GAME_OVER; request_full_redraw(); return; }
        }
        if (p->god != GOD_NONE) {
            p->god_turns--;
            if (p->god_turns <= 0) p->god = GOD_NONE;
        }

        snap_camera_to_player(g.cur_player);

        if (p->skip_turns > 0) {
            p->skip_turns--;
            const char *reasons[5] = { "", "住院休養", "監獄服刑", "冰凍冬眠", "飯店休息" };
            char sb[140];
            snprintf(sb, sizeof(sb), "P%d %s %s中，本回合暫停！", g.cur_player + 1, p->name, reasons[p->skip_reason]);
            if (p->is_ai) {
                finish_ai_step(sb);
            } else {
                push_feed(PLAYER_COLORS[g.cur_player], sb);
                show_msg("暫停回合", sb, "按確認繼續下一位玩家。", "", ST_GAME_OVER);
            }
            return;
        }

        if (p->is_ai) {
            g_ai_did_card = 0;
            char b[140];
            snprintf(b, sizeof(b), "【輪到 P%d %s [AI]】準備行動... (空白加速 / ESC暫停)", g.cur_player + 1, p->name);
            set_banner(b);
            g_state = ST_AI_THINK;
            g_state_timer = g_fast_ai ? 220 : 620;
            request_full_redraw();
            return;
        } else {
            char b[140];
            snprintf(b, sizeof(b), "【輪到你了！P%d %s】按 Enter 擲骰 (按 ESC 暫停選單)", g.cur_player + 1, p->name);
            push_feed(PLAYER_COLORS[g.cur_player], b);
            set_banner(b);
            g_state = ST_TURN_MENU;
            g_menu_sel = 0;
            request_full_redraw();
            return;
        }
    }
}

static void start_coin_minigame(void) {
    memset(g_mg_items, 0, sizeof(g_mg_items));
    g_mg_px = hal_get_width() / 2;
    g_mg_score = 0;
    g_mg_last_score = -1;
    g_mg_last_sec = -1;
    g_mg_time_ms = 14000;
    g_mg_spawn_ms = 0;
    g_mg_done = 0;
    g_state = ST_MG_COINS;
    request_full_redraw();
}

static void open_pause_menu(void) {
    g_paused_state = g_state;
    g_pause_sel = 0;
    g_state = ST_PAUSE_MENU;
    request_full_redraw();
}

/* ============================================================
 * 每幀推進 (僅在真正動畫進行時局部刷新，靜止時完全零重繪零閃爍!)
 * ============================================================ */
void game_tick(int dt_ms) {
    g_tick_ms += dt_ms;

    /* 暫停選單開啟時完全凍結所有動畫與計時器！ */
    if (g_state == ST_PAUSE_MENU) return;

    /* 標題畫面：只局部旋轉中間 96x108 的 3D 骰子，零閃爍零負擔！ */
    if (g_state == ST_TITLE) {
        if (g_cache_ready && ((g_tick_ms / 75) != ((g_tick_ms - dt_ms) / 75))) {
            request_partial(RM_TITLE_DICE);
        }
        return;
    }

    /* AI 思考階段：靜態等待，絕不無意義重繪整張畫面！ */
    if (g_state == ST_AI_THINK) {
        g_state_timer -= dt_ms;
        if (g_state_timer <= 0) {
            Player *p = &g.players[g.cur_player];
            if (p->cash > 16000 && rng_next(3) == 0) {
                int s = rng_next(NUM_STOCKS);
                int cost = g.stocks[s].price * 100;
                if (p->cash > cost + 8000) {
                    p->cash -= cost; p->stocks[s] += 100;
                    char sb[128];
                    snprintf(sb, sizeof(sb), "P%d %s 買進 100 股 [%s]！", g.cur_player + 1, p->name, g.stocks[s].name);
                    push_feed(PLAYER_COLORS[g.cur_player], sb);
                }
            }
            if (!g_ai_did_card && p->card_count > 0 && rng_next(3) == 0) {
                g_ai_did_card = 1;
                for (int i = 0; i < p->card_count; i++) {
                    CardType ct = (CardType)p->cards[i];
                    if (ct == CARD_BANISH_GOD && (p->god == GOD_POOR || p->god == GOD_BADLUCK)) {
                        execute_card(g.cur_player, i, g.cur_player); return;
                    }
                    if (ct == CARD_MOTO || ct == CARD_CAR || ct == CARD_ROADBLOCK || ct == CARD_LANDMINE) {
                        execute_card(g.cur_player, i, g.cur_player); return;
                    }
                    if (ct == CARD_MISSILE || ct == CARD_TAX || ct == CARD_TURTLE || ct == CARD_MONSTER) {
                        int tgt = -1;
                        for (int j = 0; j < MAX_PLAYERS; j++) {
                            if (j != g.cur_player && !g.players[j].bankrupt) { tgt = j; break; }
                        }
                        if (tgt >= 0) { execute_card(g.cur_player, i, tgt); return; }
                    }
                }
            }
            begin_dice_roll(g.cur_player);
        }
        return;
    }

    /* 卡片與飛彈特效：局部更新 VFX 區域 */
    if (g_state == ST_CARD_FX) {
        if (gfx_vfx_is_active()) {
            gfx_vfx_tick(dt_ms);
            request_partial(RM_VFX);
        }
        g_state_timer -= dt_ms;
        if (g_state_timer <= 0 && !gfx_missile_active()) {
            snap_camera_to_player(g.cur_player);
            if (g.players[g.cur_player].is_ai) begin_dice_roll(g.cur_player);
            else { g_state = ST_TURN_MENU; request_full_redraw(); }
        }
        return;
    }

    /* 3D 骰子翻滾：只局部更新中間骰子框！超流暢不閃爍！ */
    if (g_state == ST_DICE_ANIM) {
        int prev_t = g_dice_timer;
        g_dice_timer -= dt_ms;
        int reveal_ms = (g.players[g.cur_player].is_ai && g_fast_ai) ? 150 : 420;

        if (prev_t > reveal_ms && g_dice_timer <= reveal_ms) {
            Player *p = &g.players[g.cur_player];
            char db[140];
            snprintf(db, sizeof(db), "【P%d %s】擲出 %d 點！準備前進！", g.cur_player + 1, p->name, g_dice_total);
            push_feed(PLAYER_COLORS[g.cur_player], db);
            set_banner(db);
        }

        if (g_dice_timer <= 0) {
            g_walk_steps_left = g_dice_total;
            g_state = ST_WALK_ANIM;
            if (g_walk_steps_left > 0) {
                start_next_walk_step(g.cur_player);
                request_full_redraw(); /* 進入行走先建立不含移動角色的背景快取 */
            } else {
                handle_landing(g.cur_player);
            }
        } else {
            request_partial(RM_DICE);
        }
        return;
    }

    /* 3D 角色逐格跳躍：只局部更新角色移動的髒矩形！ */
    if (g_state == ST_WALK_ANIM) {
        Player *p = &g.players[g.cur_player];
        const MapDef *m = cur_map();
        g_walk_elapsed += dt_ms;

        if (g_walk_elapsed >= g_walk_dur) {
            p->pos = g_walk_to;
            g_walk_steps_left--;

            if (p->pos == 0) {
                p->cash += 2000;
                char pb[128];
                snprintf(pb, sizeof(pb), "P%d %s 經過起點，領取薪水 +$2000！", g.cur_player + 1, p->name);
                push_feed(0xFFEA00, pb);
            }

            if (g.tiles[p->pos].map_god != GOD_NONE) {
                p->god = g.tiles[p->pos].map_god;
                p->god_turns = 5;
                g.tiles[p->pos].map_god = GOD_NONE;
                char gb[128];
                snprintf(gb, sizeof(gb), "P%d %s 巧遇【%s】附身 5 回合！", g.cur_player + 1, p->name, GOD_NAMES[p->god]);
                push_feed(0xFFF176, gb);
                set_banner(gb);
                request_full_redraw();
            }

            if (g.tiles[p->pos].has_mine) {
                g.tiles[p->pos].has_mine = 0;
                g_walk_steps_left = 0;
                p->pos = m->hospital_tile; p->skip_turns = 2; p->skip_reason = 1;
                snap_camera_to_player(g.cur_player);
                char mb[140];
                snprintf(mb, sizeof(mb), "轟隆！P%d %s 踩中地雷炸進醫院 2 回合！", g.cur_player + 1, p->name);
                if (p->is_ai) finish_ai_step(mb);
                else { push_feed(0xFF1744, mb); show_msg("踩中地雷！！", mb, "", "", ST_GAME_OVER); }
                return;
            }

            if (g.tiles[p->pos].has_roadblock) {
                g.tiles[p->pos].has_roadblock = 0;
                g_walk_steps_left = 0;
                char rb[140];
                snprintf(rb, sizeof(rb), "P%d %s 撞到 3D 路障，停在 [%s]！", g.cur_player + 1, p->name, m->tiles[p->pos].name);
                push_feed(0xFF9100, rb);
                set_banner(rb);
                handle_landing(g.cur_player);
                return;
            }

            if (g_walk_steps_left > 0) {
                start_next_walk_step(g.cur_player);
            } else {
                handle_landing(g.cur_player);
            }
        } else {
            request_partial(RM_WALK);
        }
        return;
    }

    /* AI 報告停頓：完全靜止不重繪不閃爍！ */
    if (g_state == ST_AI_PAUSE) {
        g_state_timer -= dt_ms;
        if (g_state_timer <= 0) advance_turn();
        return;
    }

    /* 即時接金幣小遊戲：只局部刷新遊玩區 */
    if (g_state == ST_MG_COINS && !g_mg_done) {
        int sw = hal_get_width(), sh = hal_get_height();
        g_mg_time_ms -= dt_ms;
        g_mg_spawn_ms -= dt_ms;
        if (g_mg_spawn_ms <= 0) {
            g_mg_spawn_ms = 340;
            for (int i = 0; i < MAX_FALL_ITEMS; i++) {
                if (!g_mg_items[i].active) {
                    g_mg_items[i].active = 1;
                    g_mg_items[i].x = 70 + rng_next(sw - 140);
                    g_mg_items[i].y = 56;
                    g_mg_items[i].vy = 6 + rng_next(5);
                    int r = rng_next(10);
                    g_mg_items[i].type = (r < 6) ? 0 : ((r < 8) ? 1 : 2);
                    break;
                }
            }
        }
        int py = sh - 84;
        for (int i = 0; i < MAX_FALL_ITEMS; i++) {
            if (!g_mg_items[i].active) continue;
            g_mg_items[i].y += g_mg_items[i].vy;
            if (abs(g_mg_items[i].x - g_mg_px) < 30 && abs(g_mg_items[i].y - py) < 28) {
                if (g_mg_items[i].type == 0) g_mg_score += 5;
                else if (g_mg_items[i].type == 1) g_mg_score += 15;
                else { g_mg_score -= 15; if (g_mg_score < 0) g_mg_score = 0; }
                g_mg_items[i].active = 0;
            } else if (g_mg_items[i].y > sh - 54) g_mg_items[i].active = 0;
        }
        if (g_mg_time_ms <= 0) {
            g_mg_time_ms = 0; g_mg_done = 1;
            g.players[g.cur_player].coupons += g_mg_score;
            char cb[128];
            snprintf(cb, sizeof(cb), "P%d %s 在接金幣大作戰賺得 +%d 點券！", g.cur_player + 1, g.players[g.cur_player].name, g_mg_score);
            push_feed(0xFFEA00, cb);
            request_full_redraw();
        } else {
            request_partial(RM_COINS);
        }
        return;
    }
}

int game_needs_redraw(void) { return (g_render_mode != RM_NONE); }

void game_init(void) {
    g_rng = hal_get_seed();
    if (g_rng == 0) g_rng = 0x5A3C9E11u;
    g_state = ST_TITLE;
    g_menu_sel = 0;
    request_full_redraw();
}

void game_shutdown(void) {
    if (g_state != ST_TITLE && g_state != ST_SETUP && g_state != ST_GAME_OVER) save_game();
}

/* ============================================================
 * 按鍵事件處理 (含全域 ESC 暫停選單!)
 * ============================================================ */
void game_on_key(GameKey key) {
    g_rng ^= (uint32_t)key * 2654435761u;
    rng_next(7);
    int is_confirm = (key == GKEY_OK || key == GKEY_SPACE);

    /* 1. ESC 暫停選單內部控制 */
    if (g_state == ST_PAUSE_MENU) {
        if (key == GKEY_BACK) {
            g_state = g_paused_state;
            request_full_redraw();
            return;
        }
        if (key == GKEY_UP || key == GKEY_LEFT) { g_pause_sel = (g_pause_sel + 6) % 7; request_full_redraw(); return; }
        if (key == GKEY_DOWN || key == GKEY_RIGHT) { g_pause_sel = (g_pause_sel + 1) % 7; request_full_redraw(); return; }
        if (is_confirm) {
            switch (g_pause_sel) {
                case 0: /* 繼續遊戲 */
                    g_state = g_paused_state;
                    request_full_redraw();
                    break;
                case 1: { /* 立即存檔 */
                    int ok = save_game();
                    show_msg("存檔結果", ok ? "遊戲進度已儲存至 C:\\richman4_3d.sav！" : "存檔失敗！", "", "", ST_PAUSE_MENU);
                    break;
                }
                case 2: /* 切換 AI 速度 */
                    g_fast_ai = !g_fast_ai;
                    request_full_redraw();
                    break;
                case 3: /* 全體資產總覽 */
                    g_sub_return_state = ST_PAUSE_MENU;
                    g_state = ST_STATUS_VIEW;
                    request_full_redraw();
                    break;
                case 4: /* 操作說明 */
                    g_sub_return_state = ST_PAUSE_MENU;
                    g_state = ST_HELP_VIEW;
                    request_full_redraw();
                    break;
                case 5: /* 返回主標題 */
                    save_game();
                    g_state = ST_TITLE;
                    g_menu_sel = 0;
                    request_full_redraw();
                    break;
                case 6: /* 離開遊戲 */
                    save_game();
                    hal_request_quit();
                    break;
            }
        }
        return;
    }

    /* 2. 在任何進行中的主畫面 / 動畫 / AI 回合按 ESC -> 立刻暫停並開啟暫停選單！ */
    if (key == GKEY_BACK) {
        if (g_state == ST_TURN_MENU || g_state == ST_AI_THINK || g_state == ST_AI_PAUSE ||
            g_state == ST_DICE_ANIM || g_state == ST_WALK_ANIM || g_state == ST_CARD_FX) {
            open_pause_menu();
            return;
        }
    }

    /* AI 停頓/行動中按空白鍵快速跳過或加速 */
    if (g_state == ST_AI_PAUSE) {
        if (is_confirm) advance_turn();
        return;
    }
    if (g_state == ST_AI_THINK ||
        ((g_state == ST_DICE_ANIM || g_state == ST_WALK_ANIM) && g.players[g.cur_player].is_ai)) {
        if (key == GKEY_SPACE || key == GKEY_OK) {
            g_fast_ai = !g_fast_ai;
            set_banner(g_fast_ai ? "AI 高速模式：開啟 (按空白回原速 / ESC暫停)" : "AI 高速模式：關閉");
            request_full_redraw();
        }
        return;
    }

    if (g_state == ST_TITLE) {
        int has_sav = has_save_file();
        int max_opt = has_sav ? 3 : 2;
        if (key == GKEY_UP || key == GKEY_LEFT) { g_menu_sel = (g_menu_sel + max_opt - 1) % max_opt; request_full_redraw(); }
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) { g_menu_sel = (g_menu_sel + 1) % max_opt; request_full_redraw(); }
        else if (is_confirm) {
            if (has_sav && g_menu_sel == 0) load_game();
            else if ((has_sav && g_menu_sel == 1) || (!has_sav && g_menu_sel == 0)) { g_setup_row = 0; g_state = ST_SETUP; request_full_redraw(); }
            else hal_request_quit();
        } else if (key == GKEY_BACK) hal_request_quit();
        return;
    }

    if (g_state == ST_SETUP) {
        if (key == GKEY_UP) g_setup_row = (g_setup_row + 5) % 6;
        else if (key == GKEY_DOWN) g_setup_row = (g_setup_row + 1) % 6;
        else if (key == GKEY_LEFT) {
            if (g_setup_row == 0) g_setup_map = (g_setup_map + NUM_MAPS - 1) % NUM_MAPS;
            else if (g_setup_row == 1 && g_setup_humans > 1) {
                g_setup_humans--;
                if (g_setup_humans + g_setup_ais < 2) g_setup_ais = 2 - g_setup_humans;
            } else if (g_setup_row == 2 && g_setup_ais > 0) {
                if (g_setup_humans + g_setup_ais > 2) g_setup_ais--;
            } else if (g_setup_row == 3) g_setup_p1_char = (g_setup_p1_char + NUM_CHARS - 1) % NUM_CHARS;
            else if (g_setup_row == 4) g_setup_win = (g_setup_win + 2) % 3;
        } else if (key == GKEY_RIGHT) {
            if (g_setup_row == 0) g_setup_map = (g_setup_map + 1) % NUM_MAPS;
            else if (g_setup_row == 1 && g_setup_humans < 4) {
                g_setup_humans++;
                if (g_setup_humans + g_setup_ais > 4) g_setup_ais = 4 - g_setup_humans;
            } else if (g_setup_row == 2 && g_setup_humans + g_setup_ais < 4) {
                g_setup_ais++;
            } else if (g_setup_row == 3) g_setup_p1_char = (g_setup_p1_char + 1) % NUM_CHARS;
            else if (g_setup_row == 4) g_setup_win = (g_setup_win + 1) % 3;
        } else if (is_confirm) {
            if (g_setup_row == 5 || key == GKEY_OK) { start_new_game(); return; }
        } else if (key == GKEY_BACK) { g_state = ST_TITLE; g_menu_sel = 0; }
        request_full_redraw();
        return;
    }

    if (g_state == ST_MESSAGE) {
        if (is_confirm || key == GKEY_BACK) {
            if (g_msg_next_state == ST_GAME_OVER) advance_turn();
            else { g_state = g_msg_next_state; request_full_redraw(); }
        }
        return;
    }

    if (g_state == ST_GAME_OVER) {
        if (is_confirm || key == GKEY_BACK) { g_state = ST_TITLE; g_menu_sel = 0; request_full_redraw(); }
        return;
    }

    if (g_state == ST_STATUS_VIEW || g_state == ST_HELP_VIEW) {
        if (is_confirm || key == GKEY_BACK) { g_state = g_sub_return_state; request_full_redraw(); }
        return;
    }

    if (g_state == ST_FREE_MAP) {
        const MapDef *m = cur_map();
        if (key == GKEY_RIGHT || key == GKEY_DOWN) g_free_cursor = (g_free_cursor + 1) % m->tile_count;
        else if (key == GKEY_LEFT || key == GKEY_UP) g_free_cursor = (g_free_cursor + m->tile_count - 1) % m->tile_count;
        else if (key == GKEY_PGDN) g_free_cursor = (g_free_cursor + 5) % m->tile_count;
        else if (key == GKEY_PGUP) g_free_cursor = (g_free_cursor + m->tile_count - 5) % m->tile_count;
        else if (key == GKEY_BACK || key == GKEY_MAP || is_confirm) {
            snap_camera_to_player(g.cur_player);
            g_state = ST_TURN_MENU;
            return;
        }
        int wx, wy; tile_iso_center(g_free_cursor, &wx, &wy);
        snap_camera_to_world(wx, wy);
        return;
    }

    if (g_state == ST_TURN_MENU) {
        if (key == GKEY_CARD) { g_menu_sel = 0; g_state = ST_CARD_BAG; request_full_redraw(); return; }
        if (key == GKEY_STOCK) { g_stock_sel = 0; g_state = ST_STOCK_MARKET; request_full_redraw(); return; }
        if (key == GKEY_MAP) { g_free_cursor = g.players[g.cur_player].pos; g_state = ST_FREE_MAP; request_full_redraw(); return; }
        if (key == GKEY_HELP) { g_sub_return_state = ST_TURN_MENU; g_state = ST_HELP_VIEW; request_full_redraw(); return; }
        if (key == GKEY_SAVE) {
            int ok = save_game();
            show_msg("存檔結果", ok ? "3D 進度已儲存至 C:\\richman4_3d.sav！" : "存檔失敗！", "", "", ST_TURN_MENU);
            return;
        }

        if (key == GKEY_LEFT || key == GKEY_UP) { g_menu_sel = (g_menu_sel + 6) % 7; request_full_redraw(); }
        else if (key == GKEY_RIGHT || key == GKEY_DOWN) { g_menu_sel = (g_menu_sel + 1) % 7; request_full_redraw(); }
        else if (is_confirm) {
            switch (g_menu_sel) {
                case 0: begin_dice_roll(g.cur_player); break;
                case 1: g_menu_sel = 0; g_state = ST_CARD_BAG; request_full_redraw(); break;
                case 2: g_stock_sel = 0; g_state = ST_STOCK_MARKET; request_full_redraw(); break;
                case 3: g_free_cursor = g.players[g.cur_player].pos; g_state = ST_FREE_MAP; request_full_redraw(); break;
                case 4: g_sub_return_state = ST_TURN_MENU; g_state = ST_STATUS_VIEW; request_full_redraw(); break;
                case 5: {
                    int ok = save_game();
                    show_msg("存檔結果", ok ? "3D 進度已儲存至 C:\\richman4_3d.sav！" : "存檔失敗！", "", "", ST_TURN_MENU);
                    break;
                }
                case 6: open_pause_menu(); break;
            }
        }
        return;
    }

    if (g_state == ST_BUY_LAND) {
        Player *p = &g.players[g.cur_player];
        const MapDef *m = cur_map();
        const TileDef *td = &m->tiles[p->pos];
        TileState *ts = &g.tiles[p->pos];

        if (key == GKEY_LEFT || key == GKEY_RIGHT || key == GKEY_UP || key == GKEY_DOWN) { g_menu_sel = 1 - g_menu_sel; request_full_redraw(); }
        else if (is_confirm) {
            if (g_menu_sel == 0) {
                char fb[128];
                if (ts->owner < 0) {
                    int cost = (p->god == GOD_FORTUNE) ? 0 : td->base_price;
                    p->cash -= cost; ts->owner = g.cur_player; ts->level = 0;
                    if (td->type == TILE_COMMERCIAL) { g_menu_sel = 0; g_state = ST_BUILD_COMM; request_full_redraw(); return; }
                    snprintf(fb, sizeof(fb), "P%d %s 買下了 [%s]！", g.cur_player + 1, p->name, td->name);
                    push_feed(PLAYER_COLORS[g.cur_player], fb);
                } else {
                    int cost = (p->god == GOD_FORTUNE) ? 0 : (td->base_price / 2);
                    p->cash -= cost; ts->level++;
                    snprintf(fb, sizeof(fb), "P%d %s 將 [%s] 升級成 Lv.%d 3D大樓！", g.cur_player + 1, p->name, td->name, ts->level);
                    push_feed(PLAYER_COLORS[g.cur_player], fb);
                }
            }
            advance_turn();
        } else if (key == GKEY_BACK) advance_turn();
        return;
    }

    if (g_state == ST_BUILD_COMM) {
        Player *p = &g.players[g.cur_player];
        TileState *ts = &g.tiles[p->pos];
        if (key == GKEY_UP || key == GKEY_LEFT) { g_menu_sel = (g_menu_sel + 4) % 5; request_full_redraw(); }
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) { g_menu_sel = (g_menu_sel + 1) % 5; request_full_redraw(); }
        else if (is_confirm) {
            ts->comm = (CommType)(g_menu_sel + 1);
            char fb[128];
            snprintf(fb, sizeof(fb), "P%d %s 興建了 3D [%s]！", g.cur_player + 1, p->name, COMM_NAMES[ts->comm]);
            push_feed(PLAYER_COLORS[g.cur_player], fb);
            advance_turn();
        }
        return;
    }

    if (g_state == ST_CARD_BAG) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_BACK || key == GKEY_CARD) { g_state = ST_TURN_MENU; g_menu_sel = 0; request_full_redraw(); return; }
        if (p->card_count == 0) return;
        if (key == GKEY_UP || key == GKEY_LEFT) { g_menu_sel = (g_menu_sel + p->card_count - 1) % p->card_count; request_full_redraw(); }
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) { g_menu_sel = (g_menu_sel + 1) % p->card_count; request_full_redraw(); }
        else if (is_confirm) {
            CardType ct = (CardType)p->cards[g_menu_sel];
            if (ct == CARD_MOTO || ct == CARD_CAR || ct == CARD_ROADBLOCK || ct == CARD_LANDMINE ||
                ct == CARD_STAY || ct == CARD_EQUAL_WEALTH || ct == CARD_BUY_LAND ||
                ct == CARD_HIBERNATE || ct == CARD_BANISH_GOD) {
                execute_card(g.cur_player, g_menu_sel, g.cur_player);
            } else {
                g_pending_card_idx = g_menu_sel; g_pending_card_type = (int)ct;
                g_menu_sel = 0; g_state = ST_CARD_TARGET; request_full_redraw();
            }
        }
        return;
    }

    if (g_state == ST_CARD_TARGET) {
        CardType ct = (CardType)g_pending_card_type;
        int max_t = (ct == CARD_REMOTE_DICE) ? 6 : ((ct == CARD_RED_STOCK || ct == CARD_BLACK_STOCK) ? NUM_STOCKS : g.player_count);
        if (key == GKEY_BACK) { g_state = ST_CARD_BAG; g_menu_sel = g_pending_card_idx; request_full_redraw(); return; }
        if (key == GKEY_UP || key == GKEY_LEFT) { g_menu_sel = (g_menu_sel + max_t - 1) % max_t; request_full_redraw(); }
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) { g_menu_sel = (g_menu_sel + 1) % max_t; request_full_redraw(); }
        else if (key >= GKEY_1 && key <= GKEY_6) {
            int num = (int)(key - GKEY_1);
            if (num < max_t) execute_card(g.cur_player, g_pending_card_idx, (ct == CARD_REMOTE_DICE) ? (num + 1) : num);
        } else if (is_confirm) {
            execute_card(g.cur_player, g_pending_card_idx, (ct == CARD_REMOTE_DICE) ? (g_menu_sel + 1) : g_menu_sel);
        }
        return;
    }

    if (g_state == ST_STOCK_MARKET) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_BACK || key == GKEY_STOCK) { g_state = ST_TURN_MENU; g_menu_sel = 0; request_full_redraw(); return; }
        if (key == GKEY_UP) g_stock_sel = (g_stock_sel + NUM_STOCKS - 1) % NUM_STOCKS;
        else if (key == GKEY_DOWN) g_stock_sel = (g_stock_sel + 1) % NUM_STOCKS;
        else if (key == GKEY_RIGHT || is_confirm || key == GKEY_PGDN) {
            int lot = (key == GKEY_PGDN) ? 500 : 100;
            int cost = g.stocks[g_stock_sel].price * lot;
            if (p->cash >= cost) { p->cash -= cost; p->stocks[g_stock_sel] += lot; }
            else if (p->deposit >= cost) { p->deposit -= cost; p->stocks[g_stock_sel] += lot; }
        } else if (key == GKEY_LEFT || key == GKEY_PGUP) {
            int lot = (key == GKEY_PGUP) ? 500 : 100;
            if (p->stocks[g_stock_sel] >= lot) { p->stocks[g_stock_sel] -= lot; p->cash += g.stocks[g_stock_sel].price * lot; }
        }
        request_full_redraw();
        return;
    }

    if (g_state == ST_BANK_MENU) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_UP || key == GKEY_LEFT) { g_menu_sel = (g_menu_sel + 5) % 6; request_full_redraw(); }
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) { g_menu_sel = (g_menu_sel + 1) % 6; request_full_redraw(); }
        else if (is_confirm) {
            if (g_menu_sel == 0 && p->cash >= 2000) { p->cash -= 2000; p->deposit += 2000; request_full_redraw(); }
            else if (g_menu_sel == 1 && p->deposit >= 2000) { p->deposit -= 2000; p->cash += 2000; request_full_redraw(); }
            else if (g_menu_sel == 2 && p->cash >= 5000) { p->cash -= 5000; p->deposit += 5000; request_full_redraw(); }
            else if (g_menu_sel == 3 && p->loan == 0) { p->loan = 10000; p->cash += 10000; request_full_redraw(); }
            else if (g_menu_sel == 4 && p->loan > 0 && p->cash >= p->loan) { p->cash -= p->loan; p->loan = 0; request_full_redraw(); }
            else if (g_menu_sel == 5) advance_turn();
        } else if (key == GKEY_BACK) advance_turn();
        return;
    }

    if (g_state == ST_CARD_SHOP) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_UP || key == GKEY_LEFT) { g_menu_sel = (g_menu_sel + 5) % 6; request_full_redraw(); }
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) { g_menu_sel = (g_menu_sel + 1) % 6; request_full_redraw(); }
        else if (is_confirm) {
            if (g_menu_sel == 5) advance_turn();
            else {
                int c = g_shop_items[g_menu_sel];
                if (p->coupons >= CARD_COSTS[c] && p->card_count < MAX_CARDS_HELD) {
                    p->coupons -= CARD_COSTS[c]; add_card(g.cur_player, (CardType)c);
                    char fb[128]; snprintf(fb, sizeof(fb), "P%d %s 購入卡片 [%s]！", g.cur_player + 1, p->name, CARD_NAMES[c]);
                    push_feed(PLAYER_COLORS[g.cur_player], fb);
                    request_full_redraw();
                }
            }
        } else if (key == GKEY_BACK) advance_turn();
        return;
    }

    if (g_state == ST_MAGIC_HOUSE) {
        Player *p = &g.players[g.cur_player];
        const MapDef *m = cur_map();
        if (key == GKEY_UP || key == GKEY_LEFT) { g_menu_sel = (g_menu_sel + 2) % 3; request_full_redraw(); }
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) { g_menu_sel = (g_menu_sel + 1) % 3; request_full_redraw(); }
        else if (is_confirm) {
            if (g_menu_sel == 0) {
                for (int i = 0; i < MAX_PLAYERS; i++) if (i != g.cur_player && !g.players[i].bankrupt) pay_money(i, 1500, g.cur_player);
                push_feed(0xCE93D8, "魔法屋：施展吸金術！向每位對手吸取 $1500！");
                show_msg("魔法生效", "施展吸金術！向每位對手吸取 $1500！", "", "", ST_GAME_OVER);
            } else if (g_menu_sel == 1) {
                for (int i = 0; i < MAX_PLAYERS; i++) if (i != g.cur_player && !g.players[i].bankrupt) {
                    g.players[i].pos = m->hospital_tile; g.players[i].skip_turns = 1; g.players[i].skip_reason = 1;
                }
                push_feed(0xCE93D8, "魔法屋：全體對手感冒送醫 1 回合！");
                show_msg("魔法生效", "施展感冒魔法！全體對手送醫 1 回合！", "", "", ST_GAME_OVER);
            } else {
                p->coupons += 50;
                push_feed(0xCE93D8, "魔法屋：點石成券獲得 +50 點券！");
                show_msg("魔法生效", "點石成券！免費獲得 50 點券！", "", "", ST_GAME_OVER);
            }
        } else if (key == GKEY_BACK) advance_turn();
        return;
    }

    if (g_state == ST_LOTTERY) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_LEFT || key == GKEY_UP) { g_lottery_pick = (g_lottery_pick <= 1) ? 20 : (g_lottery_pick - 1); request_full_redraw(); }
        else if (key == GKEY_RIGHT || key == GKEY_DOWN) { g_lottery_pick = (g_lottery_pick >= 20) ? 1 : (g_lottery_pick + 1); request_full_redraw(); }
        else if (is_confirm) {
            if (p->cash >= 500) {
                p->cash -= 500; p->lottery_num = g_lottery_pick;
                char fb[128]; snprintf(fb, sizeof(fb), "P%d %s 買了 %d 號樂透彩券！", g.cur_player + 1, p->name, g_lottery_pick);
                push_feed(0xFFD54F, fb);
            }
            advance_turn();
        } else if (key == GKEY_BACK) advance_turn();
        return;
    }

    if (g_state == ST_MINIGAME_SELECT) {
        if (key == GKEY_UP || key == GKEY_LEFT) { g_menu_sel = (g_menu_sel + 2) % 3; request_full_redraw(); }
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) { g_menu_sel = (g_menu_sel + 1) % 3; request_full_redraw(); }
        else if (is_confirm) {
            if (g_menu_sel == 0) start_coin_minigame();
            else if (g_menu_sel == 1) { g_mg_dice = 0; g_mg_result[0] = '\0'; g_menu_sel = 0; g_state = ST_MG_DICE; request_full_redraw(); }
            else advance_turn();
        } else if (key == GKEY_BACK) advance_turn();
        return;
    }

    if (g_state == ST_MG_COINS) {
        int sw = hal_get_width();
        if (g_mg_done) { if (is_confirm || key == GKEY_BACK) advance_turn(); return; }
        if (key == GKEY_LEFT) { g_mg_px -= 40; if (g_mg_px < 60) g_mg_px = 60; request_partial(RM_COINS); }
        else if (key == GKEY_RIGHT) { g_mg_px += 40; if (g_mg_px > sw - 60) g_mg_px = sw - 60; request_partial(RM_COINS); }
        else if (key == GKEY_BACK) { g_mg_done = 1; advance_turn(); }
        return;
    }

    if (g_state == ST_MG_DICE) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_BACK) { advance_turn(); return; }
        if (g_mg_dice > 0) { if (is_confirm) advance_turn(); return; }
        if (key == GKEY_LEFT || key == GKEY_UP) { g_menu_sel = (g_menu_sel + 2) % 3; request_full_redraw(); }
        else if (key == GKEY_RIGHT || key == GKEY_DOWN) { g_menu_sel = (g_menu_sel + 1) % 3; request_full_redraw(); }
        else if (is_confirm) {
            if (g_menu_sel == 2) { advance_turn(); return; }
            g_mg_dice = 1 + rng_next(6);
            int is_big = (g_mg_dice >= 4);
            if (is_big == (g_menu_sel == 0)) {
                p->coupons += 40;
                snprintf(g_mg_result, sizeof(g_mg_result), "3D骰開出 %d 點 (%s)！猜對 +40 點券！", g_mg_dice, is_big ? "大" : "小");
            } else {
                p->coupons += 10;
                snprintf(g_mg_result, sizeof(g_mg_result), "3D骰開出 %d 點 (%s)！安慰獎 +10 點券。", g_mg_dice, is_big ? "大" : "小");
            }
            push_feed(0xFFEA00, g_mg_result);
            request_full_redraw();
        }
        return;
    }
}

/* ============================================================
 * 繪圖輔助 (支援全螢幕重繪與超高速局部髒矩形更新)
 * ============================================================ */
static void draw_banner_box(int sw) {
    if (!g_banner_active || !g_banner_text[0]) return;
    int bw = sw - 180;
    if (bw > 500) bw = 500;
    int bx = 12;
    gfx_panel(bx, 40, bw, 32, 0x162432, PLAYER_COLORS[g.cur_player], 0, 0);
    hal_fill_rect(bx + 4, 44, 8, 24, PLAYER_COLORS[g.cur_player]);
    hal_set_text_bg(0x162432);
    hal_draw_text(bx + 18, 47, g_banner_text, 0xFFEA00);
}


/* ============================================================
 * 手機級全畫面觸控點擊支援 (game_on_touch)
 * ============================================================ */
void game_on_touch(int tx, int ty) {
    int sw = hal_get_width(), sh = hal_get_height();
    if (tx < 0 || ty < 0 || tx >= sw || ty >= sh) return;

    /* 1. 標題封面 */
    if (g_state == ST_TITLE) {
        int pw = 500, px = (sw - pw) / 2, py = 28;
        int has_sav = has_save_file();
        int n = has_sav ? 3 : 2;
        for (int i = 0; i < n; i++) {
            int by = py + 190 + i * 48;
            if (tx >= px + 90 && tx <= px + 410 && ty >= by && ty <= by + 38) {
                g_menu_sel = i;
                game_on_key(GKEY_OK);
                return;
            }
        }
        return;
    }

    /* 2. 開局設定 (點左半減、點右半加、點最後一列立即出發!) */
    if (g_state == ST_SETUP) {
        int pw = 540, px = (sw - pw) / 2, py = 28;
        for (int i = 0; i < 6; i++) {
            int ry = py + 54 + i * 48;
            if (tx >= px + 24 && tx <= px + pw - 24 && ty >= ry && ty <= ry + 38) {
                g_setup_row = i;
                if (i == 5) game_on_key(GKEY_OK);
                else if (tx < sw / 2) game_on_key(GKEY_LEFT);
                else game_on_key(GKEY_RIGHT);
                return;
            }
        }
        return;
    }

    if (g_state == ST_MESSAGE || g_state == ST_GAME_OVER || g_state == ST_STATUS_VIEW || g_state == ST_HELP_VIEW || g_state == ST_AI_PAUSE) {
        game_on_key(GKEY_OK);
        return;
    }

    /* 3. ESC 暫停選單 */
    if (g_state == ST_PAUSE_MENU) {
        int pw = 360, ph = 330, px = (sw - pw) / 2, py = (sh - ph) / 2;
        if (tx < px || tx > px + pw || ty < py || ty > py + ph) { game_on_key(GKEY_BACK); return; }
        if (ty >= py + 52) {
            int idx = (ty - (py + 52)) / 38;
            if (idx >= 0 && idx < 7) { g_pause_sel = idx; game_on_key(GKEY_OK); }
        }
        return;
    }

    /* 4. 主回合畫面 (手機版右下大擲骰鍵 + 6顆功能圓角鍵) */
    if (g_state == ST_TURN_MENU) {
        int roll_w = 126, roll_h = 54;
        int roll_x = sw - roll_w - 14, roll_y = sh - roll_h - 12;
        if (tx >= roll_x && tx <= roll_x + roll_w && ty >= roll_y && ty <= roll_y + roll_h) {
            g_menu_sel = 0;
            game_on_key(GKEY_OK);
            return;
        }
        int bw = 74, bh = 38, gap = 8;
        int start_x = roll_x - 6 * (bw + gap) - 4;
        int by = sh - bh - 14;
        if (ty >= by && ty <= by + bh) {
            for (int i = 0; i < 6; i++) {
                int bx = start_x + i * (bw + gap);
                if (tx >= bx && tx <= bx + bw) {
                    g_menu_sel = i + 1;
                    game_on_key(GKEY_OK);
                    return;
                }
            }
        }
        /* 點右上小地圖直接進入自由看全地圖 */
        if (tx >= sw - 140 && ty >= 36 && ty <= 130) {
            game_on_key(GKEY_MAP);
        }
        return;
    }

    /* 5. 買地 / 升級 */
    if (g_state == ST_BUY_LAND) {
        int ph = 180, py = (sh - ph) / 2;
        if (ty >= py + 116 && ty <= py + 168) {
            g_menu_sel = (tx < sw / 2) ? 0 : 1;
            game_on_key(GKEY_OK);
        }
        return;
    }

    /* 6. 商業建築 */
    if (g_state == ST_BUILD_COMM) {
        int ph = 260, py = (sh - ph) / 2;
        if (ty >= py + 48) {
            int idx = (ty - (py + 48)) / 38;
            if (idx >= 0 && idx < 5) { g_menu_sel = idx; game_on_key(GKEY_OK); }
        }
        return;
    }

    /* 7. 卡片袋 / 銀行 / 卡片商店 / 魔法屋 / 小遊戲選單 */
    if (g_state == ST_CARD_BAG) {
        int pw = 560, ph = 290, px = (sw - pw) / 2, py = (sh - ph) / 2;
        if (tx < px || tx > px + pw || ty < py || ty > py + ph) { game_on_key(GKEY_BACK); return; }
        Player *cp = &g.players[g.cur_player];
        int start = (g_menu_sel >= 6) ? (g_menu_sel - 5) : 0;
        int row = (ty - (py + 46)) / 37;
        if (row >= 0 && start + row < cp->card_count) { g_menu_sel = start + row; game_on_key(GKEY_OK); }
        return;
    }
    if (g_state == ST_BANK_MENU || g_state == ST_CARD_SHOP) {
        int pw = (g_state == ST_BANK_MENU) ? 460 : 560, ph = 290, px = (sw - pw) / 2, py = (sh - ph) / 2;
        if (tx < px || tx > px + pw || ty < py || ty > py + ph) { game_on_key(GKEY_BACK); return; }
        int row = (ty - (py + 46)) / 37;
        if (row >= 0 && row < 6) { g_menu_sel = row; game_on_key(GKEY_OK); }
        return;
    }
    if (g_state == ST_MAGIC_HOUSE || g_state == ST_MINIGAME_SELECT) {
        int pw = 480, ph = 220, px = (sw - pw) / 2, py = (sh - ph) / 2;
        if (tx < px || tx > px + pw || ty < py || ty > py + ph) { game_on_key(GKEY_BACK); return; }
        int row = (ty - (py + 54)) / 48;
        if (row >= 0 && row < 3) { g_menu_sel = row; game_on_key(GKEY_OK); }
        return;
    }
    if (g_state == ST_STOCK_MARKET) {
        int pw = sw - 60, ph = sh - 86, px = 30, py = 44;
        if (tx < px || tx > px + pw || ty < py || ty > py + ph) { game_on_key(GKEY_BACK); return; }
        int row = (ty - (py + 46)) / 46;
        if (row >= 0 && row < NUM_STOCKS) {
            g_stock_sel = row;
            if (tx > px + pw / 2) game_on_key(GKEY_RIGHT); /* 點右買 */
            else game_on_key(GKEY_LEFT);                   /* 點左賣 */
        }
        return;
    }
    if (g_state == ST_FREE_MAP) {
        if (tx < sw / 3) game_on_key(GKEY_LEFT);
        else if (tx > (sw * 2) / 3) game_on_key(GKEY_RIGHT);
        else game_on_key(GKEY_BACK);
        return;
    }
    if (g_state == ST_MG_COINS) {
        if (g_mg_done) { game_on_key(GKEY_OK); return; }
        g_mg_px = tx;
        if (g_mg_px < 60) g_mg_px = 60;
        if (g_mg_px > sw - 60) g_mg_px = sw - 60;
        request_partial(RM_COINS);
        return;
    }
    if (g_state == ST_MG_DICE) {
        if (g_mg_dice > 0) { game_on_key(GKEY_OK); return; }
        if (tx < sw / 3) { g_menu_sel = 0; game_on_key(GKEY_OK); }
        else if (tx < (sw * 2) / 3) { g_menu_sel = 1; game_on_key(GKEY_OK); }
        else { g_menu_sel = 2; game_on_key(GKEY_OK); }
    }
}

static void render_minimap(int sw) {
    const MapDef *m = cur_map();
    int mw = m->grid_w * 7 + 8;
    int mh = m->grid_h * 6 + 8;
    int mx = sw - mw - 10;
    int my = 38;

    gfx_panel(mx, my, mw, mh, 0x0B1520, 0xFFD54F, 0, 0);
    for (int i = 0; i < m->tile_count; i++) {
        int tx = mx + 4 + m->tiles[i].gx * 7;
        int ty = my + 4 + m->tiles[i].gy * 6;
        uint32_t c = (g.tiles[i].owner >= 0) ? PLAYER_COLORS[g.tiles[i].owner] : 0xB0BEC5;
        if (g_state == ST_FREE_MAP && i == g_free_cursor) c = 0xFFEA00;
        hal_fill_rect(tx, ty, 5, 4, c);
    }
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (g.players[p].bankrupt) continue;
        int ti = g.players[p].pos;
        int tx = mx + 4 + m->tiles[ti].gx * 7;
        int ty = my + 4 + m->tiles[ti].gy * 6;
        hal_fill_rect(tx - 1, ty - 1, 7, 6, 0xFFFFFF);
        hal_fill_rect(tx, ty, 5, 4, PLAYER_COLORS[p]);
    }
}

/* skip_player: 若 >= 0 則先不畫該玩家 (供行走局部刷新建立純淨背景快取) */
static void render_world_scene(int sw, int sh, int skip_player) {
    const MapDef *m = cur_map();
    int cx = g_cam_x, cy = g_cam_y;

    gfx_draw_iso_world_bg(m, cx, cy);

    int pwx[MAX_PLAYERS], pwy[MAX_PLAYERS], pjh[MAX_PLAYERS], pfl[MAX_PLAYERS], pdp[MAX_PLAYERS];
    for (int pi = 0; pi < MAX_PLAYERS; pi++) {
        if (!g.players[pi].bankrupt && pi != skip_player) {
            player_iso_pos(pi, &pwx[pi], &pwy[pi], &pjh[pi], &pfl[pi], &pdp[pi]);
        } else {
            pdp[pi] = -999;
        }
    }

    int max_d = m->grid_w + m->grid_h;
    for (int d = 0; d <= max_d; d++) {
        for (int i = 0; i < m->tile_count; i++) {
            const TileDef *td = &m->tiles[i];
            if (td->gx + td->gy != d) continue;
            const TileState *ts = &g.tiles[i];
            int wx, wy;
            tile_iso_center(i, &wx, &wy);
            uint32_t ocol = (ts->owner >= 0) ? PLAYER_COLORS[ts->owner] : 0x607D8B;
            int cursor = (g_state == ST_FREE_MAP && i == g_free_cursor) ||
                         (g_state != ST_FREE_MAP && i == g.players[g.cur_player].pos);
            int is_dest = 0;
            gfx_draw_iso_tile(wx - cx, wy - cy, td, ts->owner, ocol, ts->level, ts->comm,
                              ts->has_roadblock, ts->has_mine, ts->map_god,
                              calc_tile_toll(i), cursor, is_dest, 1);
        }

        for (int pass = 0; pass < 2; pass++) {
            for (int pi = 0; pi < MAX_PLAYERS; pi++) {
                if (g.players[pi].bankrupt || pi == skip_player || pdp[pi] != d) continue;
                int is_cur = (pi == g.cur_player);
                if ((pass == 0 && is_cur) || (pass == 1 && !is_cur)) continue;
                gfx_draw_character_3d(pwx[pi] - cx, pwy[pi] - cy, pjh[pi],
                                      g.players[pi].char_id, pi, PLAYER_COLORS[pi],
                                      0, g.players[pi].vehicle, g.players[pi].god,
                                      pfl[pi], is_cur, 0);
            }
        }
    }

    /* 頂部 HUD */
    Player *cp = &g.players[g.cur_player];
    hal_fill_rect(0, 0, sw, 32, 0x0E1924);
    hal_fill_rect(0, 32, sw, 2, PLAYER_COLORS[g.cur_player]);
    hal_fill_rect(6, 4, 14, 24, PLAYER_COLORS[g.cur_player]);
    hal_set_text_bg(0x0E1924);
    char hdr[180];
    snprintf(hdr, sizeof(hdr), "第%d月%d日 | P%d %s%s 現:$%d 存:$%d 券:%d 神:%s [ESC暫停]",
             g.month, g.day, g.cur_player + 1, cp->name, cp->is_ai ? "[AI]" : "",
             cp->cash, cp->deposit, cp->coupons, GOD_NAMES[cp->god]);
    hal_draw_text(26, 7, hdr, 0xFFFFFF);

    render_minimap(sw);
    draw_banner_box(sw);
    g_banner_dirty = 0;

    int show_feed = (g_state == ST_TURN_MENU || g_state == ST_AI_THINK || g_state == ST_DICE_ANIM ||
                     g_state == ST_WALK_ANIM || g_state == ST_AI_PAUSE || g_state == ST_CARD_FX ||
                     g_state == ST_PAUSE_MENU);
    if (show_feed && g_feed_count > 0) {
        int fw = 420;
        if (fw > sw - 24) fw = sw - 24;
        int lines = g_feed_count > 3 ? 3 : g_feed_count;
        int fh = lines * 20 + 12;
        int fy = (g_state == ST_TURN_MENU) ? (sh - 86 - fh) : (sh - 12 - fh);
        gfx_panel(10, fy, fw, fh, 0x0E1822, 0x455A64, 0, 0);
        hal_set_text_bg(0x0E1822);
        for (int i = 0; i < lines; i++) {
            int row_y = fy + 6 + (lines - 1 - i) * 20;
            hal_fill_rect(16, row_y + 3, 6, 12, g_feed_col[i]);
            hal_draw_text(26, row_y, g_feed[i], (i == 0) ? 0xFFF176 : 0xB0BEC5);
        }
    }
}

static void draw_dice_overlay(int sw, int sh) {
    Player *cp = &g.players[g.cur_player];
    int reveal_ms = (cp->is_ai && g_fast_ai) ? 150 : 420;
    int rolling = (g_dice_timer > reveal_ms);
    int elapsed = g_dice_total_dur - g_dice_timer;
    int roll_span = g_dice_total_dur - reveal_ms;
    if (roll_span < 1) roll_span = 1;

    int bounce_h = 0;
    if (rolling) {
        bounce_h = (elapsed * (roll_span - elapsed) * 140) / (roll_span * roll_span) + 8;
    } else {
        int rem = g_dice_timer;
        bounce_h = (rem > reveal_ms / 2) ? ((rem - reveal_ms / 2) / 14) : 0;
    }

    int start_x = sw / 2 - (g_dice_count - 1) * 52;
    for (int i = 0; i < g_dice_count; i++) {
        gfx_draw_3d_dice(start_x + i * 104, sh / 2 - 4, g_dice_vals[i], rolling, bounce_h, g_tick_ms, i + 1);
    }
    if (!rolling) {
        char tb[24];
        snprintf(tb, sizeof(tb), "GO %d!", g_dice_total);
        gfx_draw_ascii(sw / 2 - 42, sh / 2 + 48, tb, 3, 0xFFEA00, 1);
    }
}

static uint32_t tile_top_color(TileType t) {
    if (t == TILE_START || t == TILE_BANK) return 0xFFF3CD;
    if (t == TILE_COMMERCIAL) return 0xFCE4EC;
    if (t == TILE_NEWS) return 0xE3F2FD;
    if (t == TILE_SHOP) return 0xE8F5E9;
    if (t == TILE_MAGIC) return 0xF3E5F5;
    if (t == TILE_MINIGAME || t == TILE_LOTTERY) return 0xFFF8E1;
    if (t == TILE_HOSPITAL || t == TILE_PRISON) return 0xECEFF1;
    return 0xF5EFE0;
}

/* 只更新移動中的角色髒矩形 (超高速 + 零文字閃爍!) */
static void render_walk_partial(int sw, int sh) {
    int pi = g.cur_player;
    int wx, wy, jh, fl, dp;
    player_iso_pos(pi, &wx, &wy, &jh, &fl, &dp);
    int sx = wx - g_cam_x;
    int sy = wy - g_cam_y;

    /* 角色含跳躍、步數氣泡與陰影的精準包圍盒 */
    int cx0 = sx - 38;
    int cy0 = sy - jh - 92;
    int cw  = 84;
    int ch  = jh + 114;

    /* 合併上一幀與這一幀的區域，一次清除殘影並畫上新位置 */
    int ux0 = cx0, uy0 = cy0, ux1 = cx0 + cw, uy1 = cy0 + ch;
    if (g_prev_w > 0) {
        if (g_prev_x < ux0) ux0 = g_prev_x;
        if (g_prev_y < uy0) uy0 = g_prev_y;
        if (g_prev_x + g_prev_w > ux1) ux1 = g_prev_x + g_prev_w;
        if (g_prev_y + g_prev_h > uy1) uy1 = g_prev_y + g_prev_h;
    }
    if (uy0 < 78) uy0 = 78;
    if (ux0 < 0) ux0 = 0;
    if (ux1 > sw) ux1 = sw;
    if (uy1 > sh) uy1 = sh;

    int uw = ux1 - ux0;
    int uh = uy1 - uy0;

    hal_begin_partial();
    if (uw > 0 && uh > 0) {
        hal_restore_bg_rect(ux0, uy0, uw, uh);
        /* 僅補畫與角色局部區域重疊的 1~2 格地名，其餘畫面文字零重繪零閃爍 */
        const MapDef *m = cur_map();
        for (int i = 0; i < m->tile_count; i++) {
            int tx, ty;
            tile_iso_center(i, &tx, &ty);
            int tsx = tx - g_cam_x, tsy = ty - g_cam_y;
            if (tsx + 34 >= ux0 && tsx - 34 <= ux1 && tsy + 22 >= uy0 && tsy - 4 <= uy1) {
                if (tsx > 28 && tsx < sw - 54 && tsy > 44 && tsy < sh - 66) {
                    hal_set_text_bg(tile_top_color(m->tiles[i].type));
                    hal_draw_text(tsx - 26, tsy + 2, m->tiles[i].name, 0x1A1A1A);
                }
            }
        }
        gfx_draw_character_3d(sx, sy, jh, g.players[pi].char_id, pi, PLAYER_COLORS[pi],
                              g_walk_phase, g.players[pi].vehicle, g.players[pi].god,
                              fl, 1, g_walk_steps_left);
        hal_present_rect(ux0, uy0, uw, uh);
    }

    /* 若本步換了新目的地，單獨刷新上方播報欄 (完全不影響地圖) */
    if (g_banner_dirty) {
        int bw = sw - 180; if (bw > 500) bw = 500;
        hal_begin_partial();
        draw_banner_box(sw);
        hal_present_rect(12, 40, bw + 6, 38);
        g_banner_dirty = 0;
    }

    g_prev_x = cx0; g_prev_y = cy0; g_prev_w = cw; g_prev_h = ch;
}

static void render_coins_playfield(int sw, int sh) {
    hal_begin_partial();
    hal_restore_bg_rect(0, 40, sw, sh - 40);

    for (int i = 0; i < MAX_FALL_ITEMS; i++) {
        if (!g_mg_items[i].active) continue;
        int ix = g_mg_items[i].x, iy = g_mg_items[i].y;
        if (g_mg_items[i].type == 0) {
            hal_fill_circle(ix, iy, 10, 0xFFB300); hal_draw_circle(ix, iy, 10, 0xFFF176);
            gfx_draw_ascii(ix - 3, iy - 3, "$", 1, 0xFFFFFF, 0);
        } else if (g_mg_items[i].type == 1) {
            hal_fill_circle(ix, iy, 10, 0x00E5FF); hal_draw_circle(ix, iy, 10, 0xFFFFFF);
            gfx_draw_ascii(ix - 3, iy - 3, "*", 1, 0xFFFFFF, 0);
        } else {
            hal_fill_circle(ix, iy, 11, 0x212121); hal_draw_circle(ix, iy, 11, 0xFF1744);
            hal_fill_circle(ix, iy - 12, 3, 0xFF9100);
        }
    }
    Player *cp = &g.players[g.cur_player];
    gfx_draw_character_3d(g_mg_px, sh - 74, 0, cp->char_id, g.cur_player, PLAYER_COLORS[g.cur_player],
                          (g_tick_ms / 120) & 1, 0, GOD_NONE, 0, 1, 0);
    hal_present_rect(0, 40, sw, sh - 40);

    /* 只有秒數或分數變動時才更新頂部文字條 */
    int sec = (g_mg_time_ms + 999) / 1000;
    if (sec != g_mg_last_sec || g_mg_score != g_mg_last_score) {
        g_mg_last_sec = sec;
        g_mg_last_score = g_mg_score;
        hal_begin_partial();
        hal_fill_rect(0, 0, sw, 38, 0x0D1B2A);
        hal_set_text_bg(0x0D1B2A);
        char mh[128];
        snprintf(mh, sizeof(mh), "【接金幣大作戰】 剩餘:%d秒  已獲點券:%d  (←/→ 移動躲炸彈!)", sec, g_mg_score);
        hal_draw_text(16, 10, mh, 0xFFD54F);
        hal_present_rect(0, 0, sw, 38);
    }
}

void game_render(void) {
    int sw = hal_get_width();
    int sh = hal_get_height();
    RenderMode mode = g_render_mode;
    g_render_mode = RM_NONE;
    if (mode == RM_NONE) return;

    /* === 超高速局部更新路徑 (零文字閃爍、10倍流暢度!) === */
    if (mode == RM_TITLE_DICE && g_state == ST_TITLE && g_cache_ready) {
        int pw = 500, px = (sw - pw) / 2, py = 28;
        int dcx = px + pw / 2, dcy = py + 106;
        hal_begin_partial();
        hal_restore_bg_rect(dcx - 46, dcy - 48, 92, 96);
        gfx_draw_3d_dice(dcx, dcy, 6, 1, 6, g_tick_ms, 1);
        hal_present_rect(dcx - 46, dcy - 48, 92, 96);
        return;
    }

    if (mode == RM_DICE && g_state == ST_DICE_ANIM && g_cache_ready) {
        int dw = g_dice_count * 108 + 44;
        int dh = 196;
        int dx = (sw - dw) / 2;
        int dy = (sh / 2) - 108;
        hal_begin_partial();
        hal_restore_bg_rect(dx, dy, dw, dh);
        draw_dice_overlay(sw, sh);
        hal_present_rect(dx, dy, dw, dh);
        if (g_banner_dirty) {
            int bw = sw - 180; if (bw > 500) bw = 500;
            hal_begin_partial();
            draw_banner_box(sw);
            hal_present_rect(12, 40, bw + 6, 38);
            g_banner_dirty = 0;
        }
        return;
    }

    if (mode == RM_WALK && g_state == ST_WALK_ANIM && g_cache_ready) {
        render_walk_partial(sw, sh);
        return;
    }

    if (mode == RM_VFX && g_state == ST_CARD_FX && g_cache_ready) {
        int bx, by, bw, bh;
        int has_v = gfx_vfx_get_bounds(g_cam_x, g_cam_y, &bx, &by, &bw, &bh);
        int ux0 = has_v ? bx : g_prev_x;
        int uy0 = has_v ? by : g_prev_y;
        int ux1 = has_v ? (bx + bw) : (g_prev_x + g_prev_w);
        int uy1 = has_v ? (by + bh) : (g_prev_y + g_prev_h);
        if (g_prev_w > 0 && has_v) {
            if (g_prev_x < ux0) ux0 = g_prev_x;
            if (g_prev_y < uy0) uy0 = g_prev_y;
            if (g_prev_x + g_prev_w > ux1) ux1 = g_prev_x + g_prev_w;
            if (g_prev_y + g_prev_h > uy1) uy1 = g_prev_y + g_prev_h;
        }
        if (uy0 < 78) uy0 = 78;
        if (ux0 < 0) ux0 = 0;
        if (ux1 > sw) ux1 = sw;
        if (uy1 > sh) uy1 = sh;
        if (ux1 > ux0 && uy1 > uy0) {
            hal_begin_partial();
            hal_restore_bg_rect(ux0, uy0, ux1 - ux0, uy1 - uy0);
            gfx_vfx_render(g_cam_x, g_cam_y);
            hal_present_rect(ux0, uy0, ux1 - ux0, uy1 - uy0);
        }
        if (has_v) { g_prev_x = bx; g_prev_y = by; g_prev_w = bw; g_prev_h = bh; }
        else g_prev_w = 0;
        return;
    }

    if (mode == RM_COINS && g_state == ST_MG_COINS && g_cache_ready) {
        render_coins_playfield(sw, sh);
        return;
    }

    /* === 完整畫面重繪路徑 (切狀態 / 開選單 / 換鏡頭時執行) === */
    if (g_state == ST_TITLE) {
        hal_clear(0x0B1929);
        int pw = 500, ph = sh - 56, px = (sw - pw) / 2, py = 28;
        gfx_panel(px, py, pw, ph, 0x162536, 0xFFD54F, 0x0D47A1, 52);
        hal_set_text_bg(0x0D47A1);
        hal_draw_text(px + 84, py + 15, "大富翁 4 Fun — CD-952 流暢 3D 版", 0xFFD54F);

        gfx_draw_character_3d(px + 74,       py + 128, 0, 0, 0, PLAYER_COLORS[0], 0, 0, GOD_WEALTH, 0, 0, 0);
        gfx_draw_character_3d(px + 150,      py + 128, 0, 1, 1, PLAYER_COLORS[1], 1, 1, GOD_NONE,   0, 0, 0);
        gfx_draw_character_3d(px + pw - 150, py + 128, 0, 2, 2, PLAYER_COLORS[2], 0, 0, GOD_NONE,   1, 0, 0);
        gfx_draw_character_3d(px + pw - 74,  py + 128, 0, 3, 3, PLAYER_COLORS[3], 1, 2, GOD_NONE,   1, 0, 0);

        hal_set_text_bg(0x162536);
        hal_draw_text(px + 48, py + 156, "零震動極速3D引擎 / ESC暫停選單 / 3大環島地圖", 0x80CBC4);

        int has_sav = has_save_file();
        const char *o1[3] = { "繼續上次進度 (讀取存檔)", "開始全新 3D 大富翁冒險", "離開遊戲" };
        const char *o2[2] = { "開始全新 3D 大富翁冒險", "離開遊戲" };
        int n = has_sav ? 3 : 2;
        for (int i = 0; i < n; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x2E7D32 : 0x243648;
            hal_fill_rect(px + 90, py + 190 + i * 48, 320, 38, bg);
            hal_draw_rect(px + 90, py + 190 + i * 48, 320, 38, (i == g_menu_sel) ? 0xFFEA00 : 0x546E7A);
            hal_set_text_bg(bg);
            hal_draw_text(px + 122, py + 200 + i * 48, has_sav ? o1[i] : o2[i], 0xFFFFFF);
        }
        hal_set_text_bg(0x162536);
        hal_draw_text(px + 104, py + ph - 30, "[↑/↓]選擇   [Enter/空白]確認   [ESC]離開", 0xB0BEC5);

        /* 先存下不含中央骰子的靜態封面快取，再畫上旋轉 3D 骰子 */
        g_cache_ready = hal_save_bg_cache();
        gfx_draw_3d_dice(px + pw / 2, py + 106, 6, 1, 6, g_tick_ms, 1);
        hal_present();
        return;
    }

    if (g_state == ST_SETUP) {
        hal_clear(0x0B1929);
        int pw = 540, ph = sh - 56, px = (sw - pw) / 2, py = 28;
        gfx_panel(px, py, pw, ph, 0x162536, 0x4FC3F7, 0x1565C0, 40);
        hal_set_text_bg(0x1565C0);
        hal_draw_text(px + 150, py + 11, "3D 大富翁開局設定 (←/→ 切換)", 0xFFFFFF);

        const MapDef *sm = map_get(g_setup_map);
        char rows[6][112];
        snprintf(rows[0], sizeof(rows[0]), "選擇地圖：< %s (%d格 寬敞版) >", sm->name, sm->tile_count);
        snprintf(rows[1], sizeof(rows[1]), "人類玩家：< %d 位 >  (共 %d 人對戰)", g_setup_humans, g_setup_humans + g_setup_ais);
        snprintf(rows[2], sizeof(rows[2]), "電腦 AI ：< %d 位 >  (可自由選 0~3 位)", g_setup_ais);
        snprintf(rows[3], sizeof(rows[3]), "P1 角色 ：< %s > (%s)", CHAR_NAMES[g_setup_p1_char], CHAR_TRAITS[g_setup_p1_char]);
        snprintf(rows[4], sizeof(rows[4]), "勝利規則：< %s >", WIN_MODE_NAMES[g_setup_win]);
        snprintf(rows[5], sizeof(rows[5]), ">>>   進入 3D 大世界！出發！   <<<");

        for (int i = 0; i < 6; i++) {
            uint32_t bg = (i == g_setup_row) ? ((i == 5) ? 0xE65100 : 0x2E7D32) : 0x243648;
            hal_fill_rect(px + 24, py + 54 + i * 48, pw - 48, 38, bg);
            hal_draw_rect(px + 24, py + 54 + i * 48, pw - 48, 38, (i == g_setup_row) ? 0xFFEA00 : 0x455A64);
            hal_set_text_bg(bg);
            hal_draw_text(px + 38, py + 64 + i * 48, rows[i], 0xFFFFFF);
        }
        gfx_draw_character_3d(px + pw - 58, py + 196, 0, g_setup_p1_char, 0, PLAYER_COLORS[0], 0, 0, GOD_NONE, 0, 1, 0);
        hal_set_text_bg(0x162536);
        hal_draw_text(px + 28, py + ph - 32, sm->subtitle, 0xFFD54F);
        hal_present();
        return;
    }

    if (g_state == ST_GAME_OVER) {
        hal_clear(0x0B1929);
        int pw = 460, ph = 290, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0xFFD54F, 0xE65100, 44);
        hal_set_text_bg(0xE65100);
        hal_draw_text(px + 148, py + 12, "【最終大富翁誕生】", 0xFFFFFF);
        hal_set_text_bg(0x162536);
        if (g.win_player >= 0) {
            char wb[96];
            snprintf(wb, sizeof(wb), "恭喜 P%d %s 獲得勝利！總資產 $%d",
                     g.win_player + 1, g.players[g.win_player].name, calc_net_worth(g.win_player));
            hal_draw_text(px + 42, py + 80, wb, 0x69F0AE);
            gfx_draw_character_3d(px + pw / 2, py + 186, 6, g.players[g.win_player].char_id, g.win_player,
                                  PLAYER_COLORS[g.win_player], 1, 2, GOD_WEALTH, 0, 1, 0);
        }
        hal_draw_text(px + 100, py + ph - 36, "[按 Enter / 空白鍵 返回主選單]", 0xFFFFFF);
        hal_present();
        return;
    }

    if (g_state == ST_MG_COINS) {
        hal_clear(0x12263A);
        for (int i = 0; i < 18; i++) hal_fill_rect((i * 97) % sw, 45 + (i * 53) % (sh - 160), 2, 2, 0xFFF59D);
        hal_fill_rect(0, sh - 64, sw, 64, 0x2E7D32);
        hal_fill_rect(0, sh - 66, sw, 4, 0x81C784);
        hal_fill_rect(0, 0, sw, 38, 0x0D1B2A);
        g_cache_ready = hal_save_bg_cache();

        hal_set_text_bg(0x0D1B2A);
        char mh[128];
        int sec = (g_mg_time_ms + 999) / 1000;
        g_mg_last_sec = sec; g_mg_last_score = g_mg_score;
        snprintf(mh, sizeof(mh), "【接金幣大作戰】 剩餘:%d秒  已獲點券:%d  (←/→ 移動躲炸彈!)", sec, g_mg_score);
        hal_draw_text(16, 10, mh, 0xFFD54F);

        Player *cp = &g.players[g.cur_player];
        gfx_draw_character_3d(g_mg_px, sh - 74, 0, cp->char_id, g.cur_player, PLAYER_COLORS[g.cur_player],
                              0, 0, GOD_NONE, 0, 1, 0);
        if (g_mg_done) {
            gfx_panel(sw / 2 - 190, sh / 2 - 60, 380, 120, 0x162536, 0xFFD54F, 0x2E7D32, 34);
            hal_set_text_bg(0x2E7D32); hal_draw_text(sw / 2 - 70, sh / 2 - 52, "【挑戰完成！】", 0xFFFFFF);
            hal_set_text_bg(0x162536);
            char res[96]; snprintf(res, sizeof(res), "總共接獲 %d 點券！按 Enter 繼續", g_mg_score);
            hal_draw_text(sw / 2 - 150, sh / 2 + 4, res, 0x69F0AE);
        }
        hal_present();
        return;
    }

    /* 主 3D 等角世界場景 */
    if (g_state == ST_WALK_ANIM) {
        /* 先繪製不含移動中角色的靜態世界並存入快取，再畫移動角色 */
        render_world_scene(sw, sh, g.cur_player);
        g_cache_ready = hal_save_bg_cache();

        int wx, wy, jh, fl, dp;
        player_iso_pos(g.cur_player, &wx, &wy, &jh, &fl, &dp);
        int sx = wx - g_cam_x, sy = wy - g_cam_y;
        gfx_draw_character_3d(sx, sy, jh, g.players[g.cur_player].char_id, g.cur_player,
                              PLAYER_COLORS[g.cur_player], g_walk_phase, g.players[g.cur_player].vehicle,
                              g.players[g.cur_player].god, fl, 1, g_walk_steps_left);
        g_prev_x = sx - 38; g_prev_y = sy - jh - 92; g_prev_w = 84; g_prev_h = jh + 114;
        hal_present();
        return;
    }

    render_world_scene(sw, sh, -1);

    if (g_state == ST_DICE_ANIM) {
        g_cache_ready = hal_save_bg_cache();
        draw_dice_overlay(sw, sh);
        hal_present();
        return;
    }

    if (g_state == ST_CARD_FX) {
        g_cache_ready = hal_save_bg_cache();
        gfx_vfx_render(g_cam_x, g_cam_y);
        hal_present();
        return;
    }

    Player *cp = &g.players[g.cur_player];
    const MapDef *m = cur_map();

    /* ESC 遊戲暫停選單 */
    if (g_state == ST_PAUSE_MENU) {
        int pw = 360, ph = 330, px = (sw - pw) / 2, py = (sh - ph) / 2;
        hal_clear_text_in_rect(px - 6, py - 6, pw + 12, ph + 12); hal_dim_rect(0, 0, sw, sh);
        gfx_panel(px, py, pw, ph, 0x142230, 0xFFD54F, 0x1565C0, 40);
        hal_set_text_bg(0x1565C0);
        hal_draw_text(px + 96, py + 11, "【遊戲暫停選單】", 0xFFFFFF);

        const char *opts[7];
        opts[0] = "繼續遊戲 (ESC)";
        opts[1] = "立即儲存進度";
        opts[2] = g_fast_ai ? "AI 速度：高速模式" : "AI 速度：一般速度";
        opts[3] = "全體資產排行榜";
        opts[4] = "操作與規則說明";
        opts[5] = "返回主標題畫面";
        opts[6] = "存檔並離開遊戲";

        for (int i = 0; i < 7; i++) {
            uint32_t bg = (i == g_pause_sel) ? ((i == 6) ? 0xC62828 : 0x2E7D32) : 0x223446;
            hal_fill_rect(px + 24, py + 52 + i * 38, pw - 48, 32, bg);
            hal_draw_rect(px + 24, py + 52 + i * 38, pw - 48, 32, (i == g_pause_sel) ? 0xFFEA00 : 0x455A64);
            hal_set_text_bg(bg);
            char line[80];
            snprintf(line, sizeof(line), "%s %s", (i == g_pause_sel) ? ">" : " ", opts[i]);
            hal_draw_text(px + 38, py + 59 + i * 38, line, 0xFFFFFF);
        }
        hal_present();
        return;
    }

    if (g_state == ST_TURN_MENU) {
        /* 手機版大富翁 4 Fun 風格：右下超大「擲骰」按鈕 + 橫排圓角功能鍵 */
        int roll_w = 126, roll_h = 54;
        int roll_x = sw - roll_w - 14, roll_y = sh - roll_h - 12;
        uint32_t rbg = (g_menu_sel == 0) ? 0xF57C00 : 0xE65100;
        gfx_round_btn(roll_x, roll_y, roll_w, roll_h, 16, rbg, (g_menu_sel == 0) ? 0xFFEA00 : 0xFFE082);
        hal_set_text_bg(rbg);
        hal_draw_text(roll_x + 28, roll_y + 17, "擲 骰 子", 0xFFFFFF);

        const char *btns[6] = { "卡片", "股市", "地圖", "資產", "存檔", "暫停" };
        int bw = 74, bh = 38, gap = 8;
        int start_x = roll_x - 6 * (bw + gap) - 4;
        int by = sh - bh - 14;
        for (int i = 0; i < 6; i++) {
            int sel = (g_menu_sel == i + 1);
            uint32_t bg = sel ? 0x2E7D32 : 0x1B2B3A;
            uint32_t bd = sel ? 0xFFEA00 : 0x607D8B;
            gfx_round_btn(start_x + i * (bw + gap), by, bw, bh, 10, bg, bd);
            hal_set_text_bg(bg);
            hal_draw_text(start_x + i * (bw + gap) + 18, by + 10, btns[i], 0xFFFFFF);
        }
    } else if (g_state == ST_AI_PAUSE && g_ai_toast[0]) {
        int pw = 480, ph = 92, px = (sw - pw) / 2, py = 86;
        gfx_panel(px, py, pw, ph, 0x162536, PLAYER_COLORS[g.cur_player], PLAYER_COLORS[g.cur_player], 28);
        hal_set_text_bg(PLAYER_COLORS[g.cur_player]);
        char thdr[64];
        snprintf(thdr, sizeof(thdr), "【P%d %s 行動報告】 (空白繼續 / ESC暫停)", g.cur_player + 1, cp->name);
        hal_draw_text(px + 14, py + 6, thdr, 0xFFFFFF);
        hal_set_text_bg(0x162536);
        hal_draw_text(px + 16, py + 44, g_ai_toast, 0xFFEA00);
    } else if (g_state == ST_FREE_MAP) {
        const TileDef *td = &m->tiles[g_free_cursor];
        const TileState *ts = &g.tiles[g_free_cursor];
        gfx_panel(14, sh - 62, sw - 28, 50, 0x121F2B, 0xFFEA00, 0, 0);
        hal_set_text_bg(0x121F2B);
        char info[160];
        if (ts->owner >= 0) {
            snprintf(info, sizeof(info), "【3D檢視 #%d %s】 擁有者:%s  Lv.%d  過路費:$%d  (←→移動 / ESC返回)",
                     g_free_cursor, td->name, g.players[ts->owner].name, ts->level, calc_tile_toll(g_free_cursor));
        } else {
            snprintf(info, sizeof(info), "【3D檢視 #%d %s】 售價:$%d  神明:%s  (←→移動 / PgUpDn快跳 / ESC返回)",
                     g_free_cursor, td->name, td->base_price, GOD_NAMES[ts->map_god]);
        }
        hal_draw_text(24, sh - 46, info, 0xFFFFFF);
    } else if (g_state == ST_BUY_LAND) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        const TileDef *td = &m->tiles[cp->pos];
        const TileState *ts = &g.tiles[cp->pos];
        int is_buy = (ts->owner < 0);
        int cost = (cp->god == GOD_FORTUNE) ? 0 : (is_buy ? td->base_price : td->base_price / 2);

        int pw = 460, ph = 180, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0xFFD54F, 0x1565C0, 36);
        hal_set_text_bg(0x1565C0);
        hal_draw_text(px + 16, py + 9, is_buy ? "【土地購買】" : "【3D 大樓升級】", 0xFFFFFF);
        hal_set_text_bg(0x162536);
        char l1[128], l2[128];
        snprintf(l1, sizeof(l1), is_buy ? "是否購買【%s】？" : "是否將【%s】升級至 Lv.%d？", td->name, ts->level + 1);
        snprintf(l2, sizeof(l2), "費用：$%d %s (目前現金：$%d)", cost, (cp->god == GOD_FORTUNE) ? "[福神免費]" : "", cp->cash);
        hal_draw_text(px + 20, py + 52, l1, 0xFFD54F);
        hal_draw_text(px + 20, py + 82, l2, 0xFFFFFF);
        const char *yn[2] = { "確認購買 / 升級", "暫時放棄" };
        for (int i = 0; i < 2; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x2E7D32 : 0x37474F;
            hal_fill_rect(px + 24 + i * 212, py + 122, 196, 38, bg);
            hal_set_text_bg(bg);
            hal_draw_text(px + 42 + i * 212, py + 132, yn[i], 0xFFFFFF);
        }
    } else if (g_state == ST_BUILD_COMM) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = 520, ph = 260, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0xFFD54F, 0x6A1B9A, 36);
        hal_set_text_bg(0x6A1B9A);
        hal_draw_text(px + 16, py + 9, "【大型商業用地 — 興建 3D 建築】", 0xFFFFFF);
        const char *cinfo[5] = {
            "1. 商業中心 — 超高過路費 ($1500+)",
            "2. 大飯店   — 過路費 + 強制對手休息1回合",
            "3. 研究所   — 每4天自動免費研發1張卡片",
            "4. 公園     — 免費開放，路過送20點券",
            "5. 加油站   — 依對手骰子點數 x $350 收費"
        };
        for (int i = 0; i < 5; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x1565C0 : 0x243648;
            hal_fill_rect(px + 16, py + 48 + i * 38, pw - 32, 33, bg);
            hal_set_text_bg(bg);
            hal_draw_text(px + 24, py + 56 + i * 38, cinfo[i], 0xFFFFFF);
        }
    } else if (g_state == ST_CARD_BAG) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = 560, ph = 290, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0x4FC3F7, 0x00695C, 36);
        hal_set_text_bg(0x00695C);
        char t[96]; snprintf(t, sizeof(t), "【卡片袋 (%d/%d)】 Enter使用 / ESC返回", cp->card_count, MAX_CARDS_HELD);
        hal_draw_text(px + 16, py + 9, t, 0xFFFFFF);
        if (cp->card_count == 0) {
            hal_set_text_bg(0x162536);
            hal_draw_text(px + 40, py + 90, "目前沒有任何卡片，請到卡片商店購買！", 0xCFD8DC);
        } else {
            int start = (g_menu_sel >= 6) ? (g_menu_sel - 5) : 0;
            for (int i = start; i < cp->card_count && i < start + 6; i++) {
                int row = i - start, ct = cp->cards[i];
                uint32_t bg = (i == g_menu_sel) ? 0x2E7D32 : 0x243648;
                hal_fill_rect(px + 14, py + 46 + row * 37, pw - 28, 32, bg);
                hal_set_text_bg(bg);
                char line[128]; snprintf(line, sizeof(line), "%s [%s] %s", (i == g_menu_sel) ? ">" : " ", CARD_NAMES[ct], CARD_DESCS[ct]);
                hal_draw_text(px + 22, py + 53 + row * 37, line, 0xFFFFFF);
            }
        }
    } else if (g_state == ST_CARD_TARGET) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        CardType ct = (CardType)g_pending_card_type;
        int pw = 500, ph = 240, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0xFF9100, 0xBF360C, 36);
        hal_set_text_bg(0xBF360C);
        char t[96]; snprintf(t, sizeof(t), "選擇【%s】目標 (ESC取消)", CARD_NAMES[ct]);
        hal_draw_text(px + 16, py + 9, t, 0xFFFFFF);

        if (ct == CARD_REMOTE_DICE) {
            for (int i = 0; i < 6; i++) {
                uint32_t bg = (i == g_menu_sel) ? 0xE65100 : 0x37474F;
                hal_fill_rect(px + 20 + i * 76, py + 90, 66, 60, bg);
                hal_set_text_bg(bg);
                char b[16]; snprintf(b, sizeof(b), "%d 步", i + 1);
                hal_draw_text(px + 34 + i * 76, py + 110, b, 0xFFFFFF);
            }
        } else if (ct == CARD_RED_STOCK || ct == CARD_BLACK_STOCK) {
            for (int s = 0; s < NUM_STOCKS; s++) {
                uint32_t bg = (s == g_menu_sel) ? 0xC62828 : 0x243648;
                hal_fill_rect(px + 16, py + 44 + s * 30, pw - 32, 26, bg);
                hal_set_text_bg(bg);
                char b[96]; snprintf(b, sizeof(b), "%s %s ($%d)", (s == g_menu_sel) ? ">" : " ", g.stocks[s].name, g.stocks[s].price);
                hal_draw_text(px + 24, py + 48 + s * 30, b, 0xFFFFFF);
            }
        } else {
            for (int i = 0; i < g.player_count; i++) {
                uint32_t bg = (i == g_menu_sel) ? 0xAD1457 : 0x243648;
                hal_fill_rect(px + 16, py + 50 + i * 42, pw - 32, 36, bg);
                hal_set_text_bg(bg);
                char b[112];
                snprintf(b, sizeof(b), "%s P%d %s  現:$%d 存:$%d 卡:%d",
                         (i == g_menu_sel) ? ">" : " ", i + 1, g.players[i].name,
                         g.players[i].cash, g.players[i].deposit, g.players[i].card_count);
                hal_draw_text(px + 24, py + 59 + i * 42, b, 0xFFFFFF);
            }
        }
    } else if (g_state == ST_STOCK_MARKET) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = sw - 60, ph = sh - 86, px = 30, py = 44;
        gfx_panel(px, py, pw, ph, 0x131F2B, 0xFFD54F, 0xB71C1C, 36);
        hal_set_text_bg(0xB71C1C);
        hal_draw_text(px + 16, py + 9, "【台北證券交易所】 ↑↓選股  →買100  ←賣100  PgDn買500  ESC返回", 0xFFFFFF);

        int lw = pw - 230;
        for (int s = 0; s < NUM_STOCKS; s++) {
            Stock *st = &g.stocks[s];
            int diff = st->price - st->prev_price;
            uint32_t bg = (s == g_stock_sel) ? 0x2C3E50 : 0x1B2836;
            uint32_t col = (diff > 0) ? 0xFF5252 : ((diff < 0) ? 0x69F0AE : 0xFFFFFF);
            hal_fill_rect(px + 12, py + 46 + s * 46, lw, 40, bg);
            hal_draw_rect(px + 12, py + 46 + s * 46, lw, 40, (s == g_stock_sel) ? 0xFFEA00 : 0x37474F);
            hal_set_text_bg(bg);
            char l[128];
            snprintf(l, sizeof(l), "%s %s  $%d (%+d)  持股:%d",
                     (s == g_stock_sel) ? ">" : " ", st->name, st->price, diff, cp->stocks[s]);
            hal_draw_text(px + 18, py + 57 + s * 46, l, col);
        }

        int gx = px + lw + 22, gy = py + 52, gw = 196, gh = 200;
        gfx_panel(gx, gy, gw, gh, 0x0B141C, 0x455A64, 0, 0);
        hal_set_text_bg(0x0B141C);
        hal_draw_text(gx + 10, gy + 8, "12日走勢圖", 0xFFD54F);

        Stock *sel = &g.stocks[g_stock_sel];
        int mn = sel->hist[0], mx = sel->hist[0];
        for (int h = 1; h < STOCK_HIST_LEN; h++) {
            if (sel->hist[h] < mn) mn = sel->hist[h];
            if (sel->hist[h] > mx) mx = sel->hist[h];
        }
        if (mx == mn) { mx += 5; mn -= 5; }
        for (int h = 0; h < STOCK_HIST_LEN - 1; h++) {
            int x0 = gx + 14 + (h * (gw - 28)) / (STOCK_HIST_LEN - 1);
            int x1 = gx + 14 + ((h + 1) * (gw - 28)) / (STOCK_HIST_LEN - 1);
            int y0 = gy + gh - 24 - ((sel->hist[h] - mn) * (gh - 60)) / (mx - mn);
            int y1 = gy + gh - 24 - ((sel->hist[h + 1] - mn) * (gh - 60)) / (mx - mn);
            uint32_t lc = (sel->hist[h + 1] >= sel->hist[h]) ? 0xFF5252 : 0x69F0AE;
            hal_draw_line(x0, y0, x1, y1, lc);
            hal_fill_circle(x1, y1, 2, lc);
        }
        char cap[64]; snprintf(cap, sizeof(cap), "現金:$%d", cp->cash);
        hal_set_text_bg(0x131F2B); hal_draw_text(gx, gy + gh + 14, cap, 0x80CBC4);
    } else if (g_state == ST_BANK_MENU) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = 460, ph = 290, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0xFFD54F, 0xF57F17, 36);
        hal_set_text_bg(0xF57F17);
        char info[128]; snprintf(info, sizeof(info), "【銀行】現:$%d 存:$%d 貸:$%d", cp->cash, cp->deposit, cp->loan);
        hal_draw_text(px + 14, py + 9, info, 0x1A1A1A);
        const char *bopts[6] = {
            "存入 $2000 (月息 5%)", "領出 $2000", "存入 $5000",
            "申請信用貸款 $10000 (月息10%)", "清償全部貸款", "離開銀行"
        };
        for (int i = 0; i < 6; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x2E7D32 : 0x243648;
            hal_fill_rect(px + 16, py + 46 + i * 37, pw - 32, 32, bg);
            hal_set_text_bg(bg);
            hal_draw_text(px + 24, py + 53 + i * 37, bopts[i], 0xFFFFFF);
        }
    } else if (g_state == ST_CARD_SHOP) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = 560, ph = 290, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0x69F0AE, 0x2E7D32, 36);
        hal_set_text_bg(0x2E7D32);
        char t[112]; snprintf(t, sizeof(t), "【卡片商店】 點券:%d  卡片:%d/%d", cp->coupons, cp->card_count, MAX_CARDS_HELD);
        hal_draw_text(px + 16, py + 9, t, 0xFFFFFF);
        for (int i = 0; i < 6; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x1565C0 : 0x243648;
            hal_fill_rect(px + 14, py + 46 + i * 37, pw - 28, 32, bg);
            hal_set_text_bg(bg);
            char l[128];
            if (i < 5) {
                int c = g_shop_items[i];
                snprintf(l, sizeof(l), "%s [%s] (%d券) %s", (i == g_menu_sel) ? ">" : " ", CARD_NAMES[c], CARD_COSTS[c], CARD_DESCS[c]);
            } else snprintf(l, sizeof(l), "%s 離開商店", (i == g_menu_sel) ? ">" : " ");
            hal_draw_text(px + 20, py + 53 + i * 37, l, 0xFFFFFF);
        }
    } else if (g_state == ST_MAGIC_HOUSE) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = 480, ph = 220, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0xCE93D8, 0x6A1B9A, 36);
        hal_set_text_bg(0x6A1B9A);
        hal_draw_text(px + 16, py + 9, "【神秘魔法屋 — 施展免費魔法】", 0xFFFFFF);
        const char *mopts[3] = {
            "1. 吸金大法 — 向每位對手吸取 $1500",
            "2. 感冒魔法 — 全體對手住院 1 回合",
            "3. 點石成券 — 免費獲得 50 點券"
        };
        for (int i = 0; i < 3; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x8E24AA : 0x243648;
            hal_fill_rect(px + 18, py + 54 + i * 48, pw - 36, 38, bg);
            hal_set_text_bg(bg);
            hal_draw_text(px + 26, py + 64 + i * 48, mopts[i], 0xFFFFFF);
        }
    } else if (g_state == ST_LOTTERY) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = 460, ph = 190, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0xFFD54F, 0xE65100, 36);
        hal_set_text_bg(0xE65100);
        hal_draw_text(px + 16, py + 9, "【大富翁樂透】 票價$500 / 頭獎$15000 (每月15日開獎)", 0xFFFFFF);
        hal_set_text_bg(0x162536);
        char b[96]; snprintf(b, sizeof(b), "選擇幸運號碼：<  %02d  >  (←/→ 調整)", g_lottery_pick);
        hal_draw_text(px + 70, py + 76, b, 0xFFEA00);
        hal_draw_text(px + 76, py + 132, "[Enter/空白] 購買彩券   [ESC] 不買離開", 0x80CBC4);
    } else if (g_state == ST_MINIGAME_SELECT) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = 460, ph = 220, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0xFFEA00, 0xE65100, 36);
        hal_set_text_bg(0xE65100);
        hal_draw_text(px + 16, py + 9, "【歡樂遊樂場 — 免費賺點券】", 0xFFFFFF);
        const char *gopts[3] = {
            "1. 接金幣大作戰 (即時動作閃炸彈！)",
            "2. 幸運猜大小 (3D骰拼手氣)",
            "3. 不玩直接離開"
        };
        for (int i = 0; i < 3; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x2E7D32 : 0x243648;
            hal_fill_rect(px + 18, py + 54 + i * 48, pw - 36, 38, bg);
            hal_set_text_bg(bg);
            hal_draw_text(px + 26, py + 64 + i * 48, gopts[i], 0xFFFFFF);
        }
    } else if (g_state == ST_MG_DICE) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = 480, ph = 210, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0xFFD54F, 0x1565C0, 36);
        hal_set_text_bg(0x1565C0);
        hal_draw_text(px + 16, py + 9, "【3D 幸運猜大小】 猜中 +40 點券 / 安慰獎 +10 點券", 0xFFFFFF);
        hal_set_text_bg(0x162536);
        if (g_mg_dice > 0) {
            gfx_draw_3d_dice(px + pw - 74, py + 110, g_mg_dice, 0, 0, g_tick_ms, 0);
            hal_draw_text(px + 24, py + 76, g_mg_result, 0x69F0AE);
            hal_draw_text(px + 24, py + 132, "[按 Enter / 空白鍵 繼續]", 0xFFFFFF);
        } else {
            const char *opts[3] = { "猜大(4~6)", "猜小(1~3)", "離開" };
            for (int i = 0; i < 3; i++) {
                uint32_t bg = (i == g_menu_sel) ? 0xE65100 : 0x37474F;
                hal_fill_rect(px + 24 + i * 148, py + 98, 134, 42, bg);
                hal_set_text_bg(bg);
                hal_draw_text(px + 42 + i * 148, py + 110, opts[i], 0xFFFFFF);
            }
        }
    } else if (g_state == ST_STATUS_VIEW) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = 560, ph = 270, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0xFFD54F, 0x263238, 36);
        hal_set_text_bg(0x263238);
        hal_draw_text(px + 16, py + 9, "【全體玩家資產排行榜】 (按 Enter / ESC 返回)", 0xFFD54F);
        for (int i = 0; i < g.player_count; i++) {
            Player *pl = &g.players[i];
            int lands = 0;
            for (int t = 0; t < m->tile_count; t++) if (g.tiles[t].owner == i) lands++;
            int ry = py + 46 + i * 52;
            hal_fill_rect(px + 14, ry, pw - 28, 44, 0x243648);
            hal_fill_rect(px + 14, ry, 8, 44, PLAYER_COLORS[i]);
            hal_set_text_bg(0x243648);
            char b1[128], b2[128];
            snprintf(b1, sizeof(b1), "P%d %s%s  總資產:$%d  地產:%d筆  神明:%s",
                     i + 1, pl->name, pl->is_ai ? "[AI]" : "", calc_net_worth(i), lands, GOD_NAMES[pl->god]);
            snprintf(b2, sizeof(b2), "現金:$%d  存款:$%d  貸款:$%d  點券:%d  卡:%d",
                     pl->cash, pl->deposit, pl->loan, pl->coupons, pl->card_count);
            hal_draw_text(px + 28, ry + 4, b1, 0xFFFFFF);
            hal_draw_text(px + 28, ry + 23, b2, 0xB0BEC5);
        }
    } else if (g_state == ST_HELP_VIEW) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = 560, ph = 260, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0x4FC3F7, 0x1565C0, 36);
        hal_set_text_bg(0x1565C0);
        hal_draw_text(px + 16, py + 9, "【大富翁 4 Fun 流暢 3D 版 操作指南】 (ESC返回)", 0xFFFFFF);
        hal_set_text_bg(0x162536);
        const char *hl[] = {
            "• [ESC] 隨時暫停遊戲並開啟暫停選單 (存檔/調AI速度/離開)",
            "• 方向鍵：選單 / 逛 3D 地圖    Enter/空白鍵：擲 3D 骰 / 確認",
            "• [C] 卡片袋  [S] 股市(含走勢圖)  [M] 自由逛 3D 大地圖  [F] 存檔",
            "• AI 行動中按 [空白鍵] 可切換「一般速度 / 高速模式」或立即跳過報告",
            "• 機車=2顆3D骰、汽車=3顆3D骰、加油站依點數收費、研究所免費產卡"
        };
        for (int i = 0; i < 5; i++) hal_draw_text(px + 20, py + 52 + i * 36, hl[i], 0xECEFF1);
    } else if (g_state == ST_MESSAGE) {
        hal_clear_text_in_rect(0, 34, sw, sh - 34); hal_dim_rect(0, 34, sw, sh - 34);
        int pw = 480, ph = 186, px = (sw - pw) / 2, py = (sh - ph) / 2;
        gfx_panel(px, py, pw, ph, 0x162536, 0xFFD54F, 0x263238, 36);
        hal_set_text_bg(0x263238);
        hal_draw_text(px + 16, py + 9, g_msg_title, 0xFFD54F);
        hal_set_text_bg(0x162536);
        hal_draw_text(px + 20, py + 52, g_msg_l1, 0xFFFFFF);
        hal_draw_text(px + 20, py + 82, g_msg_l2, 0xCFD8DC);
        hal_draw_text(px + 20, py + 110, g_msg_l3, 0xB0BEC5);
        hal_draw_text(px + 20, py + 148, "[按 Enter / 空白鍵 繼續]", 0x80CBC4);
    }

    hal_present();
}
