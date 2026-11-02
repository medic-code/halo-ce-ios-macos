/* MATCHMAKING.H: the game side of a MATCHMAKING search (matchmaking.c) */

#ifndef __MATCHMAKING_H
#define __MATCHMAKING_H

#include "cseries.h"

void matchmaking_update(float seconds);
boolean matchmaking_game_locked(void);
boolean matchmaking_teams_locked(void);
boolean matchmaking_can_start_now(void);
boolean matchmaking_hosts_big_game(void);
void matchmaking_start_now(void);

#endif
