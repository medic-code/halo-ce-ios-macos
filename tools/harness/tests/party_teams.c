#include "harness.h"
#include "enums.inc"
/* PARTY_TEAMS.C (test): a matchmade host's team rule (matchmaking.c), with every join order of each game */

enum { _matchmaking_idle, _matchmaking_hosting };
static struct { int role; boolean public_game; } matchmaking = { _matchmaking_hosting, FALSE };

#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))
struct network_player { int valid; char machine_index, team_index; };
struct network_game { struct network_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS]; };

static struct network_game game;
static struct p2p_mm_team_party table[P2P_MM_TEAM_PARTIES];
static int table_count, own_machine, unknown_machine = NONE;

static boolean network_player_is_valid(struct network_player const *player)
{
	return player->valid;
}

static unsigned long network_game_server_machine_address(long machine_index)
{
	return 0x64400001UL + (unsigned long)machine_index;
}

static short network_game_client_get_local_machine_index(void)
{
	return (short)own_machine;
}

static unsigned long network_order(unsigned long value)
{
	return value;
}

static void hash_of(int machine, unsigned char *hash)
{
	memset(hash, 0, P2P_PLAYER_KEY_HASH_SIZE);
	hash[0] = (unsigned char)(machine + 1);
}

static int p2p_peer_key_hash(unsigned long address, unsigned char *identifier, unsigned char *hash)
{
	int machine = (int)(address - 0x64400001UL);

	(void)identifier;
	if (machine == unknown_machine || machine == own_machine)
		return 0;
	hash_of(machine, hash);
	return 1;
}

static int p2p_mm_team_parties(struct p2p_mm_team_party *parties, unsigned char *own_key_hash)
{
	memcpy(parties, table, sizeof(table));
	hash_of(own_machine, own_key_hash);
	return table_count;
}

#include "under_test.inc"

static void add_party(int reserved, int players, const int *machines)
{
	struct p2p_mm_team_party *party = &table[table_count++];
	int index;

	memset(party, 0, sizeof(*party));
	party->players = players;
	party->reserved = reserved;
	for (index = 0; index < players; index++)
		hash_of(machines[index], party->key_hashes[index]);
}

static int team_of_machine(int machine)
{
	int index;

	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		if (game.players[index].valid && game.players[index].machine_index == machine)
			return game.players[index].team_index;
	}
	return NONE;
}

/* the fall back in network_game_server_add_player_to_game: the smaller team */
static void add_player(int machine)
{
	long team = matchmaking_team(&game, (short)machine);
	int counts[2] = { 0, 0 }, index, free_slot = NONE;

	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		if (game.players[index].valid)
			counts[(int)game.players[index].team_index]++;
		else if (free_slot == NONE)
			free_slot = index;
	}
	if (team == NONE)
		team = counts[1] < counts[0] ? 1 : 0;
	game.players[free_slot].valid = TRUE;
	game.players[free_slot].machine_index = (char)machine;
	game.players[free_slot].team_index = (char)team;
}

static void clear_game(void)
{
	memset(&game, 0, sizeof(game));
}

static void clear_all(void)
{
	clear_game();
	table_count = 0;
	own_machine = 0;
	unknown_machine = NONE;
	matchmaking.role = _matchmaking_hosting;
	matchmaking.public_game = FALSE;
}

static int team_count(int team)
{
	int index, count = 0;

	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
		count += game.players[index].valid && game.players[index].team_index == team;
	return count;
}

static void party_whole(const int *machines, int players, const int *order, int count)
{
	int index;

	for (index = 1; index < players; index++)
	{
		CHECK(team_of_machine(machines[index]) == team_of_machine(machines[0]),
			"order %d %d %d %d: machine %d on %d, machine %d on %d", order[0], order[1], order[2], order[count - 1],
			machines[0], team_of_machine(machines[0]), machines[index], team_of_machine(machines[index]));
	}
}

/* each order of joining, the first machine in it the host */
static void each_order(int *order, int at, int count, void (*check)(const int *order))
{
	int index, swap;

	if (at == count)
	{
		check(order);
		return;
	}
	for (index = at; index < count; index++)
	{
		swap = order[at], order[at] = order[index], order[index] = swap;
		each_order(order, at + 1, count, check);
		swap = order[at], order[at] = order[index], order[index] = swap;
	}
}

static void play_order(const int *order, int count)
{
	int index;

	clear_game();
	own_machine = order[0];
	for (index = 0; index < count; index++)
		add_player(order[index]);
}

static const int chief_rook[2] = { 0, 1 };

static void check_e21(const int *order)
{
	play_order(order, 4);
	party_whole(chief_rook, 2, order, 4);
	CHECK(team_count(0) == 2 && team_count(1) == 2, "2 on each team: %d and %d", team_count(0), team_count(1));
}

static void check_host_team(const int *order)
{
	if (order[0] > 1)
		return;
	play_order(order, 4);
	party_whole(chief_rook, 2, order, 4);
	CHECK(team_of_machine(0) == team_of_machine(order[0]) && team_count(0) == 2,
		"the party on its host's team: %d of %d", team_of_machine(0), team_of_machine(order[0]));
}

static const int party_b[2] = { 0, 1 }, party_c[2] = { 2, 3 }, party_a[3] = { 4, 5, 6 };

static void check_largest(const int *order)
{
	play_order(order, 8);
	party_whole(party_a, 3, order, 8);
	party_whole(party_b, 2, order, 8);
	party_whole(party_c, 2, order, 8);
	CHECK(team_count(0) == 4 && team_count(1) == 4, "4 on each team: %d and %d", team_count(0), team_count(1));
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	int order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

	CASE("e21_whole")
	{
		clear_all();
		add_party(1, 2, chief_rook);
		each_order(order, 0, 4, check_e21);
	}
	CASE("e27_leader_hosts")
	{
		clear_all();
		add_party(1, 2, chief_rook);
		each_order(order, 0, 4, check_host_team);
	}
	CASE("closest_split")
	{
		clear_all();
		add_party(1, 2, party_b);
		add_party(1, 2, party_c);
		add_party(1, 3, party_a);
		each_order(order, 0, 8, check_largest);
	}
	CASE("e28_short_team")
	{
		clear_all();
		own_machine = 2;
		add_party(0, 2, chief_rook);
		add_player(2);
		add_player(3);
		game.players[1].team_index = 0;
		add_player(0);
		CHECK(team_of_machine(0) == 1, "the leader on the short team: %d", team_of_machine(0));
		add_player(1);
		CHECK(team_of_machine(1) == 1, "its member with it: %d", team_of_machine(1));
	}
	CASE("member_team_held")
	{
		clear_all();
		own_machine = 2;
		add_party(0, 2, chief_rook);
		add_player(2);
		add_player(0);
		game.players[0].team_index = 1;
		add_player(1);
		CHECK(team_of_machine(1) == team_of_machine(0), "a member takes its leader's team, not the short one: %d",
			team_of_machine(1));
	}
	CASE("joiner_counts")
	{
		static const int party_of_three[3] = { 3, 4, 5 };

		clear_all();
		own_machine = 2;
		add_party(0, 2, chief_rook);
		add_party(1, 3, party_of_three);
		add_player(2);
		add_player(0);
		CHECK(team_of_machine(0) == team_of_machine(2), "the party joining counts all its seats: %d and %d",
			team_of_machine(0), team_of_machine(2));
	}
	CASE("seats_lapse")
	{
		clear_all();
		own_machine = 2;
		add_party(0, 2, chief_rook);
		add_player(2);
		add_player(3);
		CHECK(team_of_machine(2) != team_of_machine(3), "a party not come holds nothing: %d and %d",
			team_of_machine(2), team_of_machine(3));
	}
	CASE("other_games")
	{
		clear_all();
		add_party(1, 2, chief_rook);
		matchmaking.public_game = TRUE;
		CHECK(matchmaking_team(&game, 0) == NONE, "a public game keeps its rule");
		matchmaking.public_game = FALSE;
		matchmaking.role = _matchmaking_idle;
		CHECK(matchmaking_team(&game, 0) == NONE, "as does any game not matchmade");
		matchmaking.role = _matchmaking_hosting;
		table_count = 0;
		CHECK(matchmaking_team(&game, 0) == NONE, "no parties, no change");
	}
	CASE("unknown_machine")
	{
		clear_all();
		own_machine = 2;
		add_party(0, 2, chief_rook);
		unknown_machine = 1;
		add_player(2);
		add_player(0);
		CHECK(team_of_machine(0) == 1, "the leader on the short team: %d", team_of_machine(0));
		CHECK(matchmaking_team(&game, 1) == 0, "a machine with no key is a solo: %ld", matchmaking_team(&game, 1));
	}
	return 0;
}
