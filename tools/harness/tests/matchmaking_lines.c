/* MATCHMAKING_LINES.C (test): the search screens' words (menu_functions.c) */

#include <wchar.h>
#include "harness.h"
#include "enums.inc"
#define p2p_mm_size_big(size) ((size) >= P2P_MM_BIG_TEAM)
#include "under_test.inc"

static wchar_t title[96], count[96], help[128], notice[96], line[128];
static const struct p2p_party_status no_party;

static void lines_of(const struct p2p_mm_status *status, const struct p2p_party_status *party)
{
	matchmaking_lines(status, party, title, count, help, notice, 96);
}

static struct p2p_party_status party_of(int you, int players)
{
	static char const *const names[] = { "Chief", "Rook", "Kat", "Jun", "Emile" };
	struct p2p_party_status party;
	int index;

	memset(&party, 0, sizeof(party));
	party.count = players;
	for (index = 0; index < players; index++)
	{
		strcpy(party.members[index].name, names[index]);
		party.members[index].leader = !index;
		party.members[index].you = index == you;
	}
	return party;
}

/* the words as they read: the box's line breaks are spaces */
static wchar_t const *read_out(wchar_t const *text)
{
	static wchar_t out[128];
	int at = 0;

	for (; *text; text++)
	{
		if (*text == L'\r')
			continue;
		out[at++] = *text == L'\n' ? L' ' : *text;
	}
	out[at] = 0;
	return out;
}

static void party_notice_is(int kind, char const *name, int searching, wchar_t const *expected)
{
	struct p2p_party_status party = party_of(0, 2);

	party.notice = kind;
	strcpy(party.notice_name, name);
	matchmaking_party_notice(&party, searching, notice, 96);
	CHECK(!wcscmp(notice, expected), "party notice %d: %ls", kind, notice);
}

static struct p2p_mm_status queued(int can_host)
{
	struct p2p_mm_status status;

	memset(&status, 0, sizeof(status));
	status.state = P2P_MM_QUEUED;
	status.seconds = 12;
	status.can_host = can_host;
	status.playlist = P2P_MM_SLAYER;
	status.size = P2P_MM_2V2;
	status.searching = 3;
	return status;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	struct p2p_mm_status status;

	CASE("looking")
	{
		status = queued(1);
		status.state = P2P_MM_LOOKING;
		status.searching = 1;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(title, L"SEARCHING..."), "one searching state: %ls", title);
		CHECK(!help[0], "no help line: %ls", help);
		CHECK(!wcscmp(count, L"1 searching") && !notice[0], "the count, no notice: %ls, %ls", count, notice);
	}
	CASE("queued")
	{
		status = queued(1);
		lines_of(&status, &no_party);
		CHECK(!wcscmp(title, L"SEARCHING..."), "title: %ls", title);
		CHECK(!wcscmp(count, L"3 searching"), "E9: count: %ls", count);
		CHECK(!help[0], "E9: no help line when healthy: %ls", help);
		status = queued(0);
		lines_of(&status, &no_party);
		CHECK(!wcscmp(help, L"Your router cannot host.\r\nYou can still join games."), "E14: strict: %ls", help);
		status = queued(-1);
		lines_of(&status, &no_party);
		CHECK(!help[0], "E14: not known yet: no line: %ls", help);
		status = queued(1);
		status.searching = 1;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(count, L"1 searching"), "E5: alone: %ls", count);
		status.searching = 0;
		lines_of(&status, &no_party);
		CHECK(!count[0], "no one heard, no count: %ls", count);
	}
	CASE("unreached")
	{
		status = queued(0);
		status.unreached = 1;
		lines_of(&status, &no_party);
		CHECK(!count[0], "E15: no broker, no count: %ls", count);
		CHECK(!wcscmp(help, L"No matchmaking server reached.\r\nStill trying. CANCEL to stop."), "E15: %ls", help);
		status.searching = 0;
		lines_of(&status, &no_party);
		CHECK(!count[0], "E15: no broker, none heard: %ls", count);
		status.searching = 3;
		status.unreached = 0;
		status.can_host = 1;
		lines_of(&status, &no_party);
		CHECK(!help[0] && !wcscmp(count, L"3 searching"),
			"E15: the line clears when a broker answers: %ls, %ls", help, count);
	}
	CASE("widened")
	{
		status = queued(1);
		status.widened = 1;
		status.searching = 5;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(count, L"5 searching"), "E6: %ls", count);
		CHECK(!help[0], "E6: no size words: %ls", help);
		status.can_host = 0;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(help, L"Your router cannot host.\r\nYou can still join games."), "E14: strict: %ls", help);
	}
	CASE("hosting")
	{
		status = queued(1);
		status.state = P2P_MM_HOSTING;
		status.hosts = 1;
		status.players = 2;
		status.most_players = 4;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(title, L"SLAYER 2V2"), "title: %ls", title);
		CHECK(!wcscmp(read_out(help), L"You are hosting. Searchers join you. Or START NOW."), "E1: %ls", help);
		CHECK(!count[0], "the lobby carries the game's players: %ls", count);
	}
	CASE("hosted_panel")
	{
		wchar_t panel[256];
		int lines = 1;
		wchar_t const *at;

		status = queued(1);
		status.state = P2P_MM_HOSTING;
		status.hosts = 1;
		lines_of(&status, &no_party);
		matchmaking_hosted_text(L"", L"SLAYER", help, TRUE, panel, 256);
		for (at = panel; (at = wcsstr(at, L"\r\n")) != NULL; at += 2)
			lines++;
		CHECK(lines <= 7, "lobby_game_data holds 7 lines, not %d: %ls", lines, panel);
		CHECK(wcsstr(panel, L"Or START NOW.\r\nInvite link copied:\r\npaste it to friends") != NULL, "help and invite: %ls", panel);
		matchmaking_hosted_text(L"", L"SLAYER", help, FALSE, panel, 256);
		CHECK(!wcscmp(read_out(panel), L" SLAYER You are hosting. Searchers join you. Or START NOW."), "no link: %ls", panel);
	}
	CASE("found")
	{
		status = queued(1);
		status.state = P2P_MM_FOUND;
		status.players = 3;
		status.most_players = 4;
		status.ping = 48;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(title, L"MATCH FOUND"), "title: %ls", title);
		CHECK(!wcscmp(count, L"3 of 4 matched"), "count: %ls", count);
		CHECK(!wcscmp(help, L"Joining the game:\r\nSLAYER 2V2, 48 ms"), "help: %ls", help);
		status.ping = -1;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(help, L"Joining the game:\r\nSLAYER 2V2"), "a matchmade game, not timed: %ls", help);
		status.hosts = 1;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(read_out(help), L"You are hosting. Searchers join you. Or START NOW."), "this machine hosts: %ls", help);
	}
	CASE("big_looking")
	{
		status = queued(1);
		status.state = P2P_MM_LOOKING;
		status.size = P2P_MM_BIG_TEAM;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(title, L"LOOKING FOR A GAME"), "title: %ls", title);
		CHECK(!wcscmp(help, L"Looking for the best big game near you.\r\nIf none fits, you host one."), "help: %ls", help);
		status.state = P2P_MM_QUEUED;
		status.can_host = 0;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(title, L"LOOKING FOR A GAME") && !count[0], "a big size has no queue: %ls, %ls", title, count);
		CHECK(wcsstr(help, L"Your router cannot host") && !wcsstr(help, L"you host one"), "strict: %ls", help);
	}
	CASE("big_hosting")
	{
		wchar_t line[32];

		status = queued(1);
		status.state = P2P_MM_HOSTING;
		status.hosts = 1;
		status.size = P2P_MM_BIG_FFA;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(title, L"SLAYER BIG FFA"), "title: %ls", title);
		CHECK(!wcscmp(help, L"Or START NOW."), "help: %ls", help);
		matchmaking_lobby_line(-1, TRUE, 2, 128, line, 32);
		CHECK(!wcscmp(line, L"2 of 128 players"), "the lobby: %ls", line);
		matchmaking_lobby_line(2, TRUE, 2, 128, line, 32);
		CHECK(!wcscmp(line, L"Game starts in 3"), "after START NOW: %ls", line);
	}
	CASE("big_found")
	{
		status = queued(1);
		status.state = P2P_MM_FOUND;
		status.size = P2P_MM_BIG_TEAM;
		status.players = 9;
		status.most_players = 16;
		status.ping = -1;
		strcpy(status.region, "eu");
		lines_of(&status, &no_party);
		CHECK(!wcscmp(title, L"MATCH FOUND") && !count[0], "no pool to count: %ls, %ls", title, count);
		CHECK(wcsstr(help, L"SLAYER BIG TEAM\r\n9 of 16 players, EU") != NULL, "a dedicated server's region: %ls", help);
		status.region[0] = 0;
		status.players = 47;
		status.most_players = 128;
		status.ping = 48;
		lines_of(&status, &no_party);
		CHECK(wcsstr(help, L"SLAYER BIG TEAM\r\n47 of 128 players, 48 ms") != NULL, "a player's game: %ls", help);
	}
	CASE("lobby")
	{
		wchar_t line[32];

		matchmaking_lobby_line(4, FALSE, 0, 0, line, 32);
		CHECK(!wcscmp(line, L"Game starts in 5"), "the first message: %ls", line);
		matchmaking_lobby_line(0, FALSE, 0, 0, line, 32);
		CHECK(!wcscmp(line, L"Game starts in 1"), "the last message: %ls", line);
		matchmaking_lobby_line(-1, FALSE, 0, 0, line, 32);
		CHECK(!wcscmp(line, L"Waiting for players."), "no countdown, no number: %ls", line);
	}
	CASE("notices")
	{
		status = queued(1);
		status.notice = P2P_MM_NOTICE_HOST_LEFT;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(notice, L"THE HOST LEFT. BACK IN THE QUEUE, YOUR PLACE KEPT."), "host left: %ls", notice);
		status.notice = P2P_MM_NOTICE_HOST_UNREACHED;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(notice, L"COULD NOT REACH THE HOST. BACK IN THE QUEUE."), "host unreached: %ls", notice);
		status.notice = P2P_MM_NOTICE_HOST_FAILED;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(notice, L"COULD NOT HOST THE GAME. BACK IN THE QUEUE."), "host failed: %ls", notice);
		status.notice = P2P_MM_NOTICE_TOO_BIG;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(notice, L"THE PARTY IS TOO BIG FOR THAT GAME. PICK A BIGGER GAME."), "too big: %ls", notice);
		status.notice = P2P_MM_NOTICE_APART;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(notice, L"COULD NOT GET THE PARTY INTO ONE GAME. SEARCH STOPPED."), "apart: %ls", notice);
		status.notice = P2P_MM_NOTICE_NONE;
		lines_of(&status, &no_party);
		CHECK(!notice[0], "none: %ls", notice);
	}
	CASE("member_view")
	{
		struct matchmaking_party_view view = { _matchmaking_ask_none, NONE, FALSE, FALSE };
		struct p2p_party_status party = party_of(1, 3);

		CHECK(matchmaking_party_member(&party), "Rook is a member");
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(help), L"Chief searches for you. You join Chief's team."),
			"help: %ls", help);
		CHECK(!wcscmp(line, L"Waiting for Chief to search."), "one line: %ls", line);
		strcpy(party.members[0].name, "WWWWWWWWWWW");
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(line, L"Waiting for WWWWWWWWWWW\r\nto search."), "long name: %ls", line);
		strcpy(party.members[0].name, "Chief");
		CHECK(matchmaking_party_bar(&party, _matchmaking_ask_none) == (MATCHMAKING_BAR(leave) | MATCHMAKING_BAR(cancel)),
			"the bar: LEAVE PARTY and CANCEL");
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 0), L"LEADER"), "Chief's mark");
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 1), L"YOU"), "Rook's mark");
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 2), L""), "Kat's mark");
		view.asking = _matchmaking_ask_leave;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(help, L"Leave Chief's party?"), "E61: %ls", help);
		CHECK(!line[0], "E61: no line: %ls", line);
		CHECK(matchmaking_party_bar(&party, view.asking) == (MATCHMAKING_BAR(yes) | MATCHMAKING_BAR(no)), "E61: YES and NO");
	}
	CASE("member_queue")
	{
		struct p2p_party_status party = party_of(1, 3);

		status = queued(1);
		status.party = 3;
		lines_of(&status, &party);
		CHECK(!wcscmp(title, L"SEARCHING..."), "E49: title: %ls", title);
		CHECK(!wcscmp(count, L"3 searching"), "E49: the count: %ls", count);
		CHECK(!wcscmp(help, L"Chief searches for you.\r\nYou follow Chief into the game."), "E49: %ls", help);
		strcpy(party.members[0].name, "WWWWWWWWWWW");
		lines_of(&status, &party);
		CHECK(!wcscmp(help, L"WWWWWWWWWWW searches for you.\r\nYou follow them into the game."), "long name: %ls", help);
		strcpy(party.members[0].name, "Chief");
		CHECK(matchmaking_search_bar(&party) == MATCHMAKING_SEARCH_BAR(leave), "E49: LEAVE SEARCH (B) only");
		status.state = P2P_MM_LOOKING;
		lines_of(&status, &party);
		CHECK(wcsstr(help, L"Chief searches for you") != NULL, "E49: looking: %ls", help);
		CHECK(matchmaking_search_bar(&party) == MATCHMAKING_SEARCH_BAR(leave), "E49: looking: the bar");
		party = party_of(0, 3);
		status = queued(1);
		lines_of(&status, &party);
		CHECK(!help[0], "the leader's own lines: %ls", help);
		CHECK(matchmaking_search_bar(&party) == MATCHMAKING_SEARCH_BAR(cancel), "the leader's bar: CANCEL only");
	}
	CASE("member_found")
	{
		struct p2p_party_status party = party_of(1, 3);

		status = queued(1);
		status.state = P2P_MM_FOUND;
		status.players = 0;
		status.most_players = 0;
		lines_of(&status, &party);
		CHECK(!wcscmp(title, L"MATCH FOUND") && !count[0], "a member knows no pool: %ls, %ls", title, count);
	}
	CASE("party_sizes")
	{
		static short const offered[] = { P2P_MM_3V3, P2P_MM_4V4, P2P_MM_FFA_6, P2P_MM_BIG_TEAM, P2P_MM_BIG_FFA };
		short index;

		CHECK(matchmaking_party_size(P2P_MM_3V3, P2P_MM_2V2, 3) == P2P_MM_BIG_FFA, "E22: left of 3V3");
		CHECK(matchmaking_party_size(P2P_MM_BIG_FFA, P2P_MM_1V1, 3) == P2P_MM_3V3, "E22: right of BIG FFA");
		CHECK(matchmaking_party_size(P2P_MM_1V1, P2P_MM_1V1, 3) == P2P_MM_3V3, "E22: a saved 1V1 goes up");
		CHECK(matchmaking_party_size(P2P_MM_1V1, P2P_MM_1V1, 2) == P2P_MM_2V2, "a party of 2: a saved 1V1");
		CHECK(matchmaking_party_size(P2P_MM_4V4, P2P_MM_FFA_6, 4) == P2P_MM_BIG_TEAM, "a party of 4: past FFA 6");
		CHECK(matchmaking_party_size(P2P_MM_1V1, P2P_MM_1V1, 5) == P2P_MM_BIG_TEAM, "a party of 5: BIG TEAM");
		CHECK(matchmaking_party_size(P2P_MM_BIG_TEAM, P2P_MM_FFA_6, 5) == P2P_MM_BIG_FFA, "a party of 5: left of BIG TEAM");
		CHECK(matchmaking_size_after(1u << P2P_MM_CTF, P2P_MM_BIG_FFA, 5) == P2P_MM_BIG_TEAM, "no BIG FFA for CTF: BIG TEAM");
		for (index = 0; index < (short)(sizeof(offered) / sizeof(offered[0])); index++)
			CHECK(matchmaking_party_size(P2P_MM_1V1, offered[index], 3) == offered[index], "E22: offered: %d", index);
		CHECK(matchmaking_size_after(1u << P2P_MM_CTF, P2P_MM_FFA_6, 3) == P2P_MM_3V3, "E22: no FFA for CTF: the party's size");
		CHECK(matchmaking_size_after(1u << P2P_MM_CTF, P2P_MM_FFA_6, 1) == P2P_MM_2V2, "alone: as ever");
		CHECK(!matchmaking_size_saved(P2P_MM_2V2, P2P_MM_3V3, 3), "a size the party forced is not saved");
		CHECK(!matchmaking_size_saved(P2P_MM_3V3, P2P_MM_3V3, 3), "a playlist change leaves the saved size");
		CHECK(matchmaking_size_saved(P2P_MM_3V3, P2P_MM_4V4, 3), "a size the leader stepped to is saved");
	}
	CASE("party_panel")
	{
		struct matchmaking_party_view view = { _matchmaking_ask_none, NONE, TRUE, FALSE };
		struct p2p_party_status party = party_of(0, 3);
		char name[P2P_PARTY_NAME_SIZE + 1];
		wchar_t back[P2P_PARTY_NAME_SIZE + 1];

		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(help), L"A party of 3 plays 3V3 or larger: 1V1 and 2V2 are skipped."), "the size's help: %ls", help);
		CHECK(!line[0], "no line before INVITE: %ls", line);
		CHECK(matchmaking_party_bar(&party, _matchmaking_ask_none) == (MATCHMAKING_BAR(defaults) | MATCHMAKING_BAR(invite) |
			MATCHMAKING_BAR(search) | MATCHMAKING_BAR(cancel)), "the leader's bar");
		party.search_state = P2P_MM_QUEUED;
		CHECK(matchmaking_party_bar(&party, _matchmaking_ask_none) == (MATCHMAKING_BAR(invite) | MATCHMAKING_BAR(search) |
			MATCHMAKING_BAR(cancel)), "searching: no DEFAULTS");
		party = party_of(0, 2);
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(help), L"A party of 2 plays 2V2 or larger: 1V1 is skipped."), "a party of 2: %ls", help);
		party = party_of(0, 5);
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(help), L"A party of 5 plays BIG TEAM or larger: 1V1 to FFA 6 are skipped."), "a party of 5: %ls", help);
		party = party_of(0, 3);
		view.size_focused = FALSE;
		view.focused = 2;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(help), L"X removes Kat from the party and makes a new link."), "on Kat: %ls", help);
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 2), L"X REMOVE"), "on Kat: the tag");
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 0), L"LEADER"), "on Kat: the leader's mark");
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 1), L"X"), "on Kat: Rook's X stays");
		view.asking = _matchmaking_ask_remove;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(help), L"Remove Kat from the party?"), "asking: %ls", help);
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 2), L"REMOVE?"), "asking: the tag");
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 1), L""), "asking: no X on Rook");
		CHECK(matchmaking_party_bar(&party, view.asking) == (MATCHMAKING_BAR(yes) | MATCHMAKING_BAR(no)), "asking: YES and NO");
		{
			struct p2p_party_status alone = party_of(1, 1);
			struct matchmaking_party_view view = { _matchmaking_ask_none, NONE, FALSE, FALSE };

			CHECK(!wcscmp(matchmaking_party_mark(&alone, &view, 0), L"LEADER"), "a joiner the leader's record has not reached");
		}
		CHECK(matchmaking_party_find(&party, 2, "Kat") == 2, "the asked member, in place");
		party = party_of(0, 3);
		strcpy(party.members[1].name, "Kat");
		strcpy(party.members[2].name, "Rook");
		CHECK(matchmaking_party_find(&party, 2, "Kat") == 1, "the asked member, moved up");
		CHECK(matchmaking_party_find(&party, 2, "Gone") == 3, "the asked member, gone");
		strcpy(party.members[2].name, party.members[1].name);
		CHECK(matchmaking_party_find(&party, 2, party.members[1].name) == 2, "of two with one name, the one asked about");
		party = party_of(0, 0);
		view = (struct matchmaking_party_view){ _matchmaking_ask_none, NONE, FALSE, FALSE };
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(line), L"Just you. INVITE friends: they search with you and play on your team."), "alone: %ls", line);
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 0), L"YOU"), "alone: the mark");
		view.invited = TRUE;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(line), L"Party link copied."), "INVITE, Discord off: %ls", line);
		view.discord = TRUE;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(line), L"Party link copied, and on Discord's INVITE."), "INVITE: %ls", line);
		view.invited = view.discord = FALSE;
		party.notice = P2P_PARTY_NOTICE_REMOVED;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(help), L"You are on your own. SEARCH to look for a game, or INVITE friends."), "removed: %ls", help);
		CHECK(!line[0], "removed: no line: %ls", line);
		party = party_of(1, 2);
		party.members[0].name[0] = 0;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!help[0] && !line[0], "no name for the leader yet: %ls %ls", help, line);
		matchmaking_party_name_utf8(L"Ch\x00efef\x4e16", name, sizeof(name));
		mods_wide_text(name, back, P2P_PARTY_NAME_SIZE + 1);
		CHECK(!wcscmp(back, L"Ch\x00efef\x4e16"), "a name through UTF-8: %ls", back);
	}
	CASE("party_notices")
	{
		party_notice_is(P2P_PARTY_NOTICE_LEFT, "Kat", 2, L"KAT LEFT THE PARTY. SEARCHING FOR A PARTY OF 2.");
		party_notice_is(P2P_PARTY_NOTICE_LEFT, "Kat", 0, L"KAT LEFT THE PARTY.");
		party_notice_is(P2P_PARTY_NOTICE_LEFT_SEARCH, "Kat", 2, L"KAT LEFT THE SEARCH. SEARCHING FOR A PARTY OF 2.");
		party_notice_is(P2P_PARTY_NOTICE_LEFT_SEARCH, "Kat", 0, L"KAT LEFT THE SEARCH.");
		party_notice_is(P2P_PARTY_NOTICE_COULD_NOT_JOIN, "Kat", 2, L"KAT COULD NOT JOIN. THE PARTY LEFT THAT GAME.");
		party_notice_is(P2P_PARTY_NOTICE_ENDED, "Chief", 0, L"THE PARTY ENDED: CHIEF LEFT.");
		party_notice_is(P2P_PARTY_NOTICE_ENDED, "", 0, L"THE PARTY ENDED.");
		party_notice_is(P2P_PARTY_NOTICE_REMOVED, "Chief", 0, L"CHIEF REMOVED YOU FROM THE PARTY.");
		party_notice_is(P2P_PARTY_NOTICE_FULL, "", 0, L"THE PARTY IS FULL.");
		party_notice_is(P2P_PARTY_NOTICE_TOO_BIG, "", 0, L"THE PARTY IS TOO BIG FOR THAT GAME. PICK A BIGGER GAME.");
		party_notice_is(P2P_PARTY_NOTICE_APART, "", 0, L"COULD NOT GET THE PARTY INTO ONE GAME. SEARCH STOPPED.");
		party_notice_is(P2P_PARTY_NOTICE_NONE, "", 0, L"");
		{
			struct p2p_party_status party = party_of(0, 2);

			party.notice = P2P_PARTY_NOTICE_LEFT;
			strcpy(party.notice_name, "Kat");
			status = queued(1);
			status.party = 2;
			lines_of(&status, &party);
			CHECK(!wcscmp(notice, L"KAT LEFT THE PARTY. SEARCHING FOR A PARTY OF 2."), "in the queue: %ls", notice);
			status.notice = P2P_MM_NOTICE_HOST_LEFT;
			lines_of(&status, &party);
			CHECK(wcsstr(notice, L"THE HOST LEFT") != NULL, "the host's first: %ls", notice);
			status = queued(1);
			status.state = P2P_MM_FOUND;
			lines_of(&status, &party);
			CHECK(!notice[0], "found: no line: %ls", notice);
		}
	}
	return 0;
}
