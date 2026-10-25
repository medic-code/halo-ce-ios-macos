/*
SOCIAL_GAMES.C (test)

Social Games' pick (menu_functions.c's social_games_pick), from
test_social_games.py's listing.inc and under_test.inc: the games of the
playlists chosen, those that cannot be joined or have no ping passed over,
the most players then the lowest ping, the ping limit, and none that fits.
*/

#include "harness.h"

#define NUMBEROF(array) (sizeof(array) / sizeof(array[0]))

#include "listing.inc"
#include "under_test.inc"

/* the listing's engine_type */
enum { NO_ENGINE, CTF, SLAYER, ODDBALL, KING, RACE };

static struct p2p_listing games[16];
static short count;

static void game(char const *name, int engine, int teams, int players, int ping)
{
	struct p2p_listing *listing = &games[count++];

	memset(listing, 0, sizeof(*listing));
	snprintf(listing->name, sizeof(listing->name), "%s", name);
	listing->engine_type = (unsigned char)engine;
	listing->has_teams = (unsigned char)teams;
	listing->player_count = (unsigned char)players;
	listing->maximum_player_count = 16;
	listing->open = 1;
	listing->ping = (short)ping;
}

static struct social_games_playlists playlists(int ctf, int slayer, int oddball, int king, int race, int team_games,
	int free_for_all)
{
	struct social_games_playlists chosen = { { FALSE, ctf, slayer, oddball, king, race }, team_games, free_for_all };

	return chosen;
}

static struct social_games_playlists const every = { { FALSE, TRUE, TRUE, TRUE, TRUE, TRUE }, TRUE, TRUE };
static short fitting, untimed;

/* the name of the game picked, "-" for none */
static char const *picked(struct social_games_playlists chosen, int ping_limit)
{
	short index = social_games_pick(games, count, &chosen, ping_limit, &fitting, &untimed);

	return index == NONE ? "-" : games[index].name;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("playlists")
	{
		game("ffa slayer", SLAYER, 0, 3, 40);
		game("team slayer", SLAYER, 1, 2, 40);
		game("ctf", CTF, 1, 6, 40);
		game("oddball", ODDBALL, 0, 5, 40);
		game("king", KING, 0, 4, 40);
		game("race", RACE, 1, 7, 40);
		game("co-op", NO_ENGINE, 0, 9, 10);
		CHECK(!strcmp(picked(playlists(0, 1, 0, 0, 0, 1, 1), 200), "ffa slayer") && fitting == 2,
			"SLAYER: the slayer games only, the most players first (%d fit)", fitting);
		CHECK(!strcmp(picked(playlists(1, 0, 0, 0, 0, 1, 1), 200), "ctf") && fitting == 1, "CTF");
		CHECK(!strcmp(picked(playlists(0, 0, 1, 0, 0, 1, 1), 200), "oddball"), "ODDBALL");
		CHECK(!strcmp(picked(playlists(0, 0, 0, 1, 0, 1, 1), 200), "king"), "KING OF THE HILL");
		CHECK(!strcmp(picked(playlists(0, 0, 0, 0, 1, 1, 1), 200), "race"), "RACE");
		CHECK(!strcmp(picked(playlists(0, 1, 0, 0, 0, 1, 0), 200), "team slayer"),
			"SLAYER and TEAM GAMES only: team slayer");
		CHECK(!strcmp(picked(playlists(0, 1, 0, 0, 0, 0, 1), 200), "ffa slayer"),
			"SLAYER and FREE FOR ALL only: free-for-all slayer");
		CHECK(!strcmp(picked(playlists(1, 1, 1, 1, 1, 0, 1), 200), "oddball") && fitting == 3,
			"FREE FOR ALL only: no team game, nor co-op (%d fit)", fitting);
		CHECK(!strcmp(picked(every, 200), "race") && fitting == 6, "every playlist: every game but co-op");
		CHECK(!strcmp(picked(playlists(1, 1, 1, 1, 1, 0, 0), 200), "-"), "no kind of game: none");
	}
	else CASE("skipped")
	{
		game("joinable", SLAYER, 0, 2, 90);
		game("full", SLAYER, 0, 16, 10);
		game("closed", SLAYER, 0, 9, 10);
		games[count - 1].open = 0;
		game("password", SLAYER, 0, 9, 10);
		games[count - 1].locked = 1;
		game("failed", SLAYER, 0, 9, 10);
		games[count - 1].failed = 1;
		game("not timed", SLAYER, 0, 9, -1);
		CHECK(!strcmp(picked(every, 200), "joinable") && fitting == 1,
			"full, closed, password, failed and untimed games passed over (%s, %d fit)", picked(every, 200), fitting);
		CHECK(untimed == 1, "the game with no ping yet counted as one that would fit (%d)", untimed);
	}
	else CASE("order")
	{
		game("near, few", SLAYER, 0, 1, 15);
		game("far, many", SLAYER, 0, 5, 150);
		game("mid, many", SLAYER, 0, 5, 90);
		game("near, many", SLAYER, 0, 5, 90);
		game("empty", SLAYER, 0, 0, 1);
		CHECK(!strcmp(picked(every, 200), "mid, many") && fitting == 5,
			"the most players first, then the lowest ping, then the first shown (%s)", picked(every, 200));
		count = 0;
		game("empty, near", SLAYER, 0, 0, 5);
		game("empty, far", SLAYER, 0, 0, 60);
		CHECK(!strcmp(picked(every, 200), "empty, near"), "empty games: the lowest ping");
	}
	else CASE("limit")
	{
		game("crowded, at the limit", SLAYER, 0, 8, 200);
		game("crowded, at FILTERS' limit", SLAYER, 0, 7, 100);
		game("under", SLAYER, 0, 2, 99);
		CHECK(!strcmp(picked(every, 200), "crowded, at FILTERS' limit") && fitting == 2,
			"a ping of the limit does not fit (%s)", picked(every, 200));
		CHECK(!strcmp(picked(every, 100), "under") && fitting == 1, "FILTERS' PING < 100");
		CHECK(!strcmp(picked(every, 201), "crowded, at the limit"), "under a higher limit, the most players");
	}
	else CASE("none")
	{
		CHECK(!strcmp(picked(every, 200), "-") && fitting == 0 && untimed == 0, "no games: none");
		game("ctf", CTF, 1, 4, 30);
		game("far", SLAYER, 0, 4, 400);
		game("not timed", SLAYER, 0, 4, -1);
		game("being timed", SLAYER, 0, 4, -1);
		CHECK(!strcmp(picked(playlists(0, 1, 0, 0, 0, 1, 1), 200), "-") && fitting == 0,
			"no slayer game near enough: none");
		CHECK(untimed == 2, "two would fit once timed (%d)", untimed);
	}
	else return 2;
	return 0;
}
