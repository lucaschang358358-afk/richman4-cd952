#ifndef GAME_H
#define GAME_H

#include "hal.h"

void game_init(void);
void game_on_key(GameKey key);
void game_tick(int dt_ms);
int  game_needs_redraw(void);
void game_render(void);
void game_shutdown(void);

#endif
