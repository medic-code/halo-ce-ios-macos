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
		strcpy(party.members[index].name, names[index % 5]);
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
	status.seconds = status.mark_seconds = 12;
	status.can_host = can_host;
	status.playlist = P2P_MM_SLAYER;
	status.size = P2P_MM_2V2;
	status.count = 3;
	status.needed = 4;
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
		status.games_checked = 12;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(title, L"LOOKING FOR A GAME"), "title: %ls", title);
		CHECK(!wcscmp(help, L"Checking 12 public games..."), "help: %ls", help);
		CHECK(!count[0] && !notice[0], "no count, no notice: %ls, %ls", count, notice);
	}
	CASE("queued")
	{
		status = queued(1);
		lines_of(&status, &no_party);
		CHECK(!wcscmp(title, L"IN THE QUEUE"), "title: %ls", title);
		CHECK(!wcscmp(count, L"3 searching"), "count: %ls", count);
		CHECK(wcsstr(help, L"1 more for 2V2") && wcsstr(help, L"At 0:45"), "the next mark: %ls", help);
		status = queued(0);
		lines_of(&status, &no_party);
		CHECK(!wcsstr(help, L"2:00") && wcsstr(help, L"Your router cannot host"), "E13: strict: %ls", help);
		status = queued(-1);
		lines_of(&status, &no_party);
		CHECK(wcsstr(help, L"Your router cannot host") != NULL, "E48: not known yet: greyed: %ls", help);
		status = queued(1);
		status.count = 1;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(count, L"1 searching"), "E5: alone: %ls", count);
		status.count = status.needed = 0;
		lines_of(&status, &no_party);
		CHECK(!help[0], "no pool yet, no line about it: %ls", help);
	}
	CASE("widened")
	{
		status = queued(1);
		status.widened = 1;
		status.all_sizes_count = 5;
		lines_of(&status, &no_party);
		CHECK(!wcscmp(count, L"5 searching, all sizes"), "E6: %ls", count);
		CHECK(wcsstr(help, L"Any size") && wcsstr(help, L"2:00"), "E6: any size, then the 2:00 mark: %ls", help);
		status.can_host = 0;
		lines_of(&status, &no_party);
		CHECK(!wcsstr(help, L"2:00") && wcsstr(help, L"Your router cannot host"), "E13: strict: %ls", help);
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
		CHECK(!wcscmp(help, L"Searchers join you."), "E5, E7: %ls", help);
		CHECK(!count[0], "the lobby carries the game's players: %ls", count);
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
		CHECK(!wcscmp(help, L"Searchers join you."), "this machine hosts: %ls", help);
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
		CHECK(!wcscmp(help, L"START NOW starts it"), "help: %ls", help);
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
		CHECK(!wcscmp(line, L"Waiting for players"), "no countdown, no number: %ls", line);
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
		CHECK(!wcscmp(read_out(line), L"Waiting for Chief to search."), "line: %ls", line);
		CHECK(matchmaking_party_bar(&party, _matchmaking_ask_none, FALSE) == (MATCHMAKING_BAR(leave) | MATCHMAKING_BAR(cancel)),
			"the bar: LEAVE PARTY and CANCEL");
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 0), L"LEADER"), "Chief's mark");
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 1), L"YOU"), "Rook's mark");
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 2), L""), "Kat's mark");
		view.asking = _matchmaking_ask_leave;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(help, L"Leave Chief's party?"), "E61: %ls", help);
		CHECK(matchmaking_party_bar(&party, view.asking, FALSE) == (MATCHMAKING_BAR(yes) | MATCHMAKING_BAR(no)), "E61: YES and NO");
	}
	CASE("member_queue")
	{
		struct p2p_party_status party = party_of(1, 3);

		status = queued(1);
		status.party = 3;
		lines_of(&status, &party);
		CHECK(!wcscmp(title, L"IN THE QUEUE"), "member title: %ls", title);
		CHECK(!wcscmp(count, L"3 searching"), "member count: %ls", count);
		CHECK(!wcscmp(help, L"Chief searches for you.\r\nYou follow Chief into the game."), "E49: %ls", help);
		strcpy(party.members[0].name, "WWWWWWWWWWW");
		lines_of(&status, &party);
		CHECK(!wcscmp(help, L"WWWWWWWWWWW searches for you.\r\nYou follow them into the game."), "long name: %ls", help);
		strcpy(party.members[0].name, "Chief");
		CHECK(matchmaking_search_bar(&party, FALSE) == MATCHMAKING_SEARCH_BAR(leave), "E49: LEAVE SEARCH (B) only");
		status.state = P2P_MM_LOOKING;
		lines_of(&status, &party);
		CHECK(wcsstr(help, L"Chief searches for you") != NULL, "E49: looking: %ls", help);
		CHECK(matchmaking_search_bar(&party, FALSE) == MATCHMAKING_SEARCH_BAR(leave), "E49: looking: the bar");
		party = party_of(0, 3);
		status = queued(1);
		lines_of(&status, &party);
		CHECK(!help[0], "the leader's own lines: %ls", help);
		CHECK(matchmaking_search_bar(&party, FALSE) == MATCHMAKING_SEARCH_BAR(cancel), "the leader's bar: CANCEL only");
		CHECK(matchmaking_search_bar(&party, TRUE) == (MATCHMAKING_SEARCH_BAR(yes) | MATCHMAKING_SEARCH_BAR(no)),
			"a friend's invite while searching: YES and NO only");
	}
	CASE("member_found")
	{
		struct p2p_party_status party = party_of(1, 3);

		status = queued(1);
		status.state = P2P_MM_FOUND;
		status.players = 0;
		status.most_players = 0;
		lines_of(&status, &party);
		CHECK(!wcscmp(title, L"MATCH FOUND") && !wcscmp(count, L"0 of 0 matched"), "member match: %ls, %ls", title, count);
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
		CHECK(matchmaking_party_bar(&party, _matchmaking_ask_none, FALSE) == (MATCHMAKING_BAR(defaults) | MATCHMAKING_BAR(invite) |
			MATCHMAKING_BAR(search) | MATCHMAKING_BAR(cancel)), "the leader's bar");
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
		view.asking = _matchmaking_ask_remove;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(help), L"Remove Kat from the party?"), "asking: %ls", help);
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 2), L"REMOVE?"), "asking: the tag");
		CHECK(matchmaking_party_bar(&party, view.asking, FALSE) == (MATCHMAKING_BAR(yes) | MATCHMAKING_BAR(no)), "asking: YES and NO");
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
		CHECK(!wcscmp(read_out(line), L"INVITE friends to search with you."), "alone: %ls", line);
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 0), L"YOU"), "alone: the mark");
		view.invited = TRUE;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(line), L"Party link copied, and on Discord's INVITE."), "INVITE: %ls", line);
		view.invited = FALSE;
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
	CASE("friend_rows")
	{
		struct matchmaking_party_view view = { _matchmaking_ask_none, NONE, FALSE, FALSE };
		struct p2p_party_status party = party_of(0, 3);
		struct p2p_friends_status friends;
		struct p2p_friend_row request = { "0123456789ABCDEF", "Rook#77HDQXB3", 0 };
		wchar_t name[P2P_PARTY_NAME_SIZE + 1], mark[32];

		party.members[1].friend_key[0] = party.members[2].friend_key[0] = 1;
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 1), L"ADD FRIEND"), "a key, not a friend: %ls",
			matchmaking_party_mark(&party, &view, 1));
		party.members[2].friend = 1;
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 2), L"X"), "a friend: no ADD FRIEND");
		party.members[2].friend = 2;
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 2), L"REQUEST SENT"), "a request sent");
		party.members[2].friend = 0;
		memset(party.members[2].friend_key, 0, P2P_FRIEND_KEY_SIZE);
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 2), L"X"), "no key: no ADD FRIEND");
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 0), L"LEADER"), "the leader: no ADD FRIEND");
		view.asking = _matchmaking_ask_remove;
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 1), L""), "asking: no ADD FRIEND");
		view.asking = _matchmaking_ask_none;
		party.search_state = P2P_MM_QUEUED;
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 1), L"X"), "searching: rows are locked");
		party.search_state = P2P_MM_OFF;
		view.focused = 1;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(help), L"X removes Rook from the party and makes a new link. A adds Rook as a friend."),
			"on Rook: %ls", help);
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 1), L"A ADD FRIEND"), "on Rook: A adds");
		view.friends_off = TRUE;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(help), L"X removes Rook from the party and makes a new link."), "friends off: %ls", help);
		view.focused = NONE;
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 1), L"X"), "friends off: no ADD FRIEND");
		view.friends_off = FALSE;
		view.focused = 1;
		party.members[1].friend = 1;
		matchmaking_party_words(&party, &view, help, line, 128);
		CHECK(!wcscmp(read_out(help), L"X removes Rook from the party and makes a new link."), "a friend: %ls", help);
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 1), L"X REMOVE"), "on a friend: X removes");
		view.focused = NONE;
		view.asking = _matchmaking_ask_remove;
		CHECK(!wcscmp(matchmaking_party_mark(&party, &view, 2), L""), "while asking: no standing X");
		view.asking = _matchmaking_ask_none;

		matchmaking_request_row(&request, FALSE, name, mark, 32);
		CHECK(!wcscmp(name, L"Rook") && !wcscmp(mark, L"REQUEST"), "a request row: %ls %ls", name, mark);
		matchmaking_request_row(&request, TRUE, name, mark, 32);
		CHECK(!wcscmp(mark, L"A ANSWER"), "a request row with focus: %ls", mark);
		matchmaking_request_help(&request, FALSE, help, 128);
		CHECK(!wcscmp(read_out(help), L"Rook#77HDQXB3 wants to be friends. A: answer YES or NO."), "request help: %ls", help);
		matchmaking_request_help(&request, TRUE, help, 128);
		CHECK(!wcscmp(read_out(help), L"Rook#77HDQXB3 wants to be friends."), "the question: %ls", help);
		CHECK(!wcscmp(help, L"Rook#77HDQXB3 wants to\r\nbe friends."), "two lines that fit the help box: %ls", help);

		memset(&friends, 0, sizeof(friends));
		friends.request_count = 3;
		party = party_of(0, 2);
		CHECK(matchmaking_requests_shown(&party, &friends) == 3, "3 request rows");
		friends.request_count = 5;
		CHECK(matchmaking_requests_shown(&party, &friends) == P2P_FRIEND_REQUESTS_SHOWN, "at most 3");
		party = party_of(0, 7);
		CHECK(matchmaking_requests_shown(&party, &friends) == 1, "a party of 7 leaves one row");
		party = party_of(0, 8);
		CHECK(matchmaking_requests_shown(&party, &friends) == 0, "a party of 8 leaves none");
		party = party_of(0, 0);
		CHECK(matchmaking_requests_shown(&party, &friends) == P2P_FRIEND_REQUESTS_SHOWN, "alone: the player's own row first");
		party = party_of(0, 2);
		party.search_state = P2P_MM_QUEUED;
		CHECK(matchmaking_requests_shown(&party, &friends) == 0, "searching: no request rows");
		party = party_of(1, 3);
		CHECK(matchmaking_requests_shown(&party, &friends) == P2P_FRIEND_REQUESTS_SHOWN, "a member sees requests");

		matchmaking_add_words(P2P_FRIEND_ADD_NO_SERVER, help, 128);
		CHECK(!wcscmp(help, L"No matchmaking server reached."), "E16: %ls", help);
		matchmaking_add_words(P2P_FRIEND_ADD_FULL, help, 128);
		CHECK(!wcscmp(help, L"Your friends list is full."), "full: %ls", help);
		matchmaking_add_words(P2P_FRIEND_ADD_SENT, help, 128);
		CHECK(!help[0], "sent: the mark says so");
	}
	CASE("friend_layout")
	{
		static struct matchmaking_row rows[MATCHMAKING_ROW_LIMIT];
		struct p2p_party_status party = party_of(0, 6);
		struct p2p_friends_status friends;
		short total, scroll;

		memset(&friends, 0, sizeof(friends));
		friends.on = 1;
		friends.online_count = 4;
		strcpy(friends.online[0].id, "0000000000000001");
		strcpy(friends.online[3].id, "0000000000000004");
		total = matchmaking_rows_layout(&party, &friends, rows);
		CHECK(total == 12, "E10: 6 members, the heading, 4 friends and ADD FRIEND: %d", total);
		CHECK(rows[5].kind == _matchmaking_row_member && rows[5].index == 5, "the last member");
		CHECK(rows[6].kind == _matchmaking_row_heading, "the heading under the members");
		CHECK(rows[7].kind == _matchmaking_row_friend && rows[7].index == 0 && !strcmp(rows[7].id, "0000000000000001"),
			"the first friend carries its id");
		CHECK(rows[10].index == 3 && !strcmp(rows[10].id, "0000000000000004"), "the last friend carries its id");
		CHECK(matchmaking_scroll(rows, total, 0, 0) == 0, "all shown from the top");
		scroll = matchmaking_scroll(rows, total, 0, 1);
		CHECK(scroll == 1, "a step down scrolls one row: %d", scroll);
		CHECK(matchmaking_scroll(rows, total, 4, 1) == 4, "the list ends after the last row: %d", matchmaking_scroll(rows, total, 4, 1));
		CHECK(matchmaking_scroll(rows, total, 0, -1) == 0, "no row above the first");
		CHECK(matchmaking_scroll(rows, total, 2, -1) == 1, "a step up scrolls one row");

		friends.online_count = 0;
		total = matchmaking_rows_layout(&party, &friends, rows);
		CHECK(total == 8 && rows[7].kind == _matchmaking_row_add, "no friends, no requests: the heading and ADD FRIEND: %d", total);
		friends.request_count = 2;
		party = party_of(0, 2);
		total = matchmaking_rows_layout(&party, &friends, rows);
		CHECK(total == 6 && rows[2].kind == _matchmaking_row_heading && rows[3].kind == _matchmaking_row_request,
			"requests under the heading: %d", total);
		friends.request_count = 0;
		friends.online_count = 4;
		party.search_state = P2P_MM_QUEUED;
		CHECK(matchmaking_rows_layout(&party, &friends, rows) == 2, "searching: no friend rows");
		party.search_state = P2P_MM_OFF;
		friends.on = 0;
		CHECK(matchmaking_rows_layout(&party, &friends, rows) == 2, "friends off: no friend rows");
		friends.on = 1;
		party = party_of(0, 8);
		CHECK(matchmaking_rows_layout(&party, &friends, rows) == 8, "a full party has nobody to invite");
		party = party_of(1, 3);
		CHECK(matchmaking_rows_layout(&party, &friends, rows) == 3, "a member of a party: no friend rows");
		friends.request_count = 1;
		total = matchmaking_rows_layout(&party, &friends, rows);
		CHECK(total == 5 && rows[3].kind == _matchmaking_row_heading && rows[4].kind == _matchmaking_row_request,
			"a member: the heading and requests, no friend rows: %d", total);
		friends.request_count = 0;

		party = party_of(0, 7);
		friends.online_count = 4;
		total = matchmaking_rows_layout(&party, &friends, rows);
		CHECK(total == 13 && rows[7].kind == _matchmaking_row_heading, "7 members: the heading is row 7");
		CHECK(matchmaking_scroll(rows, total, 0, 0) == 1, "the heading leaves the bottom slot: %d",
			matchmaking_scroll(rows, total, 0, 0));
		scroll = matchmaking_scroll(rows, total, 1, -1);
		CHECK(scroll == 1, "a step up from member 1 has no good place to go: %d", scroll);
		CHECK(matchmaking_scroll(rows, total, scroll, 0) == 1, "and the next frame holds: %d", matchmaking_scroll(rows, total, scroll, 0));
		party = party_of(0, 0);
		friends.online_count = 12;
		total = matchmaking_rows_layout(&party, &friends, rows);
		CHECK(total == 15 && rows[1].kind == _matchmaking_row_heading, "alone: the heading is row 1");
		scroll = matchmaking_scroll(rows, total, 0, 1);
		CHECK(scroll == 2, "a step down passes the heading, not on the top slot: %d", scroll);
		scroll = matchmaking_scroll(rows, total, 2, -1);
		CHECK(scroll == 0, "a step up passes it too: %d", scroll);
	}
	CASE("add_friend")
	{
		static struct matchmaking_row rows[MATCHMAKING_ROW_LIMIT];
		struct p2p_party_status party = party_of(0, 2);
		struct p2p_friends_status friends;
		wchar_t name[P2P_PARTY_NAME_SIZE + 1], mark[32];
		char text[32];
		short total;

		memset(&friends, 0, sizeof(friends));
		friends.on = 1;
		friends.request_count = 1;
		friends.online_count = 2;
		total = matchmaking_rows_layout(&party, &friends, rows);
		CHECK(total == 7 && rows[6].kind == _matchmaking_row_add, "E2: the last row is ADD FRIEND: %d", total);
		CHECK(rows[5].kind == _matchmaking_row_friend, "after the friends");
		party = party_of(0, 0);
		friends.request_count = friends.online_count = 0;
		total = matchmaking_rows_layout(&party, &friends, rows);
		CHECK(total == 3 && rows[1].kind == _matchmaking_row_heading && rows[2].kind == _matchmaking_row_add,
			"alone, no friends: ADD FRIEND under the heading: %d", total);
		party = party_of(1, 3);
		friends.request_count = 1;
		total = matchmaking_rows_layout(&party, &friends, rows);
		CHECK(total == 5 && rows[4].kind == _matchmaking_row_request, "a member has the request, not ADD FRIEND: %d", total);
		party = party_of(0, 2);
		friends.request_count = 0;
		friends.on = 0;
		CHECK(matchmaking_rows_layout(&party, &friends, rows) == 2, "friends off: no ADD FRIEND");
		friends.on = 1;
		party.search_state = P2P_MM_QUEUED;
		CHECK(matchmaking_rows_layout(&party, &friends, rows) == 2, "searching: no ADD FRIEND");

		CHECK(matchmaking_party_bar(&party, _matchmaking_ask_none, TRUE) == (MATCHMAKING_BAR(add_friend) | MATCHMAKING_BAR(invite) |
			MATCHMAKING_BAR(search) | MATCHMAKING_BAR(cancel)), "E2: bar with ADD FRIEND row");
		matchmaking_add_row(&friends, name, mark, 32);
		CHECK(!wcscmp(name, L"ADD FRIEND") && !mark[0], "the row: %ls %ls", name, mark);
		friends.sent = 1;
		matchmaking_add_row(&friends, name, mark, 32);
		CHECK(!wcscmp(mark, L"REQUEST SENT"), "E2: %ls", mark);
		friends.looking = 1;
		matchmaking_add_row(&friends, name, mark, 32);
		CHECK(!wcscmp(mark, L"LOOKING"), "a lookup runs: %ls", mark);

		matchmaking_add_words(MATCHMAKING_NO_TAG, help, 128);
		CHECK(!wcscmp(help, L"NO PLAYER WITH THAT TAG."), "E22: %ls", help);
		matchmaking_add_words(P2P_FRIEND_ADD_TWO_PLAYERS, help, 128);
		CHECK(!wcscmp(help, L"TWO PLAYERS HAVE THAT TAG."), "E22: %ls", help);
		matchmaking_add_words(P2P_FRIEND_ADD_SELF, help, 128);
		CHECK(!wcscmp(help, L"THAT IS YOUR OWN TAG."), "the player's own tag: %ls", help);
		matchmaking_add_words(P2P_FRIEND_ADD_FRIEND, help, 128);
		CHECK(!wcscmp(help, L"ALREADY A FRIEND."), "a friend's tag: %ls", help);
		matchmaking_add_help(help, 128);
		CHECK(!wcscmp(read_out(help), L"Type the 8 characters after the # in your friend's name. A opens the keyboard."), "help: %ls", help);

		CHECK(matchmaking_tag_typed(L"P3VD8AQ2", text, sizeof(text)) && !strcmp(text, "P3VD8AQ2"), "E2: a bare tag: %s", text);
		CHECK(matchmaking_tag_typed(L"Luke#p3vd8aq2", text, sizeof(text)) && !strcmp(text, "P3VD8AQ2"), "name#tag: %s", text);
		CHECK(matchmaking_tag_typed(L"#P3VD8AQ2", text, sizeof(text)) && !strcmp(text, "P3VD8AQ2"), "a lone #: %s", text);
		CHECK(matchmaking_tag_typed(L"  p3vd8aq2 ", text, sizeof(text)) && !strcmp(text, "P3VD8AQ2"), "spaces trimmed: %s", text);
		CHECK(matchmaking_tag_typed(L"Jos\x00e9#P3VD8AQ2", text, sizeof(text)) && !strcmp(text, "P3VD8AQ2"), "a letter off the keyboard in the name: %s", text);
		CHECK(!matchmaking_tag_typed(L"P3VD8A\x00e9", text, sizeof(text)), "a letter off the keyboard in the tag");
		CHECK(!matchmaking_tag_typed(L"Luke#   ", text, sizeof(text)), "no tag after the #");
		CHECK(!matchmaking_tag_typed(L"", text, sizeof(text)), "nothing typed");
		CHECK(!matchmaking_tag_typed(L"P3VD8AQ2P3VD8AQ2P3VD8AQ2P3VD8AQ2P3VD8AQ2P3VD8AQ2", text, sizeof(text)), "too long for the field");
	}
	CASE("friend_words")
	{
		struct p2p_friend_row friend_row = { "0123456789ABCDEF", "Luke#P3VD8AQ2", P2P_FRIEND_MATCHMAKING };
		wchar_t name[P2P_PARTY_NAME_SIZE + 1], mark[32];

		matchmaking_friend_row(&friend_row, FALSE, FALSE, name, mark, 32);
		CHECK(!wcscmp(name, L"Luke") && !wcscmp(mark, L"INVITE"), "E6: %ls %ls", name, mark);
		matchmaking_friend_row(&friend_row, TRUE, FALSE, name, mark, 32);
		CHECK(!wcscmp(mark, L"A INVITE"), "focused: %ls", mark);
		matchmaking_friend_row(&friend_row, TRUE, TRUE, name, mark, 32);
		CHECK(!wcscmp(mark, L"INVITED"), "invited: %ls", mark);
		friend_row.state = P2P_FRIEND_MENUS;
		matchmaking_friend_row(&friend_row, TRUE, FALSE, name, mark, 32);
		CHECK(!wcscmp(mark, L"IN MENUS"), "E7: not invitable: %ls", mark);
		friend_row.state = P2P_FRIEND_IN_GAME;
		matchmaking_friend_row(&friend_row, TRUE, FALSE, name, mark, 32);
		CHECK(!wcscmp(mark, L"IN A GAME"), "E18: not invitable: %ls", mark);
		friend_row.state = P2P_FRIEND_UPDATE;
		matchmaking_friend_row(&friend_row, TRUE, FALSE, name, mark, 32);
		CHECK(!wcscmp(mark, L"UPDATE"), "another version: %ls", mark);

		friend_row.state = P2P_FRIEND_MATCHMAKING;
		matchmaking_friend_help(&friend_row, help, 128);
		CHECK(!wcscmp(read_out(help), L"Luke#P3VD8AQ2 A invites to your party. X removes from friends."), "help: %ls", help);
		friend_row.state = P2P_FRIEND_IN_GAME;
		matchmaking_friend_help(&friend_row, help, 128);
		CHECK(!wcscmp(read_out(help), L"Luke#P3VD8AQ2 X removes from friends."), "help, not invitable: %ls", help);
		matchmaking_invite_help("Luke#P3VD8AQ2", help, 128);
		CHECK(!wcscmp(help, L"Luke#P3VD8AQ2\r\ninvites you to a party"), "the invite prompt: %ls", help);
		matchmaking_friend_remove_help("Luke#P3VD8AQ2", help, 128);
		CHECK(!wcscmp(read_out(help), L"Remove Luke from your friends?"), "the removal question: %ls", help);
		matchmaking_add_words(MATCHMAKING_LEAVE_FIRST, help, 128);
		CHECK(!wcscmp(help, L"LEAVE YOUR PARTY FIRST."), "E9: %ls", help);
	}
	CASE("friends_line")
	{
		struct p2p_friends_status friends;

		memset(&friends, 0, sizeof(friends));
		strcpy(friends.me, "Chief#K7M4Q2XA");
		friends.on = 1;
		matchmaking_friends_line(&friends, TRUE, notice, 96);
		CHECK(!wcscmp(notice, L"YOU ARE Chief#K7M4Q2XA."), "the player's name and tag: %ls", notice);
		friends.unsaved = 1;
		matchmaking_friends_line(&friends, TRUE, notice, 96);
		CHECK(!wcscmp(notice, L"FRIENDS NOT SAVED: friends.txt not written."), "a failed save: %ls", notice);
		friends.unsaved = 0;
		friends.on = 0;
		matchmaking_friends_line(&friends, TRUE, notice, 96);
		CHECK(!wcscmp(notice, L"FRIENDS OFF: friend.key unreadable."), "an unreadable key: %ls", notice);
		friends.key_unmade = 1;
		matchmaking_friends_line(&friends, TRUE, notice, 96);
		CHECK(!wcscmp(notice, L"FRIENDS OFF: friend.key could not be created."), "no key made: %ls", notice);
		matchmaking_friends_line(&friends, FALSE, notice, 96);
		CHECK(!notice[0], "friends turned off in the settings: nothing shown");
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
