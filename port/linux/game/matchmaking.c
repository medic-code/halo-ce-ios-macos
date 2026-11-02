/* MATCHMAKING.C: the game side of a MATCHMAKING search, carried out each frame */

#include "cseries.h"
#include "main/main.h"
#include "interface/player_ui.h"
#include "interface/ui_widget.h"
#include "interface/ui_widget_definitions.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "networking/network_connection.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"
#include "game/game_engine.h"
#include "map_share.h"
#include "matchmaking.h"

#include "../src/p2p.h"

#include <stdlib.h>

/* one under 5000: a countdown message rounds down, so 4999 reads 4 and the lobby's line adds 1 */
#define MATCHMAKING_START_MILLISECONDS 4999
#define MATCHMAKING_JOIN_SECONDS 25.0f
/* MATCH FOUND stays up this long however fast the join */
#define MATCHMAKING_FOUND_SECONDS 2.0f

/* the server's setup clears a map set sooner */
#define MATCHMAKING_MAP_SECONDS 1.0f
#define MATCHMAKING_PLAYER_SECONDS 2.0f
/* menu_functions.c's SOUND_ERROR */
#define MATCHMAKING_SOUND_ERROR 4

char const *pc_menus_screen(char const *name);
unsigned long system_milliseconds(void);
boolean main_menu_is_loaded(void);
boolean pc_menu_player1_profile(void);
boolean pc_menu_multiplayer_player(short controller);
wchar_t const *pc_menu_game_name(void);
short pc_menu_maximum_players(void);
void platform_log(char const *format, ...);

/* at most a pool's units and the party joining, each of two players or more */
#define MATCHMAKING_MOST_WAITING 9
/* a host that has started refuses a move, and one that balances undoes it */
#define MATCHMAKING_MOST_TEAM_ASKS 3

struct team_party
{
	long seats, arrived, team;
};

static char const *const slayer_small_maps[] = { "chillout", "prisoner", "wizard", "longest", "carousel", "damnation", NULL };
static char const *const slayer_maps[] = { "beavercreek", "chillout", "damnation", "hangemhigh", "carousel", "ratrace", "putput", "prisoner", NULL };
static char const *const ctf_small_maps[] = { "chillout", "wizard", "longest", "carousel", NULL };
static char const *const ctf_maps[] = { "beavercreek", "damnation", "hangemhigh", "carousel", "putput", NULL };
static char const *const king_small_maps[] = { "chillout", "prisoner", "wizard", "carousel", NULL };
static char const *const king_maps[] = { "beavercreek", "chillout", "hangemhigh", "carousel", "ratrace", NULL };
static char const *const oddball_small_maps[] = { "chillout", "prisoner", "wizard", "longest", NULL };
static char const *const oddball_maps[] = { "beavercreek", "damnation", "hangemhigh", "carousel", "ratrace", NULL };
static char const *const race_maps[] = { "bloodgulch", "sidewinder", NULL };
static char const *const big_maps[] = { "bloodgulch", "sidewinder", "boardingaction", NULL };

static const struct
{
	char const *team_variant, *ffa_variant;
	char const *const *small_maps, *const *maps, *const *big_maps;
} playlists[NUMBER_OF_P2P_MM_PLAYLISTS] = {
	[P2P_MM_SLAYER] = { "team_slayer", "slayer", slayer_small_maps, slayer_maps, big_maps },
	[P2P_MM_CTF] = { "ctf", NULL, ctf_small_maps, ctf_maps, big_maps },
	[P2P_MM_KING] = { "team_king", "king", king_small_maps, king_maps, race_maps },
	[P2P_MM_ODDBALL] = { "team_oddball", "oddball", oddball_small_maps, oddball_maps, race_maps },
	[P2P_MM_RACE] = { "team_race", "race", race_maps, race_maps, race_maps },
};

enum
{
	_matchmaking_idle,
	_matchmaking_hosting,
	_matchmaking_joining,
	_matchmaking_joined,
};

static struct
{
	short role;
	float seconds;
	short playlist, size;
	char map[64];
	boolean map_set, player_added, start_now;
	boolean public_game;
	boolean reopen, party_stopped;
	boolean party_joined, party_member, party_full, following;
	float team_seconds;
	int team_asks;
	byte host[6];
} matchmaking;

boolean matchmaking_game_locked(void)
{
	return (matchmaking.role != _matchmaking_idle && !matchmaking.public_game) ||
		(!global_network_game_server_get() && p2p_joined_listed_big_game());
}

boolean matchmaking_teams_locked(void)
{
	return matchmaking.role != _matchmaking_idle && !matchmaking.public_game && !p2p_mm_size_big(matchmaking.size);
}

static void matchmaking_start_countdown(struct network_game_server *server, short players, short most_players, boolean start_now)
{
	if (start_now)
		return;
	if (players < most_players || p2p_mm_size_big(matchmaking.size))
		network_game_server_port_countdown_stop(server);
	else if (network_game_server_port_ok_to_countdown(server) && !network_game_server_port_countdown_paused(server))
		network_game_server_port_countdown_begin(server, MATCHMAKING_START_MILLISECONDS);
}

/* copies of network_server_manager.c's and network_client_manager.c's private state enums: keep the values in step */
enum { SERVER_PREGAME };

enum
{
	_client_state_searching,
	_client_state_joining,
	_client_state_pregame,
};

/* the screens behind are history only: loading the main menu runs
main_menu_initialize, which ends the client's game */
static void matchmaking_open_lobby(void)
{
	char const *const behind[] = {
		"pc\\main_menu\\main_menu",
		"pc\\main_menu\\multiplayer_type_select\\multiplayer_type_select_screen",
	};
	short index;

	ui_widgets_close_all();
	for (index = 0; index < NUMBEROF(behind); index++)
	{
		ui_widget_load_by_name_or_tag(
			pc_menus_screen("ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen"),
			NONE, NULL, NONE, tag_loaded(UI_WIDGET_DEFINITION_TAG, behind[index]), NONE, NONE);
	}
}

static void matchmaking_begin_host(struct p2p_mm_decision const *decision)
{
	boolean big = p2p_mm_size_big(decision->size);
	char const *const *maps = big ? playlists[decision->playlist].big_maps :
		decision->size <= P2P_MM_2V2 ? playlists[decision->playlist].small_maps : playlists[decision->playlist].maps;
	short most_players = MAX((short)p2p_mm_size_players(decision->size), big ? pc_menu_maximum_players() : 0);
	short count = 0;
	char const *map;

	while (maps[count])
		count++;
	map = maps[((unsigned long)rand() ^ system_milliseconds()) % (unsigned long)count];
	snprintf(matchmaking.map, sizeof(matchmaking.map), "levels\\test\\%s\\%s", map, map);
	matchmaking.playlist = (short)decision->playlist;
	matchmaking.size = (short)decision->size;
	pc_menu_player1_profile();
	player_ui_fast_setup_network_server();
	if (!global_network_game_server_get())
	{
		p2p_mm_host_gave_up();
		ui_play_audio_feedback_sound(MATCHMAKING_SOUND_ERROR);
		return;
	}
	/* before p2p_set_hosting_public: a limit set later is not the one listed */
	network_game_server_port_set_settings(big ? pc_menu_game_name() : NULL, most_players);
	p2p_set_hosting_allowed(TRUE);
	p2p_set_hosting_password(NULL);
	p2p_set_hosting_public(big);
	matchmaking_open_lobby();
	matchmaking.role = _matchmaking_hosting;
	matchmaking.public_game = FALSE;
	matchmaking.seconds = 0.0f;
	matchmaking.map_set = matchmaking.player_added = matchmaking.start_now = FALSE;
}

static boolean matchmaking_host_gave_up(float seconds, short players)
{
	return !players && seconds >= MATCHMAKING_PLAYER_SECONDS + P2P_MM_HOST_WAIT_SECONDS;
}

static void matchmaking_end_host(void)
{
	network_game_server_port_set_settings(NULL, 0);
	matchmaking.role = _matchmaking_idle;
}

static int red_players(
	struct network_game const *game)
{
	long index;
	int count = 0;

	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
		count += network_player_is_valid(&game->players[index]) && game->players[index].team_index == 0;
	return count;
}

static void matchmaking_update_host(float seconds)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game_client *client = global_network_game_client_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	short state;

	if (!game)
	{
		p2p_mm_cancel();
		matchmaking_end_host();
		return;
	}
	state = network_game_server_get_state(server, NULL);
	matchmaking.seconds += seconds;
	if (state == SERVER_PREGAME)
	{
		if (!matchmaking.map_set && matchmaking.seconds >= MATCHMAKING_MAP_SECONDS)
		{
			struct game_variant variant;
			char const *name = matchmaking.size == P2P_MM_FFA_6 || matchmaking.size == P2P_MM_BIG_FFA ?
				playlists[matchmaking.playlist].ffa_variant : playlists[matchmaking.playlist].team_variant;

			if (!name)
			{
				p2p_mm_cancel();
				matchmaking_end_host();
				return;
			}
			network_game_server_change_map_name(server, matchmaking.map);
			player_ui_set_game_variant(game_engine_get_variant_by_name(&variant, name));
			network_game_server_change_game_variant(server, &variant);
			matchmaking.map_set = TRUE;
		}
		if (!matchmaking.player_added && client && matchmaking.seconds >= MATCHMAKING_PLAYER_SECONDS)
			matchmaking.player_added = network_game_client_add_player(client, 0);
	}
	/* (a joiner would read "0 of 4 players" in a record before it) */
	if (game->player_count)
		/* a machine joining a game that is over waits out the whole join */
		p2p_mm_game(game_engine_running() && !game_engine_can_score() ? game->maximum_players : game->player_count,
			red_players(game), game->maximum_players, state != SERVER_PREGAME);
	else if (matchmaking_host_gave_up(matchmaking.seconds, game->player_count))
	{
		p2p_mm_host_gave_up();
		matchmaking_end_host();
		dispose_global_network_game_client();
		dispose_global_network_game_server();
		matchmaking.reopen = TRUE;
		return;
	}
	if (state != SERVER_PREGAME)
		matchmaking.start_now = matchmaking.map_set = FALSE;
	else if (matchmaking.player_added)
		matchmaking_start_countdown(server, game->player_count, game->maximum_players, matchmaking.start_now);
}

static long machine_party(
	short machine_index,
	struct p2p_mm_team_party const *parties,
	long party_count,
	unsigned char const *own_key_hash)
{
	byte identifier[6];
	unsigned char hash[P2P_PLAYER_KEY_HASH_SIZE];
	long party, member;

	if (machine_index == network_game_client_get_local_machine_index())
		csmemcpy(hash, own_key_hash, sizeof(hash));
	else if (!p2p_peer_key_hash(network_order(network_game_server_machine_address(machine_index)), identifier, hash))
		return NONE;
	for (party = 0; party < party_count; party++)
	{
		for (member = 0; member < parties[party].players; member++)
		{
			if (!csmemcmp(parties[party].key_hashes[member], hash, sizeof(hash)))
				return party;
		}
	}
	return NONE;
}

/* parties split so the teams come closest: taking the largest in turn can
leave a host's solo with no team that fits */
static long party_team(
	long *seats,
	struct team_party *parties,
	long party_count,
	long joining)
{
	long waiting[MATCHMAKING_MOST_WAITING];
	long waiting_count = 0, best_split = 0, best_gap = NONE;
	long index, split;

	for (index = 0; index < party_count; index++)
	{
		if (parties[index].team != NONE)
			seats[parties[index].team] += parties[index].seats;
		else if (parties[index].seats && waiting_count < MATCHMAKING_MOST_WAITING)
			waiting[waiting_count++] = index;
	}
	for (split = 0; split < 1L << waiting_count; split++)
	{
		long gap = seats[0] - seats[1];

		for (index = 0; index < waiting_count; index++)
			gap += split >> index & 1 ? -parties[waiting[index]].seats : parties[waiting[index]].seats;
		if (best_gap == NONE || labs(gap) < best_gap)
		{
			best_gap = labs(gap);
			best_split = split;
		}
	}
	for (index = 0; index < waiting_count; index++)
	{
		parties[waiting[index]].team = best_split >> index & 1;
		seats[parties[waiting[index]].team] += parties[waiting[index]].seats;
	}
	return joining != NONE ? parties[joining].team : seats[1] < seats[0] ? 1 : 0;
}

long matchmaking_team(
	struct network_game const *game,
	short machine_index)
{
	struct p2p_mm_team_party table[P2P_MM_TEAM_PARTIES];
	struct team_party team_parties[P2P_MM_TEAM_PARTIES];
	unsigned char own_key_hash[P2P_PLAYER_KEY_HASH_SIZE];
	long seats[2] = { 0, 0 };
	long party_count, joining, index;

	if (matchmaking.role != _matchmaking_hosting || matchmaking.public_game)
		return NONE;
	party_count = p2p_mm_team_parties(table, own_key_hash);
	if (!party_count)
		return NONE;
	joining = machine_party(machine_index, table, party_count, own_key_hash);
	for (index = 0; index < party_count; index++)
	{
		team_parties[index].seats = table[index].reserved || index == joining ? table[index].players : 0;
		team_parties[index].arrived = 0;
		team_parties[index].team = NONE;
	}
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		struct network_player const *other_player = &game->players[index];
		long party, team;

		if (!network_player_is_valid(other_player) ||
			!VALID_INDEX(other_player->team_index, 2))
		{
			continue;
		}
		team = other_player->team_index;
		party = machine_party(other_player->machine_index, table, party_count, own_key_hash);
		if (party == NONE)
		{
			seats[team]++;
			continue;
		}
		team_parties[party].team = team;
		if (team_parties[party].seats < ++team_parties[party].arrived)
			team_parties[party].seats = team_parties[party].arrived;
	}
	return party_team(seats, team_parties, party_count, joining);
}

boolean matchmaking_can_start_now(void)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;

	return game && matchmaking.role == _matchmaking_hosting && matchmaking.player_added && !matchmaking.start_now &&
		network_game_server_get_state(server, NULL) == SERVER_PREGAME && network_game_server_port_ok_to_countdown(server) &&
		(p2p_mm_size_big(matchmaking.size) || game->player_count < game->maximum_players);
}

boolean matchmaking_hosts_big_game(void)
{
	return matchmaking.role == _matchmaking_hosting && p2p_mm_size_big(matchmaking.size);
}

void matchmaking_start_now(void)
{
	matchmaking.start_now = TRUE;
	network_game_client_request_immediate_start();
}

static void matchmaking_begin_join(struct p2p_mm_decision const *decision)
{
	dispose_global_network_game_client();
	dispose_global_network_game_server();
	network_game_client_port_connection_ended();
	if (!create_global_network_game_client())
	{
		p2p_mm_joined(0);
		return;
	}
	game_connection_set(_game_connection_network_client);
	if (!pc_menu_multiplayer_player(0) || !p2p_invite_host(decision->invite, matchmaking.host) || !p2p_join_invite(decision->invite))
	{
		dispose_global_network_game_client();
		p2p_mm_joined(0);
		return;
	}
	matchmaking.role = _matchmaking_joining;
	matchmaking.public_game = decision->public_game != 0;
	matchmaking.size = (short)decision->size;
	matchmaking.team_asks = 0;
	matchmaking.seconds = 0.0f;
}

static void matchmaking_update_join(float seconds)
{
	struct network_game_client *client = global_network_game_client_get();
	short state_data;
	int stage;

	matchmaking.seconds += seconds;
	p2p_join_progress(&stage, NULL, 0);
	if (client && network_game_client_get_state(client, &state_data) == _client_state_pregame)
	{
		if (matchmaking.seconds < MATCHMAKING_FOUND_SECONDS)
			return;
		matchmaking_open_lobby();
		p2p_mm_joined(1);
		matchmaking.role = _matchmaking_joined;
		return;
	}
	network_game_client_port_join_host(matchmaking.host);
	if (!client || matchmaking.seconds >= MATCHMAKING_JOIN_SECONDS || stage == P2P_JOIN_FAILED)
	{
		dispose_global_network_game_client();
		p2p_mm_joined(0);
		matchmaking.role = _matchmaking_idle;
	}
}

/* screen_count: through MATCHMAKING 3, through its search screen 4 */
static void matchmaking_reopen(short screen_count)
{
	static char const *const screens[] = {
		"pc\\main_menu\\main_menu",
		"pc\\main_menu\\multiplayer_type_select\\multiplayer_type_select_screen",
		"pc\\main_menu\\multiplayer_type_select\\matchmaking\\matchmaking_screen",
		"pc\\main_menu\\multiplayer_type_select\\matchmaking\\search_screen",
	};
	long previous = NONE;
	short index;

	ui_widgets_close_all();
	for (index = 0; index < screen_count; index++)
	{
		if (!ui_widget_load_by_name_or_tag(screens[index], NONE, NULL, NONE, previous, NONE, NONE))
			return;
		previous = tag_loaded(UI_WIDGET_DEFINITION_TAG, screens[index]);
	}
}

static boolean player_named(
	wchar_t const *wide,
	char const *name)
{
	char text[P2P_PARTY_NAME_SIZE + 1];

	matchmaking_party_name_utf8(wide, text, sizeof(text));
	return !strcmp(text, name);
}

/* the team to ask for: NONE once on the leader's, or with no leader in sight */
static long leader_team(
	struct network_game const *game,
	long machine_index,
	char const *leader_name)
{
	long index, own_team = NONE, team = NONE;

	if (!game->variant.universal_variant.teams)
		return NONE;
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		struct network_player const *player = &game->players[index];

		if (!network_player_is_valid(player))
			continue;
		if (player->machine_index == machine_index)
			own_team = player->team_index;
		else if (player_named(player->name, leader_name))
			team = player->team_index;
	}
	return team != own_team ? team : NONE;
}

/* the host of a public game knows no party, so a member moves itself */
static void matchmaking_follow_leader(
	float seconds)
{
	struct p2p_party_status party;
	long index, team;

	matchmaking.team_seconds += seconds;
	if (matchmaking.team_seconds < 1.0f || (!matchmaking.public_game && !p2p_mm_size_big(matchmaking.size)) ||
		matchmaking.team_asks >= MATCHMAKING_MOST_TEAM_ASKS)
		return;
	matchmaking.team_seconds = 0.0f;
	p2p_party_get_status(&party);
	for (index = 0; index < party.count; index++)
	{
		if (party.members[index].leader && !party.members[index].you)
		{
			team = leader_team(network_game_client_get_game(global_network_game_client_get()),
				network_game_client_get_local_machine_index(), party.members[index].name);
			if (team != NONE)
			{
				matchmaking.team_asks++;
				network_game_client_set_team((char)team);
			}
		}
	}
}

static void matchmaking_update_joined(
	float seconds)
{
	if (network_game_client_port_connection_ended())
	{
		p2p_mm_game_lost();
		matchmaking.reopen = TRUE;
	}
	else if (global_network_game_client_get())
	{
		matchmaking_follow_leader(seconds);
		return;
	}
	else
		p2p_mm_cancel();
	matchmaking.role = _matchmaking_idle;
}

static void matchmaking_leave_game(void)
{
	if (matchmaking.role == _matchmaking_hosting)
		matchmaking_end_host();
	dispose_global_network_game_client();
	dispose_global_network_game_server();
	matchmaking.role = _matchmaking_idle;
	matchmaking.reopen = TRUE;
}

/* not the client DIRECT LINK and the server browser keep to list games */
static boolean matchmaking_client_in_game(void)
{
	struct network_game_client *client = global_network_game_client_get();
	short state_data;

	return client && network_game_client_get_state(client, &state_data) != _client_state_searching;
}

/* a member's screens follow the leader's search; a joiner's follow the party, or tell of its being full */
static void matchmaking_party_update(void)
{
	struct p2p_mm_status status;
	struct p2p_party_status party;
	boolean member, full, following;

	p2p_party_get_status(&party);
	if (party.count && !matchmaking.party_joined)
		matchmaking_party_name_update();
	matchmaking.party_joined = party.count > 0;
	if (matchmaking.role == _matchmaking_joined)
		matchmaking.following = FALSE;
	if (matchmaking.role != _matchmaking_idle || !main_menu_is_loaded() || matchmaking_client_in_game() ||
		global_network_game_server_get())
	{
		return;
	}
	p2p_mm_get_status(&status);
	member = party.count && !party.members[0].you;
	full = party.notice == P2P_PARTY_NOTICE_FULL;
	following = member && status.state != P2P_MM_OFF;
	if (following && !matchmaking.following)
		matchmaking_reopen(4);
	else if (member != matchmaking.party_member || (full && !matchmaking.party_full) || (matchmaking.following && !following))
		matchmaking_reopen(3);
	matchmaking.party_member = member;
	matchmaking.party_full = full;
	matchmaking.following = following;
}

void matchmaking_update(float seconds)
{
	struct p2p_mm_status status;
	struct p2p_mm_decision decision;

	if (p2p_mm_take_left_game())
		matchmaking_leave_game();
	p2p_mm_get_status(&status);
	/* the search is off while joined: a joined game must not end here */
	if (status.state == P2P_MM_OFF && (matchmaking.role == _matchmaking_hosting || matchmaking.role == _matchmaking_joining))
	{
		if (matchmaking.role == _matchmaking_hosting)
			matchmaking_end_host();
		else
			dispose_global_network_game_client();
		matchmaking.role = _matchmaking_idle;
	}
	switch (matchmaking.role)
	{
	case _matchmaking_hosting:
		matchmaking_update_host(seconds);
		break;
	case _matchmaking_joining:
		matchmaking_update_join(seconds);
		break;
	case _matchmaking_joined:
		matchmaking_update_joined(seconds);
		break;
	}
	if (status.notice >= P2P_MM_NOTICE_TOO_BIG && !matchmaking.party_stopped && !matchmaking_client_in_game())
		matchmaking.reopen = TRUE;
	matchmaking.party_stopped = status.notice >= P2P_MM_NOTICE_TOO_BIG;
	if (matchmaking.reopen && main_menu_is_loaded())
	{
		/* p2p_mm_game_lost has restarted the search since the read above */
		p2p_mm_get_status(&status);
		matchmaking.reopen = FALSE;
		if (status.state != P2P_MM_OFF || status.notice >= P2P_MM_NOTICE_TOO_BIG)
			matchmaking_reopen(4);
		else if (p2p_party_is_member())
			matchmaking_reopen(3);
	}
	matchmaking_party_update();
	if ((matchmaking.role == _matchmaking_idle || matchmaking.role == _matchmaking_hosting) &&
		status.state != P2P_MM_OFF && main_menu_is_loaded())
	{
		p2p_mm_take_decision(&decision);
		if (decision.kind == P2P_MM_DECIDE_HOST && matchmaking.role == _matchmaking_idle)
			matchmaking_begin_host(&decision);
		else if (decision.kind == P2P_MM_DECIDE_JOIN)
		{
			if (matchmaking.role == _matchmaking_hosting)
				matchmaking_end_host();
			matchmaking_begin_join(&decision);
		}
	}
}
