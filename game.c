#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 大富翁 4 Fun — CD-952 原生致敬版核心引擎
 * 特色：
 *  - 28 格台灣環島地圖（小地產4色區連鎖、大型商業用地、銀行、點券、卡店、新聞、魔法屋、醫院、監獄、遊樂場）
 *  - 4 位經典角色（孫小美、阿土伯、錢夫人、沙隆巴斯），支援 1~4 人或對電腦 AI
 *  - 現金 + 銀行存款(定期配息) + 點券 三幣制
 *  - 12 種經典卡片（遙控骰子、烏龜、路障、停留、均富、購地、怪獸、冬眠、查稅、紅卡、黑卡、飛彈、送神）
 *  - 5 種神明附身（財神、福神、土地公、窮神、衰神）
 *  - 4 支台股紅漲綠跌股市系統（含漲跌停、紅黑卡影響、每月配息）
 *  - 內建「幸運猜大小」點券小遊戲
 *  - 即時存檔 / 讀檔 (C:\richman4.sav)
 * ============================================================ */

#define MAP_SIZE       28
#define MAX_PLAYERS    4
#define MAX_CARDS_HELD 12
#define NUM_STOCKS     4
#define SAVE_MAGIC     0x524D3431u /* "RM41" */

typedef enum {
    TILE_START = 0,     /* 起點 / 銀行 */
    TILE_LAND,          /* 一般小地產 (0~5級) */
    TILE_COMMERCIAL,    /* 大型商業用地 (商業中心/飯店/研究所/公園) */
    TILE_COUPON,        /* 點券格 */
    TILE_SHOP,          /* 卡片商店 */
    TILE_NEWS,          /* 新聞/命運 */
    TILE_HOSPITAL,      /* 醫院 */
    TILE_PRISON,        /* 監獄 */
    TILE_MAGIC,         /* 魔法屋 */
    TILE_MINIGAME       /* 小遊戲 */
} TileType;

typedef enum {
    COMM_NONE = 0,
    COMM_MALL,          /* 商業中心：超高過路費 */
    COMM_HOTEL,         /* 大飯店：過路費 + 強制住宿 1 回合 */
    COMM_LAB,           /* 研究所：每回合免費研發 1 張卡片 */
    COMM_PARK           /* 公園：免收費，路過心情好 +20 點券 */
} CommType;

typedef enum {
    GOD_NONE = 0,
    GOD_WEALTH,         /* 財神：免過路費 */
    GOD_FORTUNE,        /* 福神：買/升級免費 */
    GOD_LAND,           /* 土地公：免費強佔踩到的地 */
    GOD_POOR,           /* 窮神：過路費加倍 */
    GOD_BADLUCK         /* 衰神：每回合隨機掉錢 */
} GodType;

typedef enum {
    CARD_NONE = 0,
    CARD_REMOTE_DICE,   /* 遙控骰子 */
    CARD_TURTLE,        /* 烏龜卡 */
    CARD_ROADBLOCK,     /* 路障卡 */
    CARD_STAY,          /* 停留卡 */
    CARD_EQUAL_WEALTH,  /* 均富卡 */
    CARD_BUY_LAND,      /* 購地卡 */
    CARD_MONSTER,       /* 怪獸卡 */
    CARD_HIBERNATE,     /* 冬眠卡 */
    CARD_TAX,           /* 查稅卡 */
    CARD_RED_STOCK,     /* 紅卡(漲停) */
    CARD_BLACK_STOCK,   /* 黑卡(跌停) */
    CARD_MISSILE,       /* 飛彈卡(住院) */
    CARD_BANISH_GOD,    /* 送神卡 */
    CARD_COUNT
} CardType;

typedef enum {
    ST_TITLE = 0,
    ST_SETUP,
    ST_TURN_MENU,       /* 主回合選單 */
    ST_ROLL_ANIM,       /* 擲骰結果與移動確認 */
    ST_BUY_LAND,        /* 買地/升級詢問 */
    ST_BUILD_COMM,      /* 大型商業地選擇建築 */
    ST_CARD_BAG,        /* 卡片袋選擇 */
    ST_CARD_TARGET,     /* 選擇目標(玩家/股票/遙控點數) */
    ST_STOCK_MARKET,    /* 股票交易所 */
    ST_BANK_MENU,       /* 銀行存取款 */
    ST_CARD_SHOP,       /* 卡片商店 */
    ST_MAGIC_HOUSE,     /* 魔法屋 */
    ST_MINIGAME,        /* 幸運猜大小賺點券 */
    ST_STATUS_VIEW,     /* 全體資產總覽 */
    ST_HELP_VIEW,       /* 操作與規則說明 */
    ST_MESSAGE,         /* 事件/新聞彈窗 */
    ST_GAME_OVER        /* 勝利結算 */
} GameState;

typedef struct {
    const char *name;
    TileType type;
    int group;          /* 1~4 同色地段 */
    int base_price;
} TileDef;

typedef struct {
    int owner;          /* -1 無主, 0..3 玩家 */
    int level;          /* 0~5 */
    CommType comm;      /* 商業建築類型 */
    int has_roadblock;  /* 1 = 有路障 */
} TileState;

typedef struct {
    char name[24];
    int is_ai;
    int bankrupt;
    int pos;
    int cash;
    int deposit;
    int coupons;
    int stocks[NUM_STOCKS];
    int cards[MAX_CARDS_HELD];
    int card_count;
    int skip_turns;     /* 住院/坐牢/冬眠/住宿 */
    int skip_reason;    /* 0無 1醫院 2監獄 3冬眠 4住宿 */
    int turtle_turns;   /* 烏龜卡剩餘回合 */
    int remote_dice;    /* 0無, 1..6 指定步數 */
    int stay_flag;      /* 1 = 原地停留一次 */
    GodType god;
    int god_turns;
} Player;

typedef struct {
    char name[24];
    int price;
    int prev_price;
    int red_buff;       /* >0 漲停, <0 跌停 */
} Stock;

typedef struct {
    uint32_t magic;
    int turn_count;
    int day;
    int month;
    int cur_player;
    int human_count;
    int win_player;
    TileState tiles[MAP_SIZE];
    Player players[MAX_PLAYERS];
    Stock stocks[NUM_STOCKS];
} GameData;

/* --- 靜態資料表 --- */
static const TileDef MAP_DEFS[MAP_SIZE] = {
    /* 上排 0..9 (左到右) */
    { "台北銀行", TILE_START,      0,    0 },
    { "西門町",   TILE_LAND,       1, 1000 },
    { "忠孝東路", TILE_LAND,       1, 1200 },
    { "點券+30",  TILE_COUPON,     0,    0 },
    { "信義商業", TILE_COMMERCIAL, 0, 2200 },
    { "淡水老街", TILE_LAND,       1, 1100 },
    { "大新聞",   TILE_NEWS,       0,    0 },
    { "板橋新板", TILE_LAND,       2, 1300 },
    { "桃園青埔", TILE_LAND,       2, 1400 },
    { "市立醫院", TILE_HOSPITAL,   0,    0 },
    /* 右排 10..13 (上到下) */
    { "新竹科學", TILE_LAND,       2, 1600 },
    { "卡片商店", TILE_SHOP,       0,    0 },
    { "台中商業", TILE_COMMERCIAL, 0, 2400 },
    { "逢甲夜市", TILE_LAND,       3, 1500 },
    /* 下排 14..23 (右到左) */
    { "魔法屋",   TILE_MAGIC,      0,    0 },
    { "彰化鹿港", TILE_LAND,       3, 1300 },
    { "點券+40",  TILE_COUPON,     0,    0 },
    { "嘉義阿里", TILE_LAND,       3, 1400 },
    { "台南安平", TILE_LAND,       3, 1600 },
    { "大新聞",   TILE_NEWS,       0,    0 },
    { "高雄商業", TILE_COMMERCIAL, 0, 2600 },
    { "高雄左營", TILE_LAND,       4, 1800 },
    { "墾丁大街", TILE_LAND,       4, 1900 },
    { "看守監獄", TILE_PRISON,     0,    0 },
    /* 左排 24..27 (下到上) */
    { "花蓮太魯", TILE_LAND,       4, 1700 },
    { "幸運賭場", TILE_MINIGAME,   0,    0 },
    { "宜蘭礁溪", TILE_LAND,       4, 2000 },
    { "卡片商店", TILE_SHOP,       0,    0 }
};

static const uint32_t PLAYER_COLORS[MAX_PLAYERS] = {
    0xE53935, /* P1 孫小美 紅 */
    0x1E88E5, /* P2 阿土伯 藍 */
    0x43A047, /* P3 錢夫人 綠 */
    0xFB8C00  /* P4 沙隆巴斯 橘 */
};

static const uint32_t GROUP_COLORS[5] = {
    0x546E7A, /* 0 特殊格 */
    0xEF5350, /* 1 北部紅 */
    0x42A5F5, /* 2 桃竹藍 */
    0x66BB6A, /* 3 中南綠 */
    0xAB47BC  /* 4 高花紫 */
};

static const char *CHAR_NAMES[MAX_PLAYERS] = {
    "孫小美", "阿土伯", "錢夫人", "沙隆巴斯"
};

static const char *CARD_NAMES[CARD_COUNT] = {
    "無", "遙控骰子", "烏龜卡", "路障卡", "停留卡",
    "均富卡", "購地卡", "怪獸卡", "冬眠卡", "查稅卡",
    "股市紅卡", "股市黑卡", "飛彈卡", "送神卡"
};

static const char *CARD_DESCS[CARD_COUNT] = {
    "",
    "指定下次擲骰步數 (1~6)",
    "目標接下來 3 回合只走 1 步",
    "在腳下設路障，路過必停",
    "讓自己原地再停留 1 次",
    "平分全場所有玩家的現金",
    "原價強制收購腳下對手土地",
    "摧毀腳下或對手最高級建築",
    "讓其他所有玩家冬眠 2 回合",
    "對指定對手課徵 30% 存款稅",
    "指定股票下回合強制漲停",
    "指定股票下回合強制跌停",
    "轟炸對手送進醫院 2 回合",
    "驅逐自己身上的衰神/窮神"
};

static const int CARD_COSTS[CARD_COUNT] = {
    0, 30, 25, 25, 20, 60, 50, 45, 55, 40, 35, 35, 50, 20
};

static const char *GOD_NAMES[6] = {
    "無", "財神", "福神", "土地公", "窮神", "衰神"
};

static const char *COMM_NAMES[5] = {
    "空地", "商業中心", "大飯店", "研究所", "公園"
};

/* --- 執行狀態變數 --- */
static GameData g;
static GameState g_state = ST_TITLE;
static GameState g_msg_next_state = ST_TURN_MENU;

static int g_menu_sel = 0;
static int g_setup_humans = 1;
static int g_last_dice = 1;
static int g_pending_card_idx = -1;
static int g_pending_card_type = CARD_NONE;
static int g_stock_sel = 0;
static int g_shop_items[4] = { CARD_REMOTE_DICE, CARD_ROADBLOCK, CARD_BUY_LAND, CARD_RED_STOCK };
static int g_mg_bet = 20;
static int g_mg_dice = 0;
static char g_mg_result[96] = {0};

static char g_msg_title[64] = {0};
static char g_msg_line1[128] = {0};
static char g_msg_line2[128] = {0};
static char g_msg_line3[128] = {0};
static char g_log_line[128] = "歡迎來到大富翁 4 Fun CD-952 版！";

/* --- PRNG (LCG，避免不同平台 rand 品質差異) --- */
static uint32_t g_rng = 0x12345678u;

static int rng_next(int mod) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (int)((g_rng >> 16) % (uint32_t)mod);
}

/* --- 基礎邏輯輔助 --- */
static int calc_player_net_worth(int pi) {
    const Player *p = &g.players[pi];
    if (p->bankrupt) return 0;
    int total = p->cash + p->deposit;
    for (int s = 0; s < NUM_STOCKS; s++) {
        total += p->stocks[s] * g.stocks[s].price;
    }
    for (int i = 0; i < MAP_SIZE; i++) {
        if (g.tiles[i].owner == pi) {
            total += MAP_DEFS[i].base_price + g.tiles[i].level * (MAP_DEFS[i].base_price / 2);
        }
    }
    return total;
}

static int count_group_owned(int pi, int grp) {
    if (grp <= 0) return 0;
    int c = 0;
    for (int i = 0; i < MAP_SIZE; i++) {
        if (MAP_DEFS[i].group == grp && g.tiles[i].owner == pi) c++;
    }
    return c;
}

static int calc_tile_toll(int ti) {
    const TileDef *td = &MAP_DEFS[ti];
    const TileState *ts = &g.tiles[ti];
    if (ts->owner < 0) return 0;

    if (td->type == TILE_COMMERCIAL) {
        if (ts->comm == COMM_PARK) return 0;
        if (ts->comm == COMM_MALL) return 1400 + ts->level * 600;
        if (ts->comm == COMM_HOTEL) return 900 + ts->level * 400;
        if (ts->comm == COMM_LAB) return 600 + ts->level * 300;
        return 400;
    }

    int base = td->base_price / 4;
    int toll = base + ts->level * (td->base_price / 3);
    int chain = count_group_owned(ts->owner, td->group);
    if (chain >= 2) {
        toll = (toll * (10 + (chain - 1) * 3)) / 10; /* 連鎖加成 1.3x ~ 1.9x */
    }
    return toll;
}

static int add_card(int pi, CardType c) {
    Player *p = &g.players[pi];
    if (c <= CARD_NONE || c >= CARD_COUNT) return 0;
    if (p->card_count >= MAX_CARDS_HELD) return 0;
    p->cards[p->card_count++] = (int)c;
    return 1;
}

static void remove_card_at(int pi, int idx) {
    Player *p = &g.players[pi];
    if (idx < 0 || idx >= p->card_count) return;
    for (int i = idx; i + 1 < p->card_count; i++) {
        p->cards[i] = p->cards[i + 1];
    }
    p->card_count--;
}

static void show_msg(const char *title, const char *l1, const char *l2, const char *l3, GameState next_st) {
    snprintf(g_msg_title, sizeof(g_msg_title), "%s", title ? title : "");
    snprintf(g_msg_line1, sizeof(g_msg_line1), "%s", l1 ? l1 : "");
    snprintf(g_msg_line2, sizeof(g_msg_line2), "%s", l2 ? l2 : "");
    snprintf(g_msg_line3, sizeof(g_msg_line3), "%s", l3 ? l3 : "");
    g_msg_next_state = next_st;
    g_state = ST_MESSAGE;
}

static void pay_money(int pi, int amount, int receiver_pi) {
    Player *p = &g.players[pi];
    if (amount <= 0) return;

    if (p->cash >= amount) {
        p->cash -= amount;
    } else {
        int rem = amount - p->cash;
        p->cash = 0;
        if (p->deposit >= rem) {
            p->deposit -= rem;
        } else {
            /* 強制賣股湊錢 */
            rem -= p->deposit;
            p->deposit = 0;
            for (int s = 0; s < NUM_STOCKS && rem > 0; s++) {
                while (p->stocks[s] > 0 && rem > 0) {
                    int sell = p->stocks[s] >= 100 ? 100 : p->stocks[s];
                    p->stocks[s] -= sell;
                    int val = sell * g.stocks[s].price;
                    if (val >= rem) {
                        p->cash += (val - rem);
                        rem = 0;
                    } else {
                        rem -= val;
                    }
                }
            }
            if (rem > 0) {
                /* 破產！釋放所有土地 */
                p->bankrupt = 1;
                p->cash = 0;
                p->deposit = 0;
                for (int i = 0; i < MAP_SIZE; i++) {
                    if (g.tiles[i].owner == pi) {
                        g.tiles[i].owner = -1;
                        g.tiles[i].level = 0;
                        g.tiles[i].comm = COMM_NONE;
                    }
                }
            }
        }
    }

    if (receiver_pi >= 0 && receiver_pi < MAX_PLAYERS && !g.players[receiver_pi].bankrupt) {
        g.players[receiver_pi].cash += amount;
    }
}

static int check_winner(void) {
    int alive = 0;
    int last_pi = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g.players[i].bankrupt) {
            alive++;
            last_pi = i;
        }
    }
    if (alive <= 1) {
        g.win_player = last_pi;
        return 1;
    }
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
    g_state = ST_TURN_MENU;
    g_menu_sel = 0;
    snprintf(g_log_line, sizeof(g_log_line), "已讀取存檔！第 %d 月第 %d 日，輪到 %s",
             g.month, g.day, g.players[g.cur_player].name);
    return 1;
}

/* --- 開新局 --- */
static void start_new_game(int human_count) {
    memset(&g, 0, sizeof(g));
    g.magic = SAVE_MAGIC;
    g.day = 1;
    g.month = 1;
    g.turn_count = 1;
    g.cur_player = 0;
    g.human_count = human_count;
    g.win_player = -1;

    for (int i = 0; i < MAP_SIZE; i++) {
        g.tiles[i].owner = -1;
        g.tiles[i].level = 0;
        g.tiles[i].comm = COMM_NONE;
        g.tiles[i].has_roadblock = 0;
    }

    const char *snames[NUM_STOCKS] = { "台積晶圓", "長榮海運", "大宇娛樂", "台北地產" };
    const int sprices[NUM_STOCKS] = { 45, 28, 32, 50 };
    for (int s = 0; s < NUM_STOCKS; s++) {
        snprintf(g.stocks[s].name, sizeof(g.stocks[s].name), "%s", snames[s]);
        g.stocks[s].price = sprices[s];
        g.stocks[s].prev_price = sprices[s];
        g.stocks[s].red_buff = 0;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g.players[i];
        snprintf(p->name, sizeof(p->name), "%s", CHAR_NAMES[i]);
        p->is_ai = (i >= human_count) ? 1 : 0;
        p->cash = 15000;
        p->deposit = 10000;
        p->coupons = 60;
        /* 每位角色初始贈送 3 張經典卡片 */
        add_card(i, CARD_REMOTE_DICE);
        add_card(i, CARD_ROADBLOCK);
        add_card(i, (i % 2 == 0) ? CARD_BUY_LAND : CARD_RED_STOCK);
    }

    g_state = ST_TURN_MENU;
    g_menu_sel = 0;
    snprintf(g_log_line, sizeof(g_log_line), "新遊戲開始！輪到 %s 擲骰子。", g.players[0].name);
}

/* --- 股市每日波動與月末結算 --- */
static void update_daily_economy(void) {
    g.day++;
    int is_new_month = 0;
    if (g.day > 15) {
        g.day = 1;
        g.month++;
        is_new_month = 1;
    }

    for (int s = 0; s < NUM_STOCKS; s++) {
        Stock *st = &g.stocks[s];
        st->prev_price = st->price;
        int pct = 0;
        if (st->red_buff > 0) {
            pct = 10;
            st->red_buff--;
        } else if (st->red_buff < 0) {
            pct = -10;
            st->red_buff++;
        } else {
            pct = rng_next(19) - 9; /* -9% ~ +9% */
        }
        int delta = (st->price * pct) / 100;
        if (delta == 0 && pct != 0) delta = (pct > 0) ? 1 : -1;
        st->price += delta;
        if (st->price < 8) st->price = 8;
        if (st->price > 300) st->price = 300;
    }

    /* 研究所每日研發卡片 & 每月銀行利息與股票配息 */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g.players[i];
        if (p->bankrupt) continue;

        if (is_new_month) {
            int interest = p->deposit / 20; /* 5% 月息 */
            p->deposit += interest;
            for (int s = 0; s < NUM_STOCKS; s++) {
                if (p->stocks[s] > 0) {
                    p->cash += (p->stocks[s] * g.stocks[s].price) / 25;
                }
            }
        }
    }

    /* 研究所產卡 */
    for (int t = 0; t < MAP_SIZE; t++) {
        if (g.tiles[t].owner >= 0 && g.tiles[t].comm == COMM_LAB && (g.day % 4 == 0)) {
            int ow = g.tiles[t].owner;
            if (!g.players[ow].bankrupt) {
                add_card(ow, (CardType)(1 + rng_next(CARD_COUNT - 1)));
            }
        }
    }
}

/* --- 換下一位玩家 --- */
static void advance_turn(void);

static void run_ai_turn(int pi);

static void refresh_shop_items(void) {
    for (int i = 0; i < 4; i++) {
        g_shop_items[i] = 1 + rng_next(CARD_COUNT - 1);
    }
}

static void handle_landing(int pi, int is_ai_mode) {
    Player *p = &g.players[pi];
    int pos = p->pos;
    const TileDef *td = &MAP_DEFS[pos];
    TileState *ts = &g.tiles[pos];

    /* 1. 起點 / 銀行 */
    if (td->type == TILE_START) {
        if (is_ai_mode) {
            if (p->cash > 12000) {
                p->cash -= 5000;
                p->deposit += 5000;
            }
            snprintf(g_log_line, sizeof(g_log_line), "%s 抵達台北銀行辦理存款。", p->name);
            advance_turn();
        } else {
            g_menu_sel = 0;
            g_state = ST_BANK_MENU;
        }
        return;
    }

    /* 2. 點券格 */
    if (td->type == TILE_COUPON) {
        int gain = (pos == 3) ? 30 : 40;
        p->coupons += gain;
        char l1[128];
        snprintf(l1, sizeof(l1), "%s 踩到點券格，獲得 %d 點券！", p->name, gain);
        snprintf(g_log_line, sizeof(g_log_line), "%s", l1);
        if (is_ai_mode) {
            advance_turn();
        } else {
            show_msg("獲得點券", l1, "點券可在卡片商店購買強力卡片。", "", ST_TURN_MENU);
            g_msg_next_state = ST_GAME_OVER; /* 特殊標記：關閉訊息後換下一位 */
        }
        return;
    }

    /* 3. 卡片商店 */
    if (td->type == TILE_SHOP) {
        refresh_shop_items();
        if (is_ai_mode) {
            for (int i = 0; i < 4; i++) {
                int c = g_shop_items[i];
                if (p->coupons >= CARD_COSTS[c] && p->card_count < MAX_CARDS_HELD) {
                    p->coupons -= CARD_COSTS[c];
                    add_card(pi, (CardType)c);
                    snprintf(g_log_line, sizeof(g_log_line), "%s 在卡片商店買了 [%s]。", p->name, CARD_NAMES[c]);
                    break;
                }
            }
            advance_turn();
        } else {
            g_menu_sel = 0;
            g_state = ST_CARD_SHOP;
        }
        return;
    }

    /* 4. 醫院 / 監獄（單純路過探望） */
    if (td->type == TILE_HOSPITAL || td->type == TILE_PRISON) {
        snprintf(g_log_line, sizeof(g_log_line), "%s 路過 %s 參觀一下。", p->name, td->name);
        if (is_ai_mode) advance_turn();
        else {
            show_msg(td->name, g_log_line, "沒事發生，祝旅途平安！", "", ST_GAME_OVER);
        }
        return;
    }

    /* 5. 魔法屋 */
    if (td->type == TILE_MAGIC) {
        if (is_ai_mode) {
            int richest = pi;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!g.players[i].bankrupt && g.players[i].cash > g.players[richest].cash) richest = i;
            }
            if (richest != pi) {
                pay_money(richest, 2000, -1);
                snprintf(g_log_line, sizeof(g_log_line), "%s 施展魔法，%s 損失 $2000！", p->name, g.players[richest].name);
            } else {
                p->cash += 1500;
                snprintf(g_log_line, sizeof(g_log_line), "%s 在魔法屋獲得祝福 +$1500！", p->name);
            }
            advance_turn();
        } else {
            g_menu_sel = 0;
            g_state = ST_MAGIC_HOUSE;
        }
        return;
    }

    /* 6. 小遊戲格 */
    if (td->type == TILE_MINIGAME) {
        if (is_ai_mode) {
            int win = (rng_next(2) == 0);
            if (win) p->coupons += 30;
            snprintf(g_log_line, sizeof(g_log_line), "%s 玩幸運猜大小%s！", p->name, win ? "贏得 30 點券" : "沒猜中");
            advance_turn();
        } else {
            g_mg_bet = (p->coupons >= 20) ? 20 : 10;
            g_mg_dice = 0;
            g_mg_result[0] = '\0';
            g_menu_sel = 0;
            g_state = ST_MINIGAME;
        }
        return;
    }

    /* 7. 大新聞 / 命運事件 */
    if (td->type == TILE_NEWS) {
        int ev = rng_next(7);
        char l1[128] = {0}, l2[128] = {0};
        switch (ev) {
            case 0:
                p->cash += 2500;
                snprintf(l1, sizeof(l1), "【頭條】%s 中了大樂透貳獎！", p->name);
                snprintf(l2, sizeof(l2), "獲得獎金 $2500！");
                break;
            case 1:
                pay_money(pi, 1800, -1);
                snprintf(l1, sizeof(l1), "【頭條】政府加徵豪宅房屋稅！");
                snprintf(l2, sizeof(l2), "%s 繳納稅金 $1800。", p->name);
                break;
            case 2: {
                GodType ng = (GodType)(1 + rng_next(5));
                p->god = ng;
                p->god_turns = 4;
                snprintf(l1, sizeof(l1), "【神明降臨】%s 附身在 %s 身上！", GOD_NAMES[ng], p->name);
                snprintf(l2, sizeof(l2), "持續 4 回合，好運壞運看天命！");
                break;
            }
            case 3: {
                int s = rng_next(NUM_STOCKS);
                g.stocks[s].red_buff = 2;
                snprintf(l1, sizeof(l1), "【財經快報】%s 獲大訂單！", g.stocks[s].name);
                snprintf(l2, sizeof(l2), "連續 2 回合強勢漲停！");
                break;
            }
            case 4: {
                CardType nc = (CardType)(1 + rng_next(CARD_COUNT - 1));
                add_card(pi, nc);
                snprintf(l1, sizeof(l1), "【好康報報】%s 撿到神祕禮物！", p->name);
                snprintf(l2, sizeof(l2), "獲得卡片：[%s]", CARD_NAMES[nc]);
                break;
            }
            case 5:
                p->pos = 23; /* 監獄 */
                p->skip_turns = 2;
                p->skip_reason = 2;
                snprintf(l1, sizeof(l1), "【社會新聞】%s 違規停車遭帶回！", p->name);
                snprintf(l2, sizeof(l2), "進看守監獄反省 2 回合。");
                break;
            default:
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (!g.players[i].bankrupt) g.players[i].cash += 1000;
                }
                snprintf(l1, sizeof(l1), "【普發現金】政府發放振興紅包！");
                snprintf(l2, sizeof(l2), "全場每位玩家獲得 $1000！");
                break;
        }
        snprintf(g_log_line, sizeof(g_log_line), "%s %s", l1, l2);
        if (is_ai_mode) advance_turn();
        else show_msg("大富翁新聞台", l1, l2, "", ST_GAME_OVER);
        return;
    }

    /* 8. 小地產 / 商業用地 */
    if (td->type == TILE_LAND || td->type == TILE_COMMERCIAL) {
        /* 土地公附身：直接免費強佔對手地產！ */
        if (ts->owner >= 0 && ts->owner != pi && p->god == GOD_LAND) {
            int old_ow = ts->owner;
            ts->owner = pi;
            char l1[128];
            snprintf(l1, sizeof(l1), "土地公顯靈！免費把 %s 的 [%s] 佔為己有！", g.players[old_ow].name, td->name);
            snprintf(g_log_line, sizeof(g_log_line), "%s", l1);
            if (is_ai_mode) advance_turn();
            else show_msg("土地公附身", l1, "", "", ST_GAME_OVER);
            return;
        }

        /* 無主地 -> 買地 */
        if (ts->owner < 0) {
            int cost = (p->god == GOD_FORTUNE) ? 0 : td->base_price;
            if (is_ai_mode) {
                if (p->cash >= cost + 2000 || cost == 0) {
                    p->cash -= cost;
                    ts->owner = pi;
                    ts->level = 0;
                    if (td->type == TILE_COMMERCIAL) {
                        ts->comm = (CommType)(1 + rng_next(3));
                        snprintf(g_log_line, sizeof(g_log_line), "%s 買下 %s 並蓋了 %s！", p->name, td->name, COMM_NAMES[ts->comm]);
                    } else {
                        snprintf(g_log_line, sizeof(g_log_line), "%s 花費 $%d 買下 %s！", p->name, cost, td->name);
                    }
                }
                advance_turn();
            } else {
                if (p->cash >= cost) {
                    g_menu_sel = 0;
                    g_state = ST_BUY_LAND;
                } else {
                    char l1[128];
                    snprintf(l1, sizeof(l1), "%s 需要 $%d，你的現金不足。", td->name, cost);
                    show_msg("現金不足", l1, "可先賣股或到銀行領錢。", "", ST_GAME_OVER);
                }
            }
            return;
        }

        /* 自己的地 -> 升級 */
        if (ts->owner == pi) {
            if (ts->level < 5) {
                int up_cost = (p->god == GOD_FORTUNE) ? 0 : (td->base_price / 2);
                if (is_ai_mode) {
                    if (p->cash >= up_cost + 1500 || up_cost == 0) {
                        p->cash -= up_cost;
                        ts->level++;
                        snprintf(g_log_line, sizeof(g_log_line), "%s 將 %s 升級到 Lv.%d！", p->name, td->name, ts->level);
                    }
                    advance_turn();
                } else {
                    if (p->cash >= up_cost) {
                        g_menu_sel = 0;
                        g_state = ST_BUY_LAND;
                    } else {
                        snprintf(g_log_line, sizeof(g_log_line), "回到自己的 %s (現金不足升級)。", td->name);
                        advance_turn();
                    }
                }
            } else {
                snprintf(g_log_line, sizeof(g_log_line), "%s 的 %s 已達最高等級 Lv.5！", p->name, td->name);
                advance_turn();
            }
            return;
        }

        /* 對手的地 -> 付過路費 */
        int ow = ts->owner;
        if (td->type == TILE_COMMERCIAL && ts->comm == COMM_PARK) {
            p->coupons += 20;
            snprintf(g_log_line, sizeof(g_log_line), "%s 逛了 %s 的公園，免費獲得 20 點券！", p->name, g.players[ow].name);
            if (is_ai_mode) advance_turn();
            else show_msg("漫步公園", g_log_line, "", "", ST_GAME_OVER);
            return;
        }

        if (p->god == GOD_WEALTH) {
            snprintf(g_log_line, sizeof(g_log_line), "財神附身！%s 免付 %s 的過路費！", p->name, g.players[ow].name);
            if (is_ai_mode) advance_turn();
            else show_msg("財神保佑", g_log_line, "", "", ST_GAME_OVER);
            return;
        }

        int toll = calc_tile_toll(pos);
        if (p->god == GOD_POOR) toll *= 2;

        pay_money(pi, toll, ow);
        char l1[128], l2[128] = {0};
        snprintf(l1, sizeof(l1), "%s 踩到 %s 的 [%s]，支付過路費 $%d！",
                 p->name, g.players[ow].name, td->name, toll);

        if (td->type == TILE_COMMERCIAL && ts->comm == COMM_HOTEL && !p->bankrupt) {
            p->skip_turns = 1;
            p->skip_reason = 4;
            snprintf(l2, sizeof(l2), "入住大飯店，下回合強制休息 1 天！");
        } else if (p->bankrupt) {
            snprintf(l2, sizeof(l2), "%s 資金耗盡，宣告破產出局！！", p->name);
        } else if (p->god == GOD_POOR) {
            snprintf(l2, sizeof(l2), "(窮神附身導致過路費加倍！)");
        }
        snprintf(g_log_line, sizeof(g_log_line), "%s", l1);

        if (check_winner()) {
            g_state = ST_GAME_OVER;
            return;
        }
        if (is_ai_mode) advance_turn();
        else show_msg("支付過路費", l1, l2, "", ST_GAME_OVER);
        return;
    }

    advance_turn();
}

static void move_player_steps(int pi, int steps, int is_ai_mode) {
    Player *p = &g.players[pi];

    if (p->stay_flag) {
        p->stay_flag = 0;
        snprintf(g_log_line, sizeof(g_log_line), "%s 使用停留卡留在 [%s]！", p->name, MAP_DEFS[p->pos].name);
        handle_landing(pi, is_ai_mode);
        return;
    }

    int hit_block = 0;
    for (int s = 0; s < steps; s++) {
        p->pos = (p->pos + 1) % MAP_SIZE;
        if (p->pos == 0) {
            p->cash += 2000; /* 經過起點領薪水 */
        }
        if (g.tiles[p->pos].has_roadblock) {
            g.tiles[p->pos].has_roadblock = 0;
            hit_block = 1;
            break;
        }
    }

    if (hit_block) {
        snprintf(g_log_line, sizeof(g_log_line), "%s 撞到路障，被迫停在 [%s]！", p->name, MAP_DEFS[p->pos].name);
    } else {
        snprintf(g_log_line, sizeof(g_log_line), "%s 擲出 %d 點，來到 [%s]。", p->name, steps, MAP_DEFS[p->pos].name);
    }

    handle_landing(pi, is_ai_mode);
}

static void execute_card(int pi, int card_idx, int target, int is_ai_mode) {
    Player *p = &g.players[pi];
    CardType ct = (CardType)p->cards[card_idx];
    remove_card_at(pi, card_idx);

    char l1[128] = {0}, l2[128] = {0};

    switch (ct) {
        case CARD_REMOTE_DICE:
            p->remote_dice = (target >= 1 && target <= 6) ? target : 1;
            snprintf(l1, sizeof(l1), "%s 使用遙控骰子，鎖定步數為 %d！", p->name, p->remote_dice);
            break;
        case CARD_TURTLE:
            if (target >= 0 && target < MAX_PLAYERS && !g.players[target].bankrupt) {
                g.players[target].turtle_turns = 3;
                snprintf(l1, sizeof(l1), "%s 對 %s 使用烏龜卡！", p->name, g.players[target].name);
                snprintf(l2, sizeof(l2), "%s 接下來 3 回合只能走 1 步。", g.players[target].name);
            }
            break;
        case CARD_ROADBLOCK:
            g.tiles[p->pos].has_roadblock = 1;
            snprintf(l1, sizeof(l1), "%s 在 [%s] 放下了路障！", p->name, MAP_DEFS[p->pos].name);
            break;
        case CARD_STAY:
            p->stay_flag = 1;
            snprintf(l1, sizeof(l1), "%s 使用停留卡，本回合將停在原地！", p->name);
            break;
        case CARD_EQUAL_WEALTH: {
            int sum = 0, cnt = 0;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!g.players[i].bankrupt) { sum += g.players[i].cash; cnt++; }
            }
            int each = (cnt > 0) ? (sum / cnt) : 0;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!g.players[i].bankrupt) g.players[i].cash = each;
            }
            snprintf(l1, sizeof(l1), "%s 發動【均富卡】！", p->name);
            snprintf(l2, sizeof(l2), "全場現金重新平分，每人變成 $%d！", each);
            break;
        }
        case CARD_BUY_LAND: {
            TileState *ts = &g.tiles[p->pos];
            const TileDef *td = &MAP_DEFS[p->pos];
            if ((td->type == TILE_LAND || td->type == TILE_COMMERCIAL) && ts->owner >= 0 && ts->owner != pi) {
                int cost = td->base_price;
                if (p->cash >= cost) {
                    int old_ow = ts->owner;
                    p->cash -= cost;
                    g.players[old_ow].cash += cost;
                    ts->owner = pi;
                    snprintf(l1, sizeof(l1), "%s 強行收購了 %s 的 [%s]！", p->name, g.players[old_ow].name, td->name);
                } else {
                    snprintf(l1, sizeof(l1), "現金不足 $%d，購地卡無效！", cost);
                }
            } else {
                snprintf(l1, sizeof(l1), "腳下不是對手土地，購地卡放空了！");
            }
            break;
        }
        case CARD_MONSTER: {
            int best = -1;
            for (int i = 0; i < MAP_SIZE; i++) {
                if (g.tiles[i].owner == target && g.tiles[i].level > 0) {
                    if (best < 0 || g.tiles[i].level > g.tiles[best].level) best = i;
                }
            }
            if (best >= 0) {
                g.tiles[best].level = 0;
                snprintf(l1, sizeof(l1), "怪獸出沒！%s 的 [%s] 被夷為平地！", g.players[target].name, MAP_DEFS[best].name);
            } else {
                snprintf(l1, sizeof(l1), "%s 沒有可摧毀的房屋，怪獸回家了。", g.players[target].name);
            }
            break;
        }
        case CARD_HIBERNATE:
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (i != pi && !g.players[i].bankrupt) {
                    g.players[i].skip_turns = 2;
                    g.players[i].skip_reason = 3;
                }
            }
            snprintf(l1, sizeof(l1), "%s 使用【冬眠卡】！", p->name);
            snprintf(l2, sizeof(l2), "其他所有對手全部冬眠 2 回合！");
            break;
        case CARD_TAX:
            if (target >= 0 && target < MAX_PLAYERS && !g.players[target].bankrupt) {
                int tax = (g.players[target].deposit * 3) / 10;
                g.players[target].deposit -= tax;
                snprintf(l1, sizeof(l1), "%s 對 %s 發動查稅卡！", p->name, g.players[target].name);
                snprintf(l2, sizeof(l2), "沒收 30%% 存款共 $%d！", tax);
            }
            break;
        case CARD_RED_STOCK:
            if (target >= 0 && target < NUM_STOCKS) {
                g.stocks[target].red_buff = 2;
                snprintf(l1, sizeof(l1), "%s 對 [%s] 使用紅卡！連漲 2 天！", p->name, g.stocks[target].name);
            }
            break;
        case CARD_BLACK_STOCK:
            if (target >= 0 && target < NUM_STOCKS) {
                g.stocks[target].red_buff = -2;
                snprintf(l1, sizeof(l1), "%s 對 [%s] 使用黑卡！連跌 2 天！", p->name, g.stocks[target].name);
            }
            break;
        case CARD_MISSILE:
            if (target >= 0 && target < MAX_PLAYERS && !g.players[target].bankrupt) {
                g.players[target].pos = 9; /* 醫院 */
                g.players[target].skip_turns = 2;
                g.players[target].skip_reason = 1;
                snprintf(l1, sizeof(l1), "轟隆！%s 發射飛彈炸飛 %s！", p->name, g.players[target].name);
                snprintf(l2, sizeof(l2), "%s 被送進市立醫院休養 2 回合！", g.players[target].name);
            }
            break;
        case CARD_BANISH_GOD:
            p->god = GOD_NONE;
            p->god_turns = 0;
            snprintf(l1, sizeof(l1), "%s 使用送神卡，身上的神明離開了！", p->name);
            break;
        default:
            break;
    }

    snprintf(g_log_line, sizeof(g_log_line), "%s", l1);
    if (!is_ai_mode) {
        show_msg("使用卡片", l1, l2, "", ST_TURN_MENU);
    }
}

static void run_ai_turn(int pi) {
    Player *p = &g.players[pi];

    /* 1. AI 簡單卡片策略 */
    if (p->card_count > 0 && rng_next(3) == 0) {
        for (int i = 0; i < p->card_count; i++) {
            CardType ct = (CardType)p->cards[i];
            if (ct == CARD_BANISH_GOD && (p->god == GOD_POOR || p->god == GOD_BADLUCK)) {
                execute_card(pi, i, pi, 1);
                break;
            }
            if (ct == CARD_EQUAL_WEALTH && p->cash < 5000) {
                execute_card(pi, i, 0, 1);
                break;
            }
            if (ct == CARD_MISSILE || ct == CARD_TAX || ct == CARD_TURTLE) {
                int tgt = (pi + 1) % MAX_PLAYERS;
                for (int j = 0; j < MAX_PLAYERS; j++) {
                    if (j != pi && !g.players[j].bankrupt) { tgt = j; break; }
                }
                if (tgt != pi) {
                    execute_card(pi, i, tgt, 1);
                    break;
                }
            }
        }
    }

    /* 2. AI 簡單買賣股票 */
    if (p->cash > 18000 && rng_next(3) == 0) {
        int s = rng_next(NUM_STOCKS);
        int cost = g.stocks[s].price * 100;
        if (p->cash > cost + 8000) {
            p->cash -= cost;
            p->stocks[s] += 100;
        }
    }

    /* 3. 擲骰與移動 */
    int steps = 1 + rng_next(6);
    if (p->remote_dice > 0) {
        steps = p->remote_dice;
        p->remote_dice = 0;
    } else if (p->turtle_turns > 0) {
        steps = 1;
        p->turtle_turns--;
    }
    g_last_dice = steps;
    move_player_steps(pi, steps, 1);
}

static void advance_turn(void) {
    if (check_winner()) {
        g_state = ST_GAME_OVER;
        return;
    }

    int safety = 0;
    while (safety++ < 16) {
        int prev = g.cur_player;
        g.cur_player = (g.cur_player + 1) % MAX_PLAYERS;
        if (g.cur_player <= prev) {
            g.turn_count++;
            update_daily_economy();
        }

        Player *p = &g.players[g.cur_player];
        if (p->bankrupt) continue;

        /* 衰神每回合掉錢 */
        if (p->god == GOD_BADLUCK) {
            int drop = 500 + rng_next(500);
            pay_money(g.cur_player, drop, -1);
            if (check_winner()) { g_state = ST_GAME_OVER; return; }
        }
        if (p->god != GOD_NONE) {
            p->god_turns--;
            if (p->god_turns <= 0) p->god = GOD_NONE;
        }

        /* 跳過回合檢查 (醫院/監獄/冬眠/飯店) */
        if (p->skip_turns > 0) {
            p->skip_turns--;
            const char *reasons[5] = { "", "住院中", "坐牢中", "冬眠中", "飯店休息" };
            snprintf(g_log_line, sizeof(g_log_line), "%s %s，本回合暫停！", p->name, reasons[p->skip_reason]);
            if (!p->is_ai) {
                show_msg("暫停回合", g_log_line, "按確認繼續下一位玩家。", "", ST_GAME_OVER);
                return;
            }
            continue;
        }

        if (p->is_ai) {
            run_ai_turn(g.cur_player);
            if (g_state == ST_GAME_OVER) return;
            /* 如果 AI 剛好讓下一位人類玩家輪到，在迴圈內繼續，但為了讓玩家看得到 AI 動作，若全部 AI 跑完會自然停在人類玩家 */
            if (!g.players[g.cur_player].is_ai) return;
            return; /* run_ai_turn 結尾已經遞迴呼叫了 advance_turn */
        } else {
            g_state = ST_TURN_MENU;
            g_menu_sel = 0;
            return;
        }
    }
}

/* --- 公開介面 --- */
void game_init(void) {
    g_rng = hal_get_seed();
    if (g_rng == 0) g_rng = 0x5A3C9E11u;
    g_state = ST_TITLE;
    g_menu_sel = 0;
}

void game_shutdown(void) {
    if (g_state != ST_TITLE && g_state != ST_SETUP && g_state != ST_GAME_OVER) {
        save_game();
    }
}

/* --- 按鍵處理 --- */
void game_on_key(GameKey key) {
    /* 混入每次按鍵讓隨機數更真隨機 */
    g_rng ^= (uint32_t)key * 2654435761u;
    rng_next(7);

    if (g_state == ST_TITLE) {
        int has_sav = has_save_file();
        int max_opt = has_sav ? 3 : 2;
        if (key == GKEY_UP || key == GKEY_LEFT) {
            g_menu_sel = (g_menu_sel + max_opt - 1) % max_opt;
        } else if (key == GKEY_DOWN || key == GKEY_RIGHT) {
            g_menu_sel = (g_menu_sel + 1) % max_opt;
        } else if (key == GKEY_OK) {
            if (has_sav && g_menu_sel == 0) {
                load_game();
            } else if ((has_sav && g_menu_sel == 1) || (!has_sav && g_menu_sel == 0)) {
                g_setup_humans = 1;
                g_state = ST_SETUP;
            } else {
                hal_request_quit();
            }
        } else if (key == GKEY_BACK) {
            hal_request_quit();
        }
        return;
    }

    if (g_state == ST_SETUP) {
        if (key == GKEY_LEFT || key == GKEY_UP) {
            if (g_setup_humans > 1) g_setup_humans--;
        } else if (key == GKEY_RIGHT || key == GKEY_DOWN) {
            if (g_setup_humans < 4) g_setup_humans++;
        } else if (key == GKEY_OK) {
            start_new_game(g_setup_humans);
        } else if (key == GKEY_BACK) {
            g_state = ST_TITLE;
            g_menu_sel = 0;
        }
        return;
    }

    if (g_state == ST_MESSAGE) {
        if (key == GKEY_OK || key == GKEY_BACK) {
            if (g_msg_next_state == ST_GAME_OVER) {
                advance_turn();
            } else {
                g_state = g_msg_next_state;
            }
        }
        return;
    }

    if (g_state == ST_GAME_OVER) {
        if (key == GKEY_OK || key == GKEY_BACK) {
            g_state = ST_TITLE;
            g_menu_sel = 0;
        }
        return;
    }

    if (g_state == ST_STATUS_VIEW || g_state == ST_HELP_VIEW) {
        if (key == GKEY_OK || key == GKEY_BACK) {
            g_state = ST_TURN_MENU;
        }
        return;
    }

    /* 主回合選單 */
    if (g_state == ST_TURN_MENU) {
        Player *p = &g.players[g.cur_player];
        /* 快捷鍵 */
        if (key == GKEY_CARD) { g_menu_sel = 0; g_state = ST_CARD_BAG; return; }
        if (key == GKEY_STOCK) { g_stock_sel = 0; g_state = ST_STOCK_MARKET; return; }
        if (key == GKEY_HELP) { g_state = ST_HELP_VIEW; return; }
        if (key == GKEY_SAVE) {
            int ok = save_game();
            show_msg("存檔結果", ok ? "遊戲進度已儲存至 C:\\richman4.sav！" : "存檔失敗！", "", "", ST_TURN_MENU);
            return;
        }

        if (key == GKEY_UP || key == GKEY_LEFT) {
            g_menu_sel = (g_menu_sel + 5) % 6;
        } else if (key == GKEY_DOWN || key == GKEY_RIGHT) {
            g_menu_sel = (g_menu_sel + 1) % 6;
        } else if (key == GKEY_OK) {
            switch (g_menu_sel) {
                case 0: { /* 擲骰子 */
                    int steps = 1 + rng_next(6);
                    if (p->remote_dice > 0) {
                        steps = p->remote_dice;
                        p->remote_dice = 0;
                    } else if (p->turtle_turns > 0) {
                        steps = 1;
                        p->turtle_turns--;
                    }
                    g_last_dice = steps;
                    move_player_steps(g.cur_player, steps, 0);
                    break;
                }
                case 1: /* 卡片袋 */
                    g_menu_sel = 0;
                    g_state = ST_CARD_BAG;
                    break;
                case 2: /* 股票市場 */
                    g_stock_sel = 0;
                    g_state = ST_STOCK_MARKET;
                    break;
                case 3: /* 資產總覽 */
                    g_state = ST_STATUS_VIEW;
                    break;
                case 4: { /* 存檔 */
                    int ok = save_game();
                    show_msg("存檔結果", ok ? "進度已儲存至 C:\\richman4.sav！" : "存檔失敗！", "", "", ST_TURN_MENU);
                    break;
                }
                case 5: /* 返回標題 */
                    save_game();
                    g_state = ST_TITLE;
                    g_menu_sel = 0;
                    break;
            }
        } else if (key == GKEY_BACK) {
            g_state = ST_HELP_VIEW;
        }
        return;
    }

    /* 買地 / 升級確認 */
    if (g_state == ST_BUY_LAND) {
        Player *p = &g.players[g.cur_player];
        int pos = p->pos;
        const TileDef *td = &MAP_DEFS[pos];
        TileState *ts = &g.tiles[pos];

        if (key == GKEY_LEFT || key == GKEY_RIGHT || key == GKEY_UP || key == GKEY_DOWN) {
            g_menu_sel = 1 - g_menu_sel;
        } else if (key == GKEY_OK) {
            if (g_menu_sel == 0) {
                if (ts->owner < 0) {
                    int cost = (p->god == GOD_FORTUNE) ? 0 : td->base_price;
                    p->cash -= cost;
                    ts->owner = g.cur_player;
                    ts->level = 0;
                    if (td->type == TILE_COMMERCIAL) {
                        g_menu_sel = 0;
                        g_state = ST_BUILD_COMM;
                        return;
                    }
                    snprintf(g_log_line, sizeof(g_log_line), "%s 買下了 [%s]！", p->name, td->name);
                } else {
                    int cost = (p->god == GOD_FORTUNE) ? 0 : (td->base_price / 2);
                    p->cash -= cost;
                    ts->level++;
                    snprintf(g_log_line, sizeof(g_log_line), "%s 將 [%s] 升級為 Lv.%d！", p->name, td->name, ts->level);
                }
            }
            advance_turn();
        } else if (key == GKEY_BACK) {
            advance_turn();
        }
        return;
    }

    /* 商業用地選擇建築 */
    if (g_state == ST_BUILD_COMM) {
        Player *p = &g.players[g.cur_player];
        TileState *ts = &g.tiles[p->pos];
        if (key == GKEY_UP || key == GKEY_LEFT) g_menu_sel = (g_menu_sel + 3) % 4;
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) g_menu_sel = (g_menu_sel + 1) % 4;
        else if (key == GKEY_OK) {
            ts->comm = (CommType)(g_menu_sel + 1);
            snprintf(g_log_line, sizeof(g_log_line), "%s 興建了 [%s]！", p->name, COMM_NAMES[ts->comm]);
            advance_turn();
        }
        return;
    }

    /* 卡片袋 */
    if (g_state == ST_CARD_BAG) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_BACK || key == GKEY_CARD) {
            g_state = ST_TURN_MENU;
            g_menu_sel = 0;
            return;
        }
        if (p->card_count == 0) return;

        if (key == GKEY_UP || key == GKEY_LEFT) {
            g_menu_sel = (g_menu_sel + p->card_count - 1) % p->card_count;
        } else if (key == GKEY_DOWN || key == GKEY_RIGHT) {
            g_menu_sel = (g_menu_sel + 1) % p->card_count;
        } else if (key == GKEY_OK) {
            CardType ct = (CardType)p->cards[g_menu_sel];
            if (ct == CARD_ROADBLOCK || ct == CARD_STAY || ct == CARD_EQUAL_WEALTH ||
                ct == CARD_BUY_LAND || ct == CARD_HIBERNATE || ct == CARD_BANISH_GOD) {
                execute_card(g.cur_player, g_menu_sel, g.cur_player, 0);
            } else {
                g_pending_card_idx = g_menu_sel;
                g_pending_card_type = (int)ct;
                g_menu_sel = 0;
                g_state = ST_CARD_TARGET;
            }
        }
        return;
    }

    /* 卡片目標選擇 */
    if (g_state == ST_CARD_TARGET) {
        CardType ct = (CardType)g_pending_card_type;
        int max_t = (ct == CARD_REMOTE_DICE) ? 6 : ((ct == CARD_RED_STOCK || ct == CARD_BLACK_STOCK) ? NUM_STOCKS : MAX_PLAYERS);

        if (key == GKEY_BACK) {
            g_state = ST_CARD_BAG;
            g_menu_sel = g_pending_card_idx;
            return;
        }
        if (key == GKEY_UP || key == GKEY_LEFT) {
            g_menu_sel = (g_menu_sel + max_t - 1) % max_t;
        } else if (key == GKEY_DOWN || key == GKEY_RIGHT) {
            g_menu_sel = (g_menu_sel + 1) % max_t;
        } else if (key >= GKEY_1 && key <= GKEY_6) {
            int num = (int)(key - GKEY_1);
            if (num < max_t) {
                int tgt = (ct == CARD_REMOTE_DICE) ? (num + 1) : num;
                execute_card(g.cur_player, g_pending_card_idx, tgt, 0);
            }
        } else if (key == GKEY_OK) {
            int tgt = (ct == CARD_REMOTE_DICE) ? (g_menu_sel + 1) : g_menu_sel;
            execute_card(g.cur_player, g_pending_card_idx, tgt, 0);
        }
        return;
    }

    /* 股票市場 */
    if (g_state == ST_STOCK_MARKET) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_BACK || key == GKEY_STOCK) {
            g_state = ST_TURN_MENU;
            g_menu_sel = 0;
            return;
        }
        if (key == GKEY_UP) g_stock_sel = (g_stock_sel + NUM_STOCKS - 1) % NUM_STOCKS;
        else if (key == GKEY_DOWN) g_stock_sel = (g_stock_sel + 1) % NUM_STOCKS;
        else if (key == GKEY_RIGHT || key == GKEY_OK) {
            /* 買進 100 股 */
            int cost = g.stocks[g_stock_sel].price * 100;
            if (p->cash >= cost) {
                p->cash -= cost;
                p->stocks[g_stock_sel] += 100;
            } else if (p->deposit >= cost) {
                p->deposit -= cost;
                p->stocks[g_stock_sel] += 100;
            }
        } else if (key == GKEY_LEFT) {
            /* 賣出 100 股 */
            if (p->stocks[g_stock_sel] >= 100) {
                p->stocks[g_stock_sel] -= 100;
                p->cash += g.stocks[g_stock_sel].price * 100;
            }
        }
        return;
    }

    /* 銀行存取款 */
    if (g_state == ST_BANK_MENU) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_UP || key == GKEY_LEFT) g_menu_sel = (g_menu_sel + 3) % 4;
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) g_menu_sel = (g_menu_sel + 1) % 4;
        else if (key == GKEY_OK) {
            if (g_menu_sel == 0 && p->cash >= 2000) { p->cash -= 2000; p->deposit += 2000; }
            else if (g_menu_sel == 1 && p->deposit >= 2000) { p->deposit -= 2000; p->cash += 2000; }
            else if (g_menu_sel == 2 && p->cash >= 5000) { p->cash -= 5000; p->deposit += 5000; }
            else if (g_menu_sel == 3) { advance_turn(); }
        } else if (key == GKEY_BACK) {
            advance_turn();
        }
        return;
    }

    /* 卡片商店 */
    if (g_state == ST_CARD_SHOP) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_UP || key == GKEY_LEFT) g_menu_sel = (g_menu_sel + 4) % 5;
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) g_menu_sel = (g_menu_sel + 1) % 5;
        else if (key == GKEY_OK) {
            if (g_menu_sel == 4) {
                advance_turn();
            } else {
                int c = g_shop_items[g_menu_sel];
                if (p->coupons >= CARD_COSTS[c] && p->card_count < MAX_CARDS_HELD) {
                    p->coupons -= CARD_COSTS[c];
                    add_card(g.cur_player, (CardType)c);
                    snprintf(g_log_line, sizeof(g_log_line), "購買了 [%s]！", CARD_NAMES[c]);
                }
            }
        } else if (key == GKEY_BACK) {
            advance_turn();
        }
        return;
    }

    /* 魔法屋 */
    if (g_state == ST_MAGIC_HOUSE) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_UP || key == GKEY_LEFT) g_menu_sel = (g_menu_sel + 2) % 3;
        else if (key == GKEY_DOWN || key == GKEY_RIGHT) g_menu_sel = (g_menu_sel + 1) % 3;
        else if (key == GKEY_OK) {
            if (g_menu_sel == 0) {
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (i != g.cur_player && !g.players[i].bankrupt) {
                        pay_money(i, 1500, g.cur_player);
                    }
                }
                show_msg("魔法生效", "施展吸金術！向每位對手吸取 $1500！", "", "", ST_GAME_OVER);
            } else if (g_menu_sel == 1) {
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (i != g.cur_player && !g.players[i].bankrupt) {
                        g.players[i].pos = 9;
                        g.players[i].skip_turns = 1;
                        g.players[i].skip_reason = 1;
                    }
                }
                show_msg("魔法生效", "施展感冒魔法！所有對手送醫休息 1 回合！", "", "", ST_GAME_OVER);
            } else {
                p->coupons += 50;
                show_msg("魔法生效", "點石成券！獲得 50 點券！", "", "", ST_GAME_OVER);
            }
        } else if (key == GKEY_BACK) {
            advance_turn();
        }
        return;
    }

    /* 小遊戲：幸運猜大小 */
    if (g_state == ST_MINIGAME) {
        Player *p = &g.players[g.cur_player];
        if (key == GKEY_BACK) {
            advance_turn();
            return;
        }
        if (g_mg_dice > 0) {
            if (key == GKEY_OK) advance_turn();
            return;
        }
        if (key == GKEY_LEFT || key == GKEY_UP) g_menu_sel = (g_menu_sel + 2) % 3;
        else if (key == GKEY_RIGHT || key == GKEY_DOWN) g_menu_sel = (g_menu_sel + 1) % 3;
        else if (key == GKEY_OK) {
            if (g_menu_sel == 2) {
                advance_turn();
                return;
            }
            g_mg_dice = 1 + rng_next(6);
            int is_big = (g_mg_dice >= 4);
            int guess_big = (g_menu_sel == 0);
            if (is_big == guess_big) {
                p->coupons += 40;
                snprintf(g_mg_result, sizeof(g_mg_result), "開出 %d 點 (%s)！猜對了，獲得 +40 點券！",
                         g_mg_dice, is_big ? "大" : "小");
            } else {
                p->coupons += 10;
                snprintf(g_mg_result, sizeof(g_mg_result), "開出 %d 點 (%s)！沒猜中，安慰獎 +10 點券。",
                         g_mg_dice, is_big ? "大" : "小");
            }
        }
        return;
    }
}

/* ============================================================
 * 畫面繪製 (動態適配 800x480 或任意解析度)
 * ============================================================ */

static void get_tile_xy(int idx, int tw, int th, int *ox, int *oy) {
    if (idx < 10) {
        *ox = idx * tw;
        *oy = 0;
    } else if (idx < 14) {
        *ox = 9 * tw;
        *oy = (idx - 9) * th;
    } else if (idx < 24) {
        *ox = (9 - (idx - 14)) * tw;
        *oy = 5 * th;
    } else {
        *ox = 0;
        *oy = (4 - (idx - 24)) * th;
    }
}

static void draw_panel(int x, int y, int w, int h, uint32_t bg, uint32_t border) {
    hal_fill_rect(x, y, w, h, bg);
    hal_draw_rect(x, y, w, h, border);
}

static void render_board(int w, int h) {
    int tw = w / 10;
    int th = h / 6;

    hal_clear(0x16222A);

    /* 1. 畫外圍 28 個格子 */
    for (int i = 0; i < MAP_SIZE; i++) {
        int tx, ty;
        get_tile_xy(i, tw, th, &tx, &ty);
        const TileDef *td = &MAP_DEFS[i];
        const TileState *ts = &g.tiles[i];

        uint32_t bg = 0xF5F0E1;
        if (td->type == TILE_START) bg = 0xFFF3CD;
        else if (td->type == TILE_COMMERCIAL) bg = 0xFCE4EC;
        else if (td->type == TILE_NEWS) bg = 0xE3F2FD;
        else if (td->type == TILE_SHOP) bg = 0xE8F5E9;
        else if (td->type == TILE_MAGIC) bg = 0xF3E5F5;
        else if (td->type == TILE_HOSPITAL || td->type == TILE_PRISON) bg = 0xECEFF1;

        hal_fill_rect(tx, ty, tw - 1, th - 1, bg);
        hal_draw_rect(tx, ty, tw - 1, th - 1, 0x37474F);

        /* 頂部色條：有主顯示擁有者顏色，無主顯示地段色 */
        int bar_h = 14;
        uint32_t bar_c = (ts->owner >= 0) ? PLAYER_COLORS[ts->owner] : GROUP_COLORS[td->group];
        hal_fill_rect(tx + 1, ty + 1, tw - 3, bar_h, bar_c);

        /* 顯示等級或商業建築縮寫 */
        if (ts->owner >= 0) {
            char lvl[16];
            if (td->type == TILE_COMMERCIAL && ts->comm != COMM_NONE) {
                snprintf(lvl, sizeof(lvl), "%s", COMM_NAMES[ts->comm]);
            } else {
                snprintf(lvl, sizeof(lvl), "Lv%d", ts->level);
            }
            hal_set_text_bg(bar_c);
            hal_draw_text(tx + 4, ty, lvl, 0xFFFFFF);
        }

        /* 格子名稱 */
        hal_set_text_bg(bg);
        hal_draw_text(tx + 3, ty + bar_h + 4, td->name, 0x1A1A1A);

        /* 價格或過路費 */
        if (td->type == TILE_LAND || td->type == TILE_COMMERCIAL) {
            char pr[20];
            if (ts->owner < 0) snprintf(pr, sizeof(pr), "$%d", td->base_price);
            else snprintf(pr, sizeof(pr), "收%d", calc_tile_toll(i));
            hal_draw_text(tx + 3, ty + bar_h + 24, pr, ts->owner < 0 ? 0x555555 : 0xB71C1C);
        }

        /* 路障標記 */
        if (ts->has_roadblock) {
            hal_fill_rect(tx + tw - 22, ty + th - 20, 19, 17, 0xD32F2F);
            hal_set_text_bg(0xD32F2F);
            hal_draw_text(tx + tw - 20, ty + th - 20, "障", 0xFFFFFF);
        }

        /* 玩家棋子 */
        int px = tx + 4;
        int py = ty + th - 19;
        for (int pi = 0; pi < MAX_PLAYERS; pi++) {
            if (!g.players[pi].bankrupt && g.players[pi].pos == i) {
                hal_fill_rect(px, py, 16, 16, PLAYER_COLORS[pi]);
                hal_draw_rect(px, py, 16, 16, 0xFFFFFF);
                char num[4];
                snprintf(num, sizeof(num), "%d", pi + 1);
                hal_set_text_bg(PLAYER_COLORS[pi]);
                hal_draw_text(px + 4, py, num, 0xFFFFFF);
                px += 18;
            }
        }
    }

    /* 2. 中央儀表板區域 */
    int cx = tw + 6;
    int cy = th + 6;
    int cw = w - tw * 2 - 12;
    int ch = h - th * 2 - 12;

    draw_panel(cx, cy, cw, ch, 0x1E2D38, 0x607D8B);

    /* 頂部資訊列：日期與4位玩家精簡狀態 */
    Player *cp = &g.players[g.cur_player];
    hal_fill_rect(cx + 2, cy + 2, cw - 4, 28, PLAYER_COLORS[g.cur_player]);
    hal_set_text_bg(PLAYER_COLORS[g.cur_player]);
    char hdr[160];
    snprintf(hdr, sizeof(hdr), "第%d月%d日 | 輪到:%s 現金:$%d 存款:$%d 券:%d 神:%s",
             g.month, g.day, cp->name, cp->cash, cp->deposit, cp->coupons, GOD_NAMES[cp->god]);
    hal_draw_text(cx + 8, cy + 6, hdr, 0xFFFFFF);

    /* 底部即時事件新聞跑馬燈 */
    hal_fill_rect(cx + 2, cy + ch - 26, cw - 4, 24, 0x101920);
    hal_set_text_bg(0x101920);
    hal_draw_text(cx + 8, cy + ch - 23, g_log_line, 0xFFD54F);

    /* 3. 中央互動主視窗內容 */
    int ix = cx + 10;
    int iy = cy + 36;
    int iw = cw - 20;
    int ih = ch - 68;

    if (g_state == ST_TURN_MENU) {
        /* 左半邊：動作選單；右半邊：四位玩家資產簡表 */
        int lw = iw / 2 - 6;
        const char *opts[6] = {
            "1. 擲骰子前進",
            "2. 使用卡片 [C]",
            "3. 股票市場 [S]",
            "4. 資產總覽",
            "5. 儲存進度 [F]",
            "6. 返回主選單"
        };
        for (int i = 0; i < 6; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x2E7D32 : 0x263845;
            hal_fill_rect(ix, iy + i * 32, lw, 28, bg);
            hal_set_text_bg(bg);
            char buf[64];
            snprintf(buf, sizeof(buf), "%s %s", (i == g_menu_sel) ? ">" : " ", opts[i]);
            hal_draw_text(ix + 6, iy + i * 32 + 5, buf, 0xFFFFFF);
        }

        /* 右半邊：玩家排名簡表 */
        int rx = ix + lw + 12;
        int rw = iw - lw - 12;
        draw_panel(rx, iy, rw, ih, 0x223340, 0x455A64);
        hal_set_text_bg(0x223340);
        hal_draw_text(rx + 8, iy + 6, "【即時資產與狀態】", 0x80CBC4);
        for (int i = 0; i < MAX_PLAYERS; i++) {
            Player *pl = &g.players[i];
            int py = iy + 32 + i * 38;
            hal_fill_rect(rx + 8, py + 2, 10, 28, PLAYER_COLORS[i]);
            char line1[96], line2[96];
            if (pl->bankrupt) {
                snprintf(line1, sizeof(line1), "%s (已破產)", pl->name);
                snprintf(line2, sizeof(line2), "已退出競爭");
            } else {
                snprintf(line1, sizeof(line1), "%s%s 總資產:$%d",
                         pl->name, pl->is_ai ? "[AI]" : "", calc_player_net_worth(i));
                snprintf(line2, sizeof(line2), "現:$%d 存:$%d 券:%d 卡:%d",
                         pl->cash, pl->deposit, pl->coupons, pl->card_count);
            }
            hal_draw_text(rx + 24, py, line1, 0xFFFFFF);
            hal_draw_text(rx + 24, py + 17, line2, 0xB0BEC5);
        }
        return;
    }

    if (g_state == ST_BUY_LAND) {
        const TileDef *td = &MAP_DEFS[cp->pos];
        const TileState *ts = &g.tiles[cp->pos];
        int is_buy = (ts->owner < 0);
        int cost = (cp->god == GOD_FORTUNE) ? 0 : (is_buy ? td->base_price : td->base_price / 2);

        hal_set_text_bg(0x1E2D38);
        char l1[128], l2[128];
        snprintf(l1, sizeof(l1), is_buy ? "是否購買無主地【%s】？" : "是否升級【%s】至 Lv.%d？",
                 td->name, ts->level + 1);
        snprintf(l2, sizeof(l2), "費用：$%d  %s  (目前現金：$%d)",
                 cost, (cp->god == GOD_FORTUNE) ? "[福神免費!]" : "", cp->cash);
        hal_draw_text(ix + 10, iy + 20, l1, 0xFFD54F);
        hal_draw_text(ix + 10, iy + 52, l2, 0xFFFFFF);

        const char *yn[2] = { "確認購買 / 升級", "暫時放棄" };
        for (int i = 0; i < 2; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x2E7D32 : 0x37474F;
            hal_fill_rect(ix + 10 + i * 220, iy + 100, 200, 36, bg);
            hal_set_text_bg(bg);
            hal_draw_text(ix + 24 + i * 220, iy + 109, yn[i], 0xFFFFFF);
        }
        return;
    }

    if (g_state == ST_BUILD_COMM) {
        hal_set_text_bg(0x1E2D38);
        hal_draw_text(ix + 6, iy + 6, "請選擇要興建的大型商業建築：", 0xFFD54F);
        const char *cinfo[4] = {
            "1. 商業中心 — 超高過路費 ($1400+)",
            "2. 大飯店   — 過路費 + 強制對手住宿 1 回合",
            "3. 研究所   — 每 4 天自動免費研發 1 張卡片",
            "4. 公園     — 免費開放，路過送 20 點券"
        };
        for (int i = 0; i < 4; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x1565C0 : 0x263845;
            hal_fill_rect(ix + 6, iy + 36 + i * 36, iw - 12, 32, bg);
            hal_set_text_bg(bg);
            hal_draw_text(ix + 14, iy + 43 + i * 36, cinfo[i], 0xFFFFFF);
        }
        return;
    }

    if (g_state == ST_CARD_BAG) {
        hal_set_text_bg(0x1E2D38);
        char title[96];
        snprintf(title, sizeof(title), "【卡片袋 (%d/%d)】 Enter使用 / ESC返回", cp->card_count, MAX_CARDS_HELD);
        hal_draw_text(ix + 4, iy + 2, title, 0x80CBC4);
        if (cp->card_count == 0) {
            hal_draw_text(ix + 20, iy + 50, "目前沒有任何卡片，請到卡片商店購買！", 0xCFD8DC);
            return;
        }
        int start = (g_menu_sel >= 5) ? (g_menu_sel - 4) : 0;
        for (int i = start; i < cp->card_count && i < start + 5; i++) {
            int row = i - start;
            int ct = cp->cards[i];
            uint32_t bg = (i == g_menu_sel) ? 0x2E7D32 : 0x263845;
            hal_fill_rect(ix + 4, iy + 28 + row * 32, iw - 8, 28, bg);
            hal_set_text_bg(bg);
            char line[128];
            snprintf(line, sizeof(line), "%s %s - %s", (i == g_menu_sel) ? ">" : " ", CARD_NAMES[ct], CARD_DESCS[ct]);
            hal_draw_text(ix + 10, iy + 33 + row * 32, line, 0xFFFFFF);
        }
        return;
    }

    if (g_state == ST_CARD_TARGET) {
        CardType ct = (CardType)g_pending_card_type;
        hal_set_text_bg(0x1E2D38);
        char title[96];
        snprintf(title, sizeof(title), "請選擇【%s】的目標：(ESC取消)", CARD_NAMES[ct]);
        hal_draw_text(ix + 6, iy + 6, title, 0xFFD54F);

        if (ct == CARD_REMOTE_DICE) {
            for (int i = 0; i < 6; i++) {
                uint32_t bg = (i == g_menu_sel) ? 0xE65100 : 0x37474F;
                hal_fill_rect(ix + 10 + i * 74, iy + 60, 64, 54, bg);
                hal_set_text_bg(bg);
                char b[16];
                snprintf(b, sizeof(b), "%d 步", i + 1);
                hal_draw_text(ix + 22 + i * 74, iy + 78, b, 0xFFFFFF);
            }
        } else if (ct == CARD_RED_STOCK || ct == CARD_BLACK_STOCK) {
            for (int s = 0; s < NUM_STOCKS; s++) {
                uint32_t bg = (s == g_menu_sel) ? 0xC62828 : 0x263845;
                hal_fill_rect(ix + 6, iy + 36 + s * 34, iw - 12, 30, bg);
                hal_set_text_bg(bg);
                char b[96];
                snprintf(b, sizeof(b), "%s %s (現價 $%d)", (s == g_menu_sel) ? ">" : " ", g.stocks[s].name, g.stocks[s].price);
                hal_draw_text(ix + 14, iy + 42 + s * 34, b, 0xFFFFFF);
            }
        } else {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                uint32_t bg = (i == g_menu_sel) ? 0xAD1457 : 0x263845;
                hal_fill_rect(ix + 6, iy + 36 + i * 34, iw - 12, 30, bg);
                hal_set_text_bg(bg);
                char b[96];
                snprintf(b, sizeof(b), "%s %s (現:$%d 存:$%d)", (i == g_menu_sel) ? ">" : " ",
                         g.players[i].name, g.players[i].cash, g.players[i].deposit);
                hal_draw_text(ix + 14, iy + 42 + i * 34, b, 0xFFFFFF);
            }
        }
        return;
    }

    if (g_state == ST_STOCK_MARKET) {
        hal_set_text_bg(0x1E2D38);
        hal_draw_text(ix + 4, iy + 2, "【台北股市】 ↑↓選股  →/Enter買100股  ←賣100股  ESC返回", 0xFFD54F);
        for (int s = 0; s < NUM_STOCKS; s++) {
            Stock *st = &g.stocks[s];
            int diff = st->price - st->prev_price;
            uint32_t bg = (s == g_stock_sel) ? 0x37474F : 0x263845;
            uint32_t col = (diff > 0) ? 0xFF5252 : ((diff < 0) ? 0x69F0AE : 0xFFFFFF);
            hal_fill_rect(ix + 4, iy + 30 + s * 36, iw - 8, 32, bg);
            hal_set_text_bg(bg);
            char line[128];
            snprintf(line, sizeof(line), "%s %s  股價:$%d (%+d)  持股:%d  [100股=$%d]",
                     (s == g_stock_sel) ? ">" : " ", st->name, st->price, diff, cp->stocks[s], st->price * 100);
            hal_draw_text(ix + 10, iy + 37 + s * 36, line, col);
        }
        return;
    }

    if (g_state == ST_BANK_MENU) {
        hal_set_text_bg(0x1E2D38);
        char info[128];
        snprintf(info, sizeof(info), "【台北銀行】 現金:$%d  存款:$%d (每月配息 5%%)", cp->cash, cp->deposit);
        hal_draw_text(ix + 6, iy + 6, info, 0xFFD54F);
        const char *bopts[4] = {
            "存入 $2000", "領出 $2000", "存入 $5000", "離開銀行"
        };
        for (int i = 0; i < 4; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x2E7D32 : 0x263845;
            hal_fill_rect(ix + 6, iy + 36 + i * 34, iw - 12, 30, bg);
            hal_set_text_bg(bg);
            hal_draw_text(ix + 14, iy + 42 + i * 34, bopts[i], 0xFFFFFF);
        }
        return;
    }

    if (g_state == ST_CARD_SHOP) {
        hal_set_text_bg(0x1E2D38);
        char info[128];
        snprintf(info, sizeof(info), "【卡片商店】 目前點券：%d  卡片：%d/%d", cp->coupons, cp->card_count, MAX_CARDS_HELD);
        hal_draw_text(ix + 4, iy + 2, info, 0x80CBC4);
        for (int i = 0; i < 5; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x1565C0 : 0x263845;
            hal_fill_rect(ix + 4, iy + 28 + i * 31, iw - 8, 27, bg);
            hal_set_text_bg(bg);
            char line[128];
            if (i < 4) {
                int c = g_shop_items[i];
                snprintf(line, sizeof(line), "%s %s (%d券) - %s",
                         (i == g_menu_sel) ? ">" : " ", CARD_NAMES[c], CARD_COSTS[c], CARD_DESCS[c]);
            } else {
                snprintf(line, sizeof(line), "%s 離開商店", (i == g_menu_sel) ? ">" : " ");
            }
            hal_draw_text(ix + 10, iy + 32 + i * 31, line, 0xFFFFFF);
        }
        return;
    }

    if (g_state == ST_MAGIC_HOUSE) {
        hal_set_text_bg(0x1E2D38);
        hal_draw_text(ix + 6, iy + 6, "【神秘魔法屋】 請選擇要施展的魔法：", 0xCE93D8);
        const char *mopts[3] = {
            "1. 吸金大法 — 向每位對手吸取 $1500",
            "2. 感冒魔法 — 全體對手住院休息 1 回合",
            "3. 點石成券 — 免費獲得 50 點券"
        };
        for (int i = 0; i < 3; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x6A1B9A : 0x263845;
            hal_fill_rect(ix + 6, iy + 40 + i * 38, iw - 12, 34, bg);
            hal_set_text_bg(bg);
            hal_draw_text(ix + 14, iy + 48 + i * 38, mopts[i], 0xFFFFFF);
        }
        return;
    }

    if (g_state == ST_MINIGAME) {
        hal_set_text_bg(0x1E2D38);
        hal_draw_text(ix + 6, iy + 8, "【幸運猜大小賺點券】 猜對 +40 券 / 安慰獎 +10 券", 0xFFD54F);
        if (g_mg_dice > 0) {
            hal_draw_text(ix + 16, iy + 66, g_mg_result, 0x69F0AE);
            hal_draw_text(ix + 16, iy + 110, "按 [Enter / 空白鍵] 繼續遊戲", 0xFFFFFF);
        } else {
            const char *opts[3] = { "猜 大 (4~6點)", "猜 小 (1~3點)", "不玩離開" };
            for (int i = 0; i < 3; i++) {
                uint32_t bg = (i == g_menu_sel) ? 0xE65100 : 0x37474F;
                hal_fill_rect(ix + 10 + i * 160, iy + 70, 148, 42, bg);
                hal_set_text_bg(bg);
                hal_draw_text(ix + 20 + i * 160, iy + 82, opts[i], 0xFFFFFF);
            }
        }
        return;
    }

    if (g_state == ST_STATUS_VIEW) {
        hal_set_text_bg(0x1E2D38);
        hal_draw_text(ix + 4, iy + 2, "【全體玩家詳細資產】 (按 Enter / ESC 返回)", 0xFFD54F);
        for (int i = 0; i < MAX_PLAYERS; i++) {
            Player *pl = &g.players[i];
            int lands = 0;
            for (int t = 0; t < MAP_SIZE; t++) if (g.tiles[t].owner == i) lands++;
            int y = iy + 28 + i * 40;
            hal_fill_rect(ix + 4, y, iw - 8, 36, 0x263845);
            hal_fill_rect(ix + 4, y, 8, 36, PLAYER_COLORS[i]);
            hal_set_text_bg(0x263845);
            char b1[128], b2[128];
            snprintf(b1, sizeof(b1), "%s  總資產:$%d  地產:%d筆  神明:%s",
                     pl->name, calc_player_net_worth(i), lands, GOD_NAMES[pl->god]);
            snprintf(b2, sizeof(b2), "現金:$%d  存款:$%d  點券:%d  卡片:%d張",
                     pl->cash, pl->deposit, pl->coupons, pl->card_count);
            hal_draw_text(ix + 18, y + 2, b1, 0xFFFFFF);
            hal_draw_text(ix + 18, y + 18, b2, 0xB0BEC5);
        }
        return;
    }

    if (g_state == ST_HELP_VIEW) {
        hal_set_text_bg(0x1E2D38);
        const char *hlines[] = {
            "【大富翁 4 Fun 操作說明】 (按 Enter / ESC 返回)",
            "• 方向鍵：選擇選單/目標    Enter / 空白鍵：確認 / 擲骰",
            "• [C] 卡片袋   [S] 股市交易   [F] 立即存檔",
            "• 同色區擁有多塊地產可享連鎖過路費加成 (最高1.9倍)",
            "• 商業用地可蓋：商業中心 / 大飯店 / 研究所 / 公園",
            "• 讓所有對手破產即獲得最終勝利！"
        };
        for (int i = 0; i < 6; i++) {
            hal_draw_text(ix + 8, iy + 8 + i * 28, hlines[i], i == 0 ? 0xFFD54F : 0xECEFF1);
        }
        return;
    }

    if (g_state == ST_MESSAGE) {
        draw_panel(ix + 20, iy + 20, iw - 40, ih - 40, 0x263238, 0xFFD54F);
        hal_set_text_bg(0x263238);
        hal_draw_text(ix + 36, iy + 32, g_msg_title, 0xFFD54F);
        hal_draw_text(ix + 36, iy + 64, g_msg_line1, 0xFFFFFF);
        hal_draw_text(ix + 36, iy + 92, g_msg_line2, 0xCFD8DC);
        hal_draw_text(ix + 36, iy + 120, g_msg_line3, 0xB0BEC5);
        hal_draw_text(ix + 36, iy + ih - 50, "[按 Enter / 空白鍵 繼續]", 0x80CBC4);
        return;
    }
}

void game_render(void) {
    int w = hal_get_width();
    int h = hal_get_height();

    if (g_state == ST_TITLE) {
        hal_clear(0x102027);
        draw_panel(w / 2 - 220, 40, 440, h - 80, 0x1E303A, 0xFFD54F);
        hal_set_text_bg(0x1E303A);
        hal_draw_text(w / 2 - 140, 68, "大富翁 4 Fun — CD-952 版", 0xFFD54F);
        hal_draw_text(w / 2 - 165, 102, "完整地產 / 商業建築 / 13種卡片 / 股市 / 神明", 0xB0BEC5);

        int has_sav = has_save_file();
        const char *opts_sav[3] = { "繼續上次遊戲 (讀取存檔)", "開始新遊戲", "離開遊戲" };
        const char *opts_new[2] = { "開始新遊戲", "離開遊戲" };
        int n = has_sav ? 3 : 2;
        for (int i = 0; i < n; i++) {
            uint32_t bg = (i == g_menu_sel) ? 0x2E7D32 : 0x2C4250;
            hal_fill_rect(w / 2 - 150, 160 + i * 50, 300, 38, bg);
            hal_set_text_bg(bg);
            const char *txt = has_sav ? opts_sav[i] : opts_new[i];
            hal_draw_text(w / 2 - 130, 170 + i * 50, txt, 0xFFFFFF);
        }
        hal_set_text_bg(0x1E303A);
        hal_draw_text(w / 2 - 150, h - 74, "[↑/↓]選擇  [Enter/空白]確認  [ESC]離開", 0x80CBC4);
        return;
    }

    if (g_state == ST_SETUP) {
        hal_clear(0x102027);
        draw_panel(w / 2 - 220, 60, 440, h - 120, 0x1E303A, 0x4FC3F7);
        hal_set_text_bg(0x1E303A);
        hal_draw_text(w / 2 - 110, 90, "設定玩家人數 (總共 4 位)", 0xFFD54F);

        char b[96];
        snprintf(b, sizeof(b), "<   人類玩家：%d 位   電腦對手：%d 位   >", g_setup_humans, 4 - g_setup_humans);
        hal_fill_rect(w / 2 - 180, 155, 360, 42, 0x2E7D32);
        hal_set_text_bg(0x2E7D32);
        hal_draw_text(w / 2 - 160, 168, b, 0xFFFFFF);

        hal_set_text_bg(0x1E303A);
        hal_draw_text(w / 2 - 160, 225, "角色：P1孫小美  P2阿土伯  P3錢夫人  P4沙隆巴斯", 0xECEFF1);
        hal_draw_text(w / 2 - 150, 265, "[←/→]調整人數  [Enter/空白]出發！  [ESC]返回", 0x80CBC4);
        return;
    }

    if (g_state == ST_GAME_OVER) {
        hal_clear(0x102027);
        draw_panel(w / 2 - 210, 80, 420, 260, 0x1E303A, 0xFFD54F);
        hal_set_text_bg(0x1E303A);
        hal_draw_text(w / 2 - 70, 110, "【遊戲結束】", 0xFFD54F);
        char b[128];
        if (g.win_player >= 0) {
            snprintf(b, sizeof(b), "恭喜 %s 成為最終大富翁！！", g.players[g.win_player].name);
        } else {
            snprintf(b, sizeof(b), "比賽結束！");
        }
        hal_draw_text(w / 2 - 140, 165, b, 0x69F0AE);
        hal_draw_text(w / 2 - 110, 240, "[按 Enter / 空白鍵 返回主選單]", 0xFFFFFF);
        return;
    }

    render_board(w, h);
}
