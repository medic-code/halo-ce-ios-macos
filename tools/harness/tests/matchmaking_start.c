/* MATCHMAKING_START.C (test): the countdown of a full small game and START NOW (matchmaking.c) */

#include "harness.h"

#define P2P_MM_BIG_TEAM 5
#define p2p_mm_size_big(size) ((size) >= P2P_MM_BIG_TEAM)

struct network_game_server;
struct network_game
{
	short player_count, maximum_players;
	struct { struct { boolean teams; } universal_variant; } variant;
};
struct player_update
{
	short team_index;
};

#define NUMBER_OF_MULTIPLAYER_TEAMS 2
#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))

enum { _matchmaking_idle, _matchmaking_hosting, _matchmaking_joining, _matchmaking_joined };
enum { SERVER_PREGAME };

static struct network_game_server *server;
static struct network_game the_game;
static struct
{
	short role, size;
	boolean player_added, start_now, public_game;
} matchmaking;
static boolean ok, paused, no_server, listed_big_game;
static long begun;
static short stopped;

boolean network_game_server_port_ok_to_countdown(struct network_game_server *unused)
{
	return ok;
}

boolean network_game_server_port_countdown_paused(struct network_game_server *unused)
{
	return paused;
}

void network_game_server_port_countdown_begin(struct network_game_server *unused, long milliseconds)
{
	begun = milliseconds;
}

void network_game_server_port_countdown_stop(struct network_game_server *unused)
{
	stopped++;
}

struct network_game_server *global_network_game_server_get(void)
{
	return no_server ? NULL : (struct network_game_server *)&the_game;
}

int p2p_joined_listed_big_game(void)
{
	return listed_big_game;
}

struct network_game *network_game_server_get_game(struct network_game_server *unused)
{
	return &the_game;
}

short network_game_server_get_state(struct network_game_server *unused, void *data)
{
	return SERVER_PREGAME;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	ok = TRUE;
	CASE("full_starts")
	{
		matchmaking_start_countdown(server, 4, 4, FALSE);
		CHECK(begun == 4999, "the countdown began with %ld ms", begun);
		CHECK(!stopped, "a full lobby stopped the countdown");
		return 0;
	}
	CASE("leave_stops")
	{
		matchmaking_start_countdown(server, 3, 4, FALSE);
		CHECK(stopped == 1, "a player leaving stopped %d countdowns", stopped);
		CHECK(!begun, "three of four counted down: %ld ms", begun);
		return 0;
	}
	CASE("start_now_holds")
	{
		matchmaking_start_countdown(server, 3, 4, TRUE);
		CHECK(!stopped, "a countdown the host started was stopped");
		CHECK(!begun, "a five-second countdown replaced it: %ld ms", begun);
		return 0;
	}
	CASE("not_ready")
	{
		ok = FALSE;
		matchmaking_start_countdown(server, 4, 4, FALSE);
		CHECK(!begun, "a countdown the server would refuse began: %ld ms", begun);
		ok = TRUE;
		paused = TRUE;
		matchmaking_start_countdown(server, 4, 4, FALSE);
		CHECK(!begun, "a paused lobby began counting: %ld ms", begun);
		CHECK(!stopped, "a paused lobby was stopped");
		return 0;
	}
	CASE("big_never_counts_down")
	{
		matchmaking.size = P2P_MM_BIG_TEAM;
		matchmaking_start_countdown(server, 16, 16, FALSE);
		CHECK(!begun, "a full big game counted down: %ld ms", begun);
		CHECK(stopped == 1, "a countdown a client asked for ran on: %d stops", stopped);
		matchmaking_start_countdown(server, 6, 16, TRUE);
		CHECK(stopped == 1, "the host's START NOW was stopped");
		return 0;
	}
	CASE("start_now_while_part_full")
	{
		matchmaking.role = _matchmaking_hosting;
		matchmaking.player_added = TRUE;
		the_game.player_count = 3;
		the_game.maximum_players = 4;
		CHECK(matchmaking_can_start_now(), "a part-full small game has no START NOW");
		the_game.player_count = 4;
		CHECK(!matchmaking_can_start_now(), "a full small game, starting by itself, has START NOW");
		return 0;
	}
	CASE("start_now_needs_the_server")
	{
		matchmaking.role = _matchmaking_hosting;
		matchmaking.player_added = TRUE;
		the_game.player_count = 2;
		the_game.maximum_players = 16;
		ok = FALSE;
		CHECK(!matchmaking_can_start_now(), "START NOW shown while the server would ignore it");
		ok = TRUE;
		CHECK(matchmaking_can_start_now(), "START NOW hidden once the server would start");
		return 0;
	}
	CASE("big_full_keeps_start_now")
	{
		matchmaking.role = _matchmaking_hosting;
		matchmaking.player_added = TRUE;
		matchmaking.size = P2P_MM_BIG_TEAM;
		the_game.player_count = the_game.maximum_players = 16;
		CHECK(matchmaking_can_start_now(), "a full big game, which only START NOW starts, has none");
		return 0;
	}
	CASE("lobby_locks")
	{
		static struct { short role, size; boolean public_game, locked, teams_locked; } const lobbies[] = {
			{ _matchmaking_idle, P2P_MM_BIG_TEAM, FALSE, FALSE, FALSE },
			{ _matchmaking_hosting, 1, FALSE, TRUE, TRUE },
			{ _matchmaking_joined, 1, FALSE, TRUE, TRUE },
			{ _matchmaking_joined, 1, TRUE, FALSE, FALSE },
			{ _matchmaking_hosting, P2P_MM_BIG_TEAM, FALSE, TRUE, FALSE },
			{ _matchmaking_joined, P2P_MM_BIG_TEAM, TRUE, FALSE, FALSE },
			{ _matchmaking_joined, P2P_MM_BIG_TEAM + 1, FALSE, TRUE, FALSE },
		};
		short index;

		for (index = 0; index < 7; index++)
		{
			matchmaking.role = lobbies[index].role;
			matchmaking.size = lobbies[index].size;
			matchmaking.public_game = lobbies[index].public_game;
			CHECK(!matchmaking_game_locked() == !lobbies[index].locked, "lobby %d: locked", index);
			CHECK(!matchmaking_teams_locked() == !lobbies[index].teams_locked, "lobby %d: teams locked", index);
		}
		return 0;
	}
	CASE("browser_joiner_locks")
	{
		static struct { boolean no_server, listed_big_game, locked; } const lobbies[] = {
			{ TRUE, TRUE, TRUE },
			{ TRUE, FALSE, FALSE },
			{ FALSE, TRUE, FALSE },
		};
		short index;

		the_game.variant.universal_variant.teams = TRUE;
		matchmaking.role = _matchmaking_idle;
		matchmaking.size = 1;
		for (index = 0; index < 3; index++)
		{
			no_server = lobbies[index].no_server;
			listed_big_game = lobbies[index].listed_big_game;
			CHECK(!matchmaking_game_locked() == !lobbies[index].locked, "lobby %d: locked", index);
			CHECK(team_button_shown(&the_game), "lobby %d: SWITCH TEAM hidden", index);
		}
		return 0;
	}
	CASE("big_teams_stay_open")
	{
		static struct { short role, size; boolean public_game, open; } const lobbies[] = {
			{ _matchmaking_hosting, P2P_MM_BIG_TEAM, FALSE, TRUE },
			{ _matchmaking_joined, P2P_MM_BIG_TEAM, FALSE, TRUE },
			{ _matchmaking_joined, P2P_MM_BIG_TEAM, TRUE, TRUE },
			{ _matchmaking_hosting, 1, FALSE, FALSE },
			{ _matchmaking_joined, 1, FALSE, FALSE },
		};
		struct player_update player = { 1 };
		short index;

		the_game.variant.universal_variant.teams = TRUE;
		for (index = 0; index < 5; index++)
		{
			matchmaking.role = lobbies[index].role;
			matchmaking.size = lobbies[index].size;
			matchmaking.public_game = lobbies[index].public_game;
			CHECK(!team_button_shown(&the_game) == !lobbies[index].open, "lobby %d: SWITCH TEAM shown", index);
			CHECK(!host_keeps_team(&the_game, player) == !!lobbies[index].open, "lobby %d: the host keeps the team", index);
		}
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
