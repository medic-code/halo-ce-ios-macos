#include "harness.h"
#include "enums.inc"
/* MATCHMAKING_PICK.C (test): step 1's pick of a public game (p2p_lobby.c) */
#include "under_test.inc"

static struct p2p_listing games[16];
static int count;

static void add(const char *name, int engine, int players, int most, int ping, int dedicated)
{
	struct p2p_listing *game = &games[count++];

	memset(game, 0, sizeof(*game));
	snprintf(game->name, sizeof(game->name), "%s", name);
	snprintf(game->invite, sizeof(game->invite), "halo://join/%s", name);
	game->engine_type = (unsigned char)engine;
	game->player_count = (unsigned char)players;
	game->maximum_player_count = (unsigned char)most;
	game->has_teams = 1;
	game->open = 1;
	game->ping = (short)ping;
	game->dedicated = game->master = (unsigned char)dedicated;
	game->network_version = HALO_PORT_NETWORK_VERSION;
}

static int wanted_players = 4, wanted_teams = 1, wanted_party;

static const char *picked(void)
{
	struct p2p_lobby_wanted wanted = { 1u << 2, wanted_players, wanted_teams, wanted_party };
	int index = p2p_lobby_pick(games, count, &wanted);

	return index < 0 ? "none" : games[index].name;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("e1_fits")
	{
		count = 0;
		add("slayer34", 2, 3, 4, 40, 0);
		CHECK(!strcmp(picked(), "slayer34"), "E1: %s", picked());
	}
	CASE("e2_most_players")
	{
		count = 0;
		add("two", 2, 2, 4, 120, 0);
		add("three", 2, 3, 4, 180, 0);
		CHECK(!strcmp(picked(), "three"), "E2: most players first: %s", picked());
		count = 0;
		add("slow", 2, 3, 4, 150, 0);
		add("quick", 2, 3, 4, 60, 0);
		CHECK(!strcmp(picked(), "quick"), "then the lowest ping: %s", picked());
	}
	CASE("e3_passed_over")
	{
		count = 0;
		add("full", 2, 4, 4, 40, 0);
		add("far", 2, 3, 4, 240, 0);
		add("at_limit", 2, 3, 4, 200, 0);
		add("unpinged", 2, 3, 4, -1, 0);
		add("password", 2, 3, 4, 40, 0);
		games[count - 1].locked = 1;
		add("failed", 2, 3, 4, 40, 0);
		games[count - 1].failed = 1;
		add("empty", 2, 0, 4, 40, 0);
		add("ctf", 1, 3, 4, 40, 0);
		add("eight", 2, 3, 8, 40, 0);
		add("ffa", 2, 3, 4, 40, 0);
		games[count - 1].has_teams = 0;
		add("old", 2, 3, 4, 40, 0);
		games[count - 1].network_version = HALO_PORT_NETWORK_VERSION - 1;
		CHECK(!strcmp(picked(), "none"), "E3: every one passed over: %s", picked());
	}
	CASE("e51_e56_dedicated")
	{
		count = 0;
		/* (pings given, so that only the dedicated rule passes them over) */
		add("dedicated16", 2, 3, 16, 30, 1);
		add("dedicated4", 2, 3, 4, 30, 1);
		CHECK(!strcmp(picked(), "none"), "E51: small sizes take no dedicated server");
		count = 0;
		add("brokers_only", 2, 3, 4, 50, 0);
		CHECK(!strcmp(picked(), "brokers_only"), "E56: a server the brokers alone list is a player's game");
	}
	CASE("e28_room")
	{
		CHECK(!game_has_room(2, 1, 4, 1, 2), "E28: 1 and 1: passed over");
		CHECK(game_has_room(2, 2, 4, 1, 2), "E28: 2 on red: blue has room");
		CHECK(game_has_room(3, 1, 6, 1, 2), "or 1 and 2 of 6: red has");
		CHECK(!game_has_room(2, 0, 4, 1, 2), "no red count hides the short team");
		CHECK(game_has_room(2, 0, 4, 1, 1), "a solo fits without a red count");
		CHECK(game_has_room(0, 0, 4, 1, 2), "no players, no team to be short");
		CHECK(game_has_room(2, 1, 4, 1, 1), "a solo fits 1 and 1");
		CHECK(!game_has_room(3, 3, 4, 1, 2), "no more than the game holds");
		CHECK(game_has_room(4, 0, 6, 0, 2), "free for all: 2 free");
		CHECK(!game_has_room(5, 0, 6, 0, 2), "free for all: 1 free");
		CHECK(!game_has_room(4, 2, 4, 1, 1), "a full game has none");
	}
	CASE("public_room")
	{
		wanted_party = 2;
		count = 0;
		add("three_free", 2, 1, 4, 40, 0);
		CHECK(!strcmp(picked(), "three_free"), "a party of 2 takes 3 free: %s", picked());
		count = 0;
		add("two_free", 2, 2, 4, 40, 0);
		CHECK(!strcmp(picked(), "none"), "the free slots may be 1 and 1: %s", picked());
		wanted_party = 4;
		wanted_players = 8;
		count = 0;
		add("seven_free", 2, 1, 8, 40, 0);
		CHECK(!strcmp(picked(), "seven_free"), "a party of 4 takes 7 free: %s", picked());
		count = 0;
		add("six_free", 2, 2, 8, 40, 0);
		CHECK(!strcmp(picked(), "none"), "and not 6: %s", picked());
		wanted_party = 2;
		wanted_players = 6;
		wanted_teams = 0;
		count = 0;
		add("ffa_two", 2, 4, 6, 40, 0);
		games[count - 1].has_teams = 0;
		CHECK(!strcmp(picked(), "ffa_two"), "free for all takes N free: %s", picked());
		count = 0;
		add("ffa_one", 2, 5, 6, 40, 0);
		games[count - 1].has_teams = 0;
		CHECK(!strcmp(picked(), "none"), "and not N-1: %s", picked());
		wanted_party = 0;
		wanted_players = 4;
		wanted_teams = 1;
		count = 0;
		add("solo", 2, 3, 4, 40, 0);
		CHECK(!strcmp(picked(), "solo"), "a solo takes the last slot: %s", picked());
	}
	CASE("in_progress")
	{
		count = 0;
		add("under_way", 2, 1, 4, 40, 0);
		games[count - 1].in_progress = 1;
		wanted_party = 2;
		CHECK(!strcmp(picked(), "none"), "a party cannot ask for a team in a game in progress: %s", picked());
		wanted_party = 1;
		CHECK(!strcmp(picked(), "under_way"), "a solo may still join: %s", picked());
	}
	return 0;
}
