/* MATCHMAKING_ROWS.C (test): the MATCHMAKING screen's EXTENDED SEARCH, MAX PING and REGION rows (menu_functions.c) */

#include "harness.h"
#include "under_test.inc"

static boolean same(char const *a, char const *b)
{
	return a && b && !strcmp(a, b);
}

static char const *ping_shown(long setting)
{
	return ping_strings[matchmaking_ping_row(p2p_mm_max_ping(setting))];
}

static char const *region_shown(char const *setting)
{
	return region_strings[matchmaking_region_row(setting)];
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("ping_snaps")
	{
		CHECK(same(ping_shown(175), "200 MS"), "E6: 175 shows 200 MS");
		CHECK(same(ping_shown(-1), "ANY"), "E6: -1 shows ANY");
		CHECK(same(ping_shown(5000), "ANY"), "E6: 5000 shows ANY");
		CHECK(same(ping_shown(0), "ANY"), "0 shows ANY");
		CHECK(same(ping_shown(100), "100 MS") && same(ping_shown(300), "300 MS"), "the limits show themselves");
		CHECK(same(ping_strings[matchmaking_ping_row(0)], "ANY"), "a leader's ANY shows ANY");
	}
	CASE("region_rows")
	{
		CHECK(same(region_shown("EU"), "EU"), "E6: EU shows EU");
		CHECK(same(region_shown("uk"), "AUTO"), "E6: uk shows AUTO");
		CHECK(same(region_shown("eu/#"), "AUTO"), "E6: eu/# shows AUTO");
		CHECK(same(region_shown(""), "AUTO"), "empty shows AUTO");
		CHECK(same(region_shown("Any"), "ANY"), "any shows ANY");
		CHECK(same(region_shown("oce"), "OCE") && same(region_shown("na-west"), "NA-WEST"), "names show themselves");
	}
	CASE("defaults")
	{
		CHECK(same(DEFAULT_EXTENDED, "false"), "E12: DEFAULTS sets OFF");
		CHECK(same(ping_shown(atol(DEFAULT_MAX_PING)), "200 MS"), "E12: DEFAULTS sets 200 MS");
		CHECK(matchmaking_region_row(DEFAULT_REGION) == 0, "E12: DEFAULTS sets AUTO");
	}
	CASE("member_rows")
	{
		struct p2p_mm_choice choice = { 0 };
		short region;

		for (region = 0; region < MATCHMAKING_REGION_ROWS; region++)
		{
			memset(&choice, 0, sizeof(choice));
			matchmaking_choice_rows(&choice, TRUE, 1, region);
			CHECK(matchmaking_region_of_choice(&choice) == region, "E3: the member shows the leader's region row %d",
				region);
			CHECK(matchmaking_ping_row(choice.max_ping) == 1 && choice.extended, "E3: and its ping and EXTENDED");
		}
		memset(&choice, 0, sizeof(choice));
		choice.region_auto = 1;
		snprintf(choice.region, sizeof(choice.region), "na-east");
		CHECK(same(region_strings[matchmaking_region_of_choice(&choice)], "AUTO"),
			"E3: the leader's AUTO shows AUTO alone, its region resolved");
		choice.region[0] = 0;
		CHECK(same(region_strings[matchmaking_region_of_choice(&choice)], "AUTO"),
			"a leader's AUTO with no clock region is AUTO, not ANY");
	}
	CASE("locked")
	{
		struct p2p_party_status party;

		memset(&party, 0, sizeof(party));
		CHECK(!matchmaking_rows_locked(&party), "alone, not searching: the rows step");
		party.search_state = P2P_MM_OFF + 1;
		CHECK(matchmaking_rows_locked(&party), "searching: the rows are locked");
		party.search_state = P2P_MM_OFF;
		party.count = 2;
		party.members[1].you = 1;
		CHECK(matchmaking_rows_locked(&party), "a member: the rows are locked");
		party.members[0].you = 1;
		party.members[1].you = 0;
		CHECK(!matchmaking_rows_locked(&party), "the leader: the rows step");
	}
	CASE("help")
	{
		short row;

		CHECK(NUMBEROF(help_texts) == _matchmaking_help_none + 1, "the help list matches the enum: %d",
			(int)NUMBEROF(help_texts));
		CHECK(same(help_texts[_matchmaking_help_playlist + P2P_MM_SLAYER],
			"Slayer games: the most kills wins. You play whichever YES playlist fills first."), "E10: playlist help");
		CHECK(same(help_raw[_matchmaking_help_extended + P2P_MM_1V1], "OFF: only 1V1 games.\nON: after 0:45, other small sizes too.")
			&& same(help_raw[_matchmaking_help_extended + P2P_MM_4V4], "OFF: only 4V4 games.\nON: after 0:45, other small sizes too.")
			&& same(help_raw[_matchmaking_help_extended + P2P_MM_FFA_6],
			"OFF: only FFA 6 games.\nON: after 0:45, other small sizes too."), "E10: EXTENDED SEARCH help, small sizes");
		CHECK(same(help_raw[_matchmaking_help_extended + P2P_MM_BIG_TEAM],
			"Big games always stay their size.\nEXTENDED SEARCH is for small sizes.") &&
			same(help_raw[_matchmaking_help_extended + P2P_MM_BIG_FFA],
			"Big games always stay their size.\nEXTENDED SEARCH is for small sizes."), "big sizes never widen: their help says so");
		CHECK(same(help_raw[_matchmaking_help_ping],
			"Most ping to a player's game.\nServers are not timed.\nANY takes any game that answers."), "E10: MAX PING help");
		CHECK(same(help_raw[matchmaking_region_help(0, "eu")], "Region steers big games.\nAUTO picks EU from your clock."),
			"E11: REGION help at AUTO");
		CHECK(same(help_raw[matchmaking_region_help(0, "")], "Region steers big games.\nAUTO: no region from your clock."),
			"E11: REGION help at AUTO with no clock region");
		for (row = 1; row < MATCHMAKING_REGION_ROWS; row++)
			CHECK(same(help_raw[matchmaking_region_help(row, "eu")], "Region steers big games."),
				"E12: REGION help at %s", region_strings[row]);
	}
	CASE("spinners")
	{
		static char const *const rows[] = { "AUTO", "EU", "NA-EAST", "NA-WEST", "OCE", "ANY" };
		short row;

		CHECK(NUMBEROF(region_strings) == MATCHMAKING_REGION_ROWS, "the region spinner's strings are its rows");
		CHECK(NUMBEROF(ping_strings) == NUMBEROF(matchmaking_ping_limits), "the ping spinner's strings");
		for (row = 0; row < MATCHMAKING_REGION_ROWS; row++)
			CHECK(same(region_strings[row], rows[row]) && same(region_strings[matchmaking_region_row(
				matchmaking_region_settings[row])], rows[row]), "the region spinner shows %s, AUTO alone", rows[row]);
		CHECK(_matchmaking_row_region + 1 == MATCHMAKING_ROWS && _matchmaking_row_extended == 6,
			"the rows follow the playlists");
	}
	return 0;
}
