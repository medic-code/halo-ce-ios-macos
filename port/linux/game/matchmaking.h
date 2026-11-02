/* MATCHMAKING.H: the game side of a MATCHMAKING search (matchmaking.c) */

#ifndef __MATCHMAKING_H
#define __MATCHMAKING_H

#include "cseries.h"

void matchmaking_update(float seconds);
void matchmaking_party_name_update(void);
boolean matchmaking_game_locked(void);
struct network_game;
/* the team a machine's new player takes in a matchmade game with parties, NONE for the smaller team */
long matchmaking_team(
	struct network_game const *game,
	short machine_index);
boolean matchmaking_can_start_now(void);
void matchmaking_start_now(void);

#endif
