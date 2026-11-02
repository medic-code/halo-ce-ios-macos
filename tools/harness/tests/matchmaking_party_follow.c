/* MATCHMAKING_PARTY_FOLLOW.C (test): a member's screens follow the leader's search, not while in a game
(matchmaking.c) */

#include "harness.h"
#include "enums.inc"

struct network_game_client { short state; };

static struct
{
	short role;
	boolean party_joined, party_member, party_full, following;
} matchmaking;

static struct p2p_party_status party;
static struct p2p_mm_status search;
static struct network_game_client browse, *client;
static short reopened;

static void p2p_party_get_status(struct p2p_party_status *status) { *status = party; }
static void p2p_mm_get_status(struct p2p_mm_status *status) { *status = search; }
static void matchmaking_party_name_update(void) { }
static boolean main_menu_is_loaded(void) { return TRUE; }
static struct network_game_client *global_network_game_client_get(void) { return client; }
static void *global_network_game_server_get(void) { return NULL; }
static void matchmaking_reopen(short screen_count) { reopened = screen_count; }

static short network_game_client_get_state(struct network_game_client *game_client, short *state_data)
{
	*state_data = 0;
	return game_client->state;
}

#include "under_test.inc"

static void member_of_a_party(void)
{
	memset(&party, 0, sizeof(party));
	party.count = 2;
	party.members[1].you = TRUE;
	memset(&search, 0, sizeof(search));
	search.state = P2P_MM_OFF;
	matchmaking_party_update();
	reopened = 0;
	search.state = P2P_MM_LOOKING;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("main_menu")
	{
		member_of_a_party();
		matchmaking_party_update();
		CHECK(reopened == 4, "the leader searches: the member's search screen: %d", reopened);
	}
	CASE("direct_link")
	{
		member_of_a_party();
		browse.state = _client_state_searching;
		client = &browse;
		matchmaking_party_update();
		CHECK(reopened == 4, "joined by PASTE LINK, DIRECT LINK's client kept: the search screen: %d", reopened);
	}
	CASE("in_a_game")
	{
		member_of_a_party();
		browse.state = _client_state_pregame;
		client = &browse;
		matchmaking_party_update();
		CHECK(!reopened, "in a game's lobby: stays: %d", reopened);
	}
	return 0;
}
