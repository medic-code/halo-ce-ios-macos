/* MATCHMAKING_HOST_FAILED.C (test): a game that could not be hosted is said, and the search goes on (matchmaking.c) */

#include <stdio.h>
#include <stdlib.h>
#include "harness.h"
#include "enums.inc"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static char const *const maps[] = { "bloodgulch", NULL };
static const struct
{
	char const *const *small_maps, *const *maps, *const *big_maps;
} playlists[NUMBER_OF_P2P_MM_PLAYLISTS] = { [P2P_MM_SLAYER] = { maps, maps, maps } };

static struct
{
	short role;
	float seconds;
	short playlist, size;
	char map[64];
	boolean map_set, player_added, start_now;
	boolean public_game;
} matchmaking;

static void *server;
static int gave_up, cancelled, sound = -1, lobbies;

static int p2p_mm_size_big(int size) { return size >= P2P_MM_BIG_TEAM; }
static int p2p_mm_size_players(int size) { (void)size; return 4; }
static short pc_menu_maximum_players(void) { return 16; }
static unsigned long system_milliseconds(void) { return 0; }
static void pc_menu_player1_profile(void) { }
static void player_ui_fast_setup_network_server(void) { }
static void *global_network_game_server_get(void) { return server; }
static void p2p_mm_host_gave_up(void) { gave_up++; }
static void p2p_mm_cancel(void) { cancelled++; }
static void ui_play_audio_feedback_sound(short audio_feedback) { sound = audio_feedback; }
static void network_game_server_port_set_settings(char const *name, short most) { (void)name; (void)most; }
static void p2p_set_hosting_allowed(int allowed) { (void)allowed; }
static void p2p_set_hosting_password(char const *password) { (void)password; }
static void p2p_set_hosting_public(int listed) { (void)listed; }
static char const *pc_menu_game_name(void) { return "Chief"; }
static void matchmaking_open_lobby(void) { lobbies++; }

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	struct p2p_mm_decision decision = { 0, P2P_MM_SLAYER, P2P_MM_2V2, 0, "" };

	CASE("failed")
	{
		matchmaking_begin_host(&decision);
		CHECK(gave_up == 1 && !cancelled, "back in the queue, told: %d given up, %d cancelled", gave_up, cancelled);
		CHECK(sound == MATCHMAKING_SOUND_ERROR, "the error sound: %d", sound);
		CHECK(matchmaking.role == _matchmaking_idle && !lobbies, "no lobby");
	}
	CASE("hosted")
	{
		server = &decision;
		matchmaking_begin_host(&decision);
		CHECK(!gave_up && sound < 0 && matchmaking.role == _matchmaking_hosting && lobbies == 1, "hosted: the lobby");
	}
	return 0;
}
