/* MATCHMAKING_FOUND_HOLD.C (test): MATCH FOUND shows however fast the join (matchmaking.c) */

#include "harness.h"
#include "enums.inc"

struct network_game_client { short state; };

static struct
{
	short role;
	float seconds;
	byte host[6];
} matchmaking;

static struct network_game_client joined = { _client_state_pregame };
static int lobbies, results;

static struct network_game_client *global_network_game_client_get(void) { return &joined; }
static int p2p_join_progress(int *stage, char *text, int size) { (void)text; (void)size; *stage = 0; return 0; }
static void matchmaking_open_lobby(void) { lobbies++; }
static void p2p_mm_joined(int success) { results += success; }
static boolean network_game_client_port_join_host(byte const *host) { (void)host; return TRUE; }
static void dispose_global_network_game_client(void) { }

static short network_game_client_get_state(struct network_game_client *client, short *state_data)
{
	*state_data = 0;
	return client->state;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("fast_join")
	{
		matchmaking.role = _matchmaking_joining;
		matchmaking_update_join(0.5f);
		CHECK(!lobbies && matchmaking.role == _matchmaking_joining, "joined at 0.5 s: MATCH FOUND still up");
		matchmaking_update_join(MATCHMAKING_FOUND_SECONDS);
		CHECK(lobbies == 1 && results == 1 && matchmaking.role == _matchmaking_joined, "then the lobby: %d", lobbies);
	}
	return 0;
}
